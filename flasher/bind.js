// Bind phrase for the radio encryption (see firmware Radio_Security.ino).
// The phrase is written into the last 4 KB of the app partition as one more "part" of the install, so the stock
// ESP Web Tools installer flashes it along with the firmware. Nothing is sent anywhere: it all happens in this page.
//
// Block layout (must match rsReadFlashedPhrase): "WSRBIND1", 1 length byte, the phrase (UTF-8, 1-64 bytes),
// remote ID (1-4, 0 = not set), remote type / water mode (0 BOAT, 1 TIMER, 0xFF = not set), flash number (4 bytes,
// random, never 0: a remote applies the ID / type once per flash), CRC-32 (little-endian) of everything from the
// length byte to the flash number, then 0xFF to the end of the 4 KB.
(function () {
  'use strict';
  const MAGIC = 'WSRBIND1';
  const BLOCK = 4096;
  const KEY_PREFIX = 'waterski-ramp/bind/v1:';
  const MIN_LEN = 8, MAX_BYTES = 64;
  const enc = new TextEncoder();

  function crc32(bytes) {
    let crc = 0xFFFFFFFF;
    for (const b of bytes) {
      crc ^= b;
      for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  async function sha256(bytes) {
    return new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  }

  // The phrase as it will be used: spaces at the ends dropped
  function clean(phrase) { return (phrase || '').trim(); }

  // '' if the phrase is fine, otherwise what's wrong with it
  function problem(phrase) {
    const p = clean(phrase);
    if (p.length < MIN_LEN) return 'Use at least ' + MIN_LEN + ' characters.';
    if (enc.encode(p).length > MAX_BYTES) return 'Too long: at most ' + MAX_BYTES + ' characters.';
    return '';
  }

  // The 4-character code the boards show on their NETWORK page ("Bind: 3F2A")
  async function keyCode(phrase) {
    const key = await sha256(enc.encode(KEY_PREFIX + clean(phrase)));
    const check = await sha256(key);
    return [check[0], check[1]].map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('');
  }

  // The 4 KB block to flash at the end of the app partition.
  // opts (remotes): { remoteId: 1-4, waterMode: 0 BOAT / 1 TIMER }. flashId is random unless given (tests).
  function bindBlock(phrase, opts) {
    opts = opts || {};
    const p = enc.encode(clean(phrase));
    const block = new Uint8Array(BLOCK).fill(0xFF), dv = new DataView(block.buffer);
    let flashId = opts.flashId;
    while (!flashId) flashId = crypto.getRandomValues(new Uint32Array(1))[0];
    block.set(enc.encode(MAGIC), 0);
    block[8] = p.length;
    block.set(p, 9);
    let i = 9 + p.length;
    block[i++] = (opts.remoteId >= 1 && opts.remoteId <= 4) ? opts.remoteId : 0;
    block[i++] = (opts.waterMode === 0 || opts.waterMode === 1) ? opts.waterMode : 0xFF;
    dv.setUint32(i, flashId >>> 0, true); i += 4;
    dv.setUint32(i, crc32(block.subarray(8, i)), true);
    return block;
  }

  // The app partition the board boots (ota_0, or a factory app) from partitions.bin: {offset, size}
  function appPartition(table) {
    const b = new Uint8Array(table), dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    let factory = null;
    for (let i = 0; i + 32 <= b.length; i += 32) {
      if (b[i] !== 0xAA || b[i + 1] !== 0x50) break;          // End of the table (0xFF) or its MD5 entry (0xEB)
      const type = b[i + 2], sub = b[i + 3];
      const part = { offset: dv.getUint32(i + 4, true), size: dv.getUint32(i + 8, true) };
      if (type === 0 && sub === 0x10) return part;            // app, ota_0 (what boot_app0.bin selects)
      if (type === 0 && sub === 0x00) factory = part;
    }
    if (factory) return factory;
    throw new Error('No app partition in partitions.bin');
  }

  const cache = {};
  async function fetchBytes(url) {
    if (!cache[url]) {
      cache[url] = fetch(url).then(r => {
        if (!r.ok) throw new Error('Could not download ' + url + ' (' + r.status + ')');
        return r.arrayBuffer();
      });
    }
    return cache[url];
  }

  // A manifest for ESP Web Tools: the published one plus the bind block. Returns a blob: URL to give the button.
  async function manifestWithPhrase(manifestUrl, phrase, opts) {
    const base = new URL(manifestUrl, location.href);
    const manifest = JSON.parse(new TextDecoder().decode(await fetchBytes(base.href)));
    for (const build of manifest.builds) {
      const parts = build.parts.map(p => ({ path: new URL(p.path, base).href, offset: p.offset }));
      const table = parts.find(p => /partitions\.bin$/.test(p.path));
      const app = parts.find(p => /firmware\.bin$/.test(p.path));
      if (!table || !app) throw new Error('Manifest is missing partitions.bin or firmware.bin');
      const part = appPartition(await fetchBytes(table.path));
      const appBytes = (await fetchBytes(app.path)).byteLength;
      if (app.offset !== part.offset) throw new Error('firmware.bin is not at the app partition');
      if (appBytes + BLOCK > part.size) throw new Error('Firmware too big to leave room for the bind phrase');
      const blob = new Blob([bindBlock(phrase, opts)], { type: 'application/octet-stream' });
      parts.push({ path: URL.createObjectURL(blob), offset: part.offset + part.size - BLOCK });
      build.parts = parts;
    }
    return URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }));
  }

  window.RampBind = { clean, problem, keyCode, bindBlock, appPartition, manifestWithPhrase, crc32 };
})();
