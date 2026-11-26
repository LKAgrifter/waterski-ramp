// ===========================================================================
// RADIO SECURITY - an identical copy of this tab is in BOTH sketches (like Games.ino). Edit one, copy it to the other.
//
// Every LoRa packet is encrypted and signed with a key made from the BIND PHRASE, so only units that were flashed with
// the same phrase can read each other or move the ramp. There is no pairing step: flash the ramp and every remote with
// the same phrase and they work together. A packet from anything else (a different phrase, old firmware, someone
// else's board) is ignored and counted on the NETWORK page ("Bad key" / "Old fw").
//
// WHERE THE PHRASE COMES FROM (the first one found wins)
//   1. bind_phrase.h in the sketch folder, containing:   #define BIND_PHRASE "your phrase here"
//      For boards you program from the Arduino IDE. That file is in .gitignore: NEVER commit it, or paste your phrase
//      anywhere in the code - the repository is public.
//   2. The web flasher: it asks for the phrase and writes it into the last 4 KB of the app partition. Flashing later
//      from the Arduino IDE keeps it (the IDE doesn't write that far), so a web-flashed board stays bound.
//      For a remote, the flasher also writes the remote ID and type (BOAT / TIMER water mode) picked there; the remote
//      saves them on its first boot after that flash (applyFlashedSettings in Ramp_Remote.ino). The ramp ignores them.
//   3. Neither: the board is UNBOUND. It uses a fixed, publicly known key, so it still talks to other unbound boards
//      (handy on the bench) but anyone could command it. The NETWORK page says "Bind: NONE (open)".
// Both units show a 4-character code for their key on the NETWORK page ("Bind: 3F2A"). Units with the same code have
// the same phrase. The web flasher shows the code for the phrase you type, so you can check before you flash.
//
// HOW (for whoever maintains this): ChaCha20-Poly1305 (RFC 8439) with the Poly1305 tag cut to 4 bytes, as LoRaWAN does,
// to keep packets short (airtime). On the air:
//     [sender 1][frame counter 4][encrypted payload][tag 4]
//   sender  = 0xA1 for the ramp, 0xB0 + remote ID (0xB1..0xB4) for a remote
//   counter = counts every packet this unit sends and NEVER repeats (it continues across reboots: blocks of
//             RS_FCNT_BLOCK are reserved in flash), so no two packets are ever encrypted with the same nonce.
// Key = SHA-256("waterski-ramp/bind/v1:" + phrase). Nonce = sender, counter, then zeros.
// Played-back (recorded) packets are refused: a receiver only accepts a counter higher than the last one it took from
// that sender, and every remote command is also signed together with the counter of the ramp packet the remote just
// heard (not sent - the ramp tries its last few). A command recorded earlier no longer matches anything the ramp sent
// recently, even after the ramp reboots.
// Self-contained: no libraries. Tested against the RFC 8439 / SHA-256 test vectors in tests/host/tests/radio_security.cpp.
// ===========================================================================

#include "esp_ota_ops.h"   // To read the phrase the web flasher wrote at the end of the app partition
#if __has_include("bind_phrase.h")
#include "bind_phrase.h"   // Optional and never committed: #define BIND_PHRASE "your phrase"
#endif
#ifndef BIND_PHRASE
#define BIND_PHRASE ""
#endif

// (RS_HEADER_LEN, RS_TAG_LEN, RS_SENDER_RAMP and RS_SENDER_REMOTE are in the main tab: the LORA tab uses them too.)
constexpr uint8_t  RS_BIND_MAX = 64;           // Longest phrase
constexpr uint32_t RS_BIND_BLOCK = 4096;       // The web flasher writes the phrase in the last 4 KB of the app partition
constexpr uint32_t RS_FCNT_BLOCK = 4096;       // Frame counters reserved in flash at a time (one flash write per block)
// The block: "WSRBIND1", phrase length, phrase, remote ID (0 = not set), water mode (0 BOAT, 1 TIMER, 0xFF = not set),
// flash number (4 bytes, random per flash, never 0), CRC-32 of everything from the length byte to the flash number.
const char RS_BIND_MAGIC[9] = "WSRBIND1";
const char RS_KEY_PREFIX[] = "waterski-ramp/bind/v1:";

