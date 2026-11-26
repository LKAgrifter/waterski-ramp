// Stand-in for the ESP-IDF OTA / partition API: just enough to read the bind phrase block the web flasher writes at
// the end of the app partition. Tests can point g_testPartitionData at a fake partition (nullptr = none).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#define ESP_FAIL -1
#endif
struct esp_partition_t { uint32_t address; uint32_t size; };
extern esp_partition_t g_testPartition;
extern uint8_t *g_testPartitionData;
inline const esp_partition_t *esp_ota_get_running_partition() { return g_testPartitionData ? &g_testPartition : nullptr; }
inline esp_err_t esp_partition_read(const esp_partition_t *, size_t off, void *dst, size_t n)
{ if (!g_testPartitionData) return ESP_FAIL; memcpy(dst, g_testPartitionData + off, n); return ESP_OK; }