uint8_t  rsKey[32];               // The radio key (from the phrase)
char     rsKeyCode[5] = "----";   // First 2 bytes of SHA-256(key) in hex, shown as "Bind: 3F2A"
uint8_t  rsBindSource = 0;        // 0 = none (unbound), 1 = web flasher, 2 = bind_phrase.h
uint32_t rsTxFcnt = 0;            // Next frame counter to send
uint32_t rsTxFcntReserved = 0;    // Counters below this are reserved in flash
// (rsFlashRemoteId, rsFlashWaterMode and rsFlashId - the remote settings from the web flasher - are in the main tab.)

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
uint32_t rsLe32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void rsPutLe32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

uint32_t rsRotl(uint32_t v, int n)
{
  return (v << n) | (v >> (32 - n));
}

// CRC-32 (the zlib / PNG one), to check the block the web flasher wrote
uint32_t rsCrc32(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
  }
  return ~crc;
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------
const uint32_t RS_SHA_K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void rsSha256Block(uint32_t h[8], const uint8_t blk[64])
{
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) | ((uint32_t)blk[i * 4 + 2] << 8) | blk[i * 4 + 3];
  for (int i = 16; i < 64; i++)
  {
    uint32_t s0 = rsRotl(w[i - 15], 25) ^ rsRotl(w[i - 15], 14) ^ (w[i - 15] >> 3);
    uint32_t s1 = rsRotl(w[i - 2], 15) ^ rsRotl(w[i - 2], 13) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
  for (int i = 0; i < 64; i++)
  {
    uint32_t S1 = rsRotl(e, 26) ^ rsRotl(e, 21) ^ rsRotl(e, 7);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = k + S1 + ch + RS_SHA_K[i] + w[i];
    uint32_t S0 = rsRotl(a, 30) ^ rsRotl(a, 19) ^ rsRotl(a, 10);
    uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + mj;
    k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

// SHA-256 of two pieces joined together (prefix + text), so no buffer is needed to join them
void rsSha256(const uint8_t *a, size_t aLen, const uint8_t *b, size_t bLen, uint8_t out[32])
{
  uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
  uint8_t blk[64];
  size_t fill = 0;
  uint64_t total = (uint64_t)(aLen + bLen);

  for (size_t i = 0; i < aLen + bLen; i++)
  {
    blk[fill++] = (i < aLen) ? a[i] : b[i - aLen];
    if (fill == 64) { rsSha256Block(h, blk); fill = 0; }
  }
  blk[fill++] = 0x80;
  if (fill > 56) { while (fill < 64) blk[fill++] = 0; rsSha256Block(h, blk); fill = 0; }
  while (fill < 56) blk[fill++] = 0;
  uint64_t bits = total * 8;
  for (int i = 7; i >= 0; i--) blk[fill++] = (uint8_t)(bits >> (i * 8));
  rsSha256Block(h, blk);

  for (int i = 0; i < 8; i++)
  {
    out[i * 4] = (uint8_t)(h[i] >> 24); out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(h[i] >> 8); out[i * 4 + 3] = (uint8_t)h[i];
  }
}

// ---------------------------------------------------------------------------
// ChaCha20 (RFC 8439 section 2.3)
// ---------------------------------------------------------------------------
void rsChachaBlock(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t out[64])
{
  uint32_t s[16], x[16];
  s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;   // "expand 32-byte k"
  for (int i = 0; i < 8; i++) s[4 + i] = rsLe32(key + i * 4);
  s[12] = counter;
  s[13] = rsLe32(nonce); s[14] = rsLe32(nonce + 4); s[15] = rsLe32(nonce + 8);
  for (int i = 0; i < 16; i++) x[i] = s[i];

  for (int round = 0; round < 10; round++)
  {
    const uint8_t q[8][4] = { {0, 4, 8, 12}, {1, 5, 9, 13}, {2, 6, 10, 14}, {3, 7, 11, 15},
                              {0, 5, 10, 15}, {1, 6, 11, 12}, {2, 7, 8, 13}, {3, 4, 9, 14} };
    for (int j = 0; j < 8; j++)
    {
      uint32_t &a = x[q[j][0]], &b = x[q[j][1]], &c = x[q[j][2]], &d = x[q[j][3]];
      a += b; d ^= a; d = rsRotl(d, 16);
      c += d; b ^= c; b = rsRotl(b, 12);
      a += b; d ^= a; d = rsRotl(d, 8);
      c += d; b ^= c; b = rsRotl(b, 7);
    }
  }
  for (int i = 0; i < 16; i++) rsPutLe32(out + i * 4, x[i] + s[i]);
}

// ---------------------------------------------------------------------------
// Poly1305 (RFC 8439 section 2.5), 26-bit limbs
// ---------------------------------------------------------------------------
void rsPoly1305(const uint8_t key[32], const uint8_t *m, size_t len, uint8_t tag[16])
{
  const uint32_t r0 = (rsLe32(key + 0)) & 0x3ffffff;
  const uint32_t r1 = (rsLe32(key + 3) >> 2) & 0x3ffff03;
  const uint32_t r2 = (rsLe32(key + 6) >> 4) & 0x3ffc0ff;
  const uint32_t r3 = (rsLe32(key + 9) >> 6) & 0x3f03fff;
  const uint32_t r4 = (rsLe32(key + 12) >> 8) & 0x00fffff;
  const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0, c;

  while (len > 0)
  {
    uint8_t blk[17] = {0};
    size_t n = (len < 16) ? len : 16;
    memcpy(blk, m, n);
    blk[n] = 1;              // The extra 1 bit after the block (bit 128 for a full block)
    h0 += (rsLe32(blk + 0)) & 0x3ffffff;
    h1 += (rsLe32(blk + 3) >> 2) & 0x3ffffff;
    h2 += (rsLe32(blk + 6) >> 4) & 0x3ffffff;
    h3 += (rsLe32(blk + 9) >> 6) & 0x3ffffff;
    h4 += (rsLe32(blk + 12) >> 8) | ((uint32_t)blk[16] << 24);

    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

    c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
    d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
    d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
    d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
    d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;

    m += n;
    len -= n;
  }

  c = h1 >> 26; h1 &= 0x3ffffff;
  h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
  h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
  h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
  h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
  h1 += c;

  // h - p, and keep it if h >= p
  uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - (1UL << 26);
  uint32_t mask = (g4 >> 31) - 1;     // All ones if h >= p
  h0 = (h0 & ~mask) | (g0 & mask);
  h1 = (h1 & ~mask) | (g1 & mask);
  h2 = (h2 & ~mask) | (g2 & mask);
  h3 = (h3 & ~mask) | (g3 & mask);
  h4 = (h4 & ~mask) | (g4 & mask);

  uint32_t w0 = h0 | (h1 << 26);
  uint32_t w1 = (h1 >> 6) | (h2 << 20);
  uint32_t w2 = (h2 >> 12) | (h3 << 14);
  uint32_t w3 = (h3 >> 18) | (h4 << 8);

  uint64_t f;
  f = (uint64_t)w0 + rsLe32(key + 16);             rsPutLe32(tag + 0, (uint32_t)f);
  f = (uint64_t)w1 + rsLe32(key + 20) + (f >> 32); rsPutLe32(tag + 4, (uint32_t)f);
  f = (uint64_t)w2 + rsLe32(key + 24) + (f >> 32); rsPutLe32(tag + 8, (uint32_t)f);
  f = (uint64_t)w3 + rsLe32(key + 28) + (f >> 32); rsPutLe32(tag + 12, (uint32_t)f);
}

// ---------------------------------------------------------------------------
// ChaCha20-Poly1305 AEAD (RFC 8439 section 2.8). Small packets only: aadLen <= 16, len <= 64.
// encrypt = true: data is plaintext, turned into ciphertext in place. false: data is ciphertext, the tag is computed
// over it (the caller checks the tag BEFORE decrypting with rsAeadCrypt).
// ---------------------------------------------------------------------------
void rsAeadTag(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t aadLen,
               const uint8_t *cipher, size_t len, uint8_t tag[16])
{
  uint8_t block0[64];
  rsChachaBlock(key, 0, nonce, block0);          // First 32 bytes = the one-time Poly1305 key

  uint8_t mac[16 + 64 + 16];
  size_t n = 0;
  memset(mac, 0, sizeof(mac));
  memcpy(mac, aad, aadLen);
  n = (aadLen + 15) & ~(size_t)15;
  memcpy(mac + n, cipher, len);
  n += (len + 15) & ~(size_t)15;
  mac[n] = (uint8_t)aadLen;                      // Lengths as 64-bit little-endian (both are small)
  mac[n + 8] = (uint8_t)len;
  n += 16;
  rsPoly1305(block0, mac, n, tag);
}

void rsAeadCrypt(const uint8_t key[32], const uint8_t nonce[12], uint8_t *data, size_t len)
{
  uint8_t ks[64];
  rsChachaBlock(key, 1, nonce, ks);              // Counter 1 for the data
  for (size_t i = 0; i < len; i++) data[i] ^= ks[i];
}

// ---------------------------------------------------------------------------
// Packets
// ---------------------------------------------------------------------------
void rsNonce(uint8_t sender, uint32_t fcnt, uint8_t nonce[12])
{
  memset(nonce, 0, 12);
  nonce[0] = sender;
  rsPutLe32(nonce + 1, fcnt);
}

// Build an encrypted packet in out[] (room for RS_HEADER_LEN + len + RS_TAG_LEN bytes). Returns its length.
// hasEcho: sign it together with echo (a remote passes the counter of the ramp packet it just heard).
uint8_t rsSeal(uint8_t sender, uint32_t fcnt, bool hasEcho, uint32_t echo, const uint8_t *plain, uint8_t len, uint8_t *out)
{
  uint8_t nonce[12], aad[RS_HEADER_LEN + 4], tag[16];
  out[0] = sender;
  rsPutLe32(out + 1, fcnt);
  memcpy(out + RS_HEADER_LEN, plain, len);
  rsNonce(sender, fcnt, nonce);
  rsAeadCrypt(rsKey, nonce, out + RS_HEADER_LEN, len);

  memcpy(aad, out, RS_HEADER_LEN);
  if (hasEcho) rsPutLe32(aad + RS_HEADER_LEN, echo);
  rsAeadTag(rsKey, nonce, aad, RS_HEADER_LEN + (hasEcho ? 4 : 0), out + RS_HEADER_LEN, len, tag);
  memcpy(out + RS_HEADER_LEN + len, tag, RS_TAG_LEN);
  return RS_HEADER_LEN + len + RS_TAG_LEN;
}

// Check and decrypt a packet. plainOut gets size - RS_HEADER_LEN - RS_TAG_LEN bytes. False = not from a unit with
// our key (or signed with a different echo) - plainOut is then left alone.
bool rsOpen(const uint8_t *in, uint16_t size, bool hasEcho, uint32_t echo, uint8_t *plainOut)
{
  if (size < RS_HEADER_LEN + RS_TAG_LEN || size > RS_HEADER_LEN + 64 + RS_TAG_LEN)
    return false;

  uint8_t len = (uint8_t)(size - RS_HEADER_LEN - RS_TAG_LEN);
  uint8_t nonce[12], aad[RS_HEADER_LEN + 4], tag[16];
  rsNonce(in[0], rsLe32(in + 1), nonce);
  memcpy(aad, in, RS_HEADER_LEN);
  if (hasEcho) rsPutLe32(aad + RS_HEADER_LEN, echo);
  rsAeadTag(rsKey, nonce, aad, RS_HEADER_LEN + (hasEcho ? 4 : 0), in + RS_HEADER_LEN, len, tag);

  uint8_t diff = 0;
  for (uint8_t i = 0; i < RS_TAG_LEN; i++) diff |= (uint8_t)(tag[i] ^ in[RS_HEADER_LEN + len + i]);
  if (diff != 0)
    return false;

  memcpy(plainOut, in + RS_HEADER_LEN, len);
  rsAeadCrypt(rsKey, nonce, plainOut, len);
  return true;
}

uint32_t rsPacketFcnt(const uint8_t *in)
{
  return rsLe32(in + 1);
}

// Frame counter for the next packet this unit sends. Never repeats, also across reboots: a block of RS_FCNT_BLOCK
// counters is reserved in flash at a time, and a reboot starts at the next unused block.
uint32_t rsNextFcnt()
{
  if (rsTxFcnt >= rsTxFcntReserved)
  {
    rsTxFcntReserved = rsTxFcnt + RS_FCNT_BLOCK;
    prefs.putULong("RadioFcnt", rsTxFcntReserved);
  }
  return rsTxFcnt++;
}

// ---------------------------------------------------------------------------
// Bind phrase and key
// ---------------------------------------------------------------------------

// The phrase (and remote settings) the web flasher wrote at the end of the app partition. False if there is none (or
// it's damaged).
bool rsReadFlashedPhrase(char *out)
{
  const esp_partition_t *part = esp_ota_get_running_partition();
  if (part == nullptr || part->size < RS_BIND_BLOCK)
    return false;

  uint8_t buf[8 + 1 + RS_BIND_MAX + 6 + 4];
  if (esp_partition_read(part, part->size - RS_BIND_BLOCK, buf, sizeof(buf)) != ESP_OK)
    return false;
  if (memcmp(buf, RS_BIND_MAGIC, 8) != 0)
    return false;

  uint8_t n = buf[8];
  if (n == 0 || n > RS_BIND_MAX)
    return false;
  if (rsCrc32(buf + 8, 1 + n + 6) != rsLe32(buf + 15 + n))
    return false;

  memcpy(out, buf + 9, n);
  out[n] = '\0';
  rsFlashRemoteId = buf[9 + n];
  rsFlashWaterMode = buf[10 + n];
  rsFlashId = rsLe32(buf + 11 + n);
  return true;
}

// Make the key from a phrase ("" = unbound)
void rsSetPhrase(const char *phrase)
{
  rsSha256((const uint8_t *)RS_KEY_PREFIX, strlen(RS_KEY_PREFIX), (const uint8_t *)phrase, strlen(phrase), rsKey);

  uint8_t check[32];
  rsSha256(rsKey, 32, nullptr, 0, check);
  snprintf(rsKeyCode, sizeof(rsKeyCode), "%02X%02X", check[0], check[1]);
}

// Call once in setup(), after prefs.begin() and before the radio starts
void radioSecurityBegin()
{
  char phrase[RS_BIND_MAX + 1];
  rsFlashRemoteId = 0;
  rsFlashWaterMode = 0xFF;
  rsFlashId = 0;

  if (strlen(BIND_PHRASE) > 0)
  {
    strncpy(phrase, BIND_PHRASE, RS_BIND_MAX);
    phrase[RS_BIND_MAX] = '\0';
    rsBindSource = 2;
  }
  else if (rsReadFlashedPhrase(phrase))
  {
    rsBindSource = 1;
  }
  else
  {
    phrase[0] = '\0';
    rsBindSource = 0;
  }
  rsSetPhrase(phrase);
  memset(phrase, 0, sizeof(phrase));

  // Frame counter: continue after the last block reserved before this boot
  rsTxFcnt = prefs.getULong("RadioFcnt", 0);
  rsTxFcntReserved = rsTxFcnt;   // Reserve a fresh block on the first send

  Serial.printf("Radio key: %s (%s)\n", rsBindSource ? rsKeyCode : "NONE",
                rsBindSource == 2 ? "bind_phrase.h" : rsBindSource == 1 ? "web flasher" : "unbound - open");
}

// For the NETWORK page: "Bind: 3F2A" or "Bind: NONE (open)"
const char *bindText()
{
  static char buf[24];
  if (rsBindSource == 0)
    snprintf(buf, sizeof(buf), "Bind: NONE (open)");
  else
    snprintf(buf, sizeof(buf), "Bind: %s", rsKeyCode);
  return buf;
}
