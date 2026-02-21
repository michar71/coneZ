#include <Arduino.h>
#include <ElegantOTA.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "main.h"
#include "fwupdate.h"


void dump_partitions()
{
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();

  Serial.println( "---- Partition Table ----" );

  esp_partition_iterator_t it;
  const esp_partition_t* part;

  it = esp_partition_find( ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL );
  while( it != NULL )
  {
    part = esp_partition_get( it );
    Serial.printf( "Partition: %-16s  Offset: 0x%08X  Size: 0x%06X (%u KB)  Type: 0x%02X/0x%02X",
                   part->label, part->address, part->size, part->size / 1024, part->type, part->subtype );

    if( part == running ) Serial.print( " [RUNNING]" );
    if( part == boot )    Serial.print( " [BOOT]" );

    Serial.println();
    it = esp_partition_next( it );
  }
  esp_partition_iterator_release( it );

  Serial.println( "" );

  // Print boot partition if it's not part of the APP partitions
  if( boot && running && boot != running )
  {
    Serial.printf( "Boot partition is different from currently running:\n  BOOT: %s at 0x%08X\n  RUNNING: %s at 0x%08X\n",
                   boot->label, boot->address, running->label, running->address );
  }
}


void dump_nvs()
{
  Serial.println("---- NVS Parameters ----");

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("NVS init failed, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY);

  if (it == NULL) {
    Serial.println("No NVS entries found.");
    return;
  }

  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    Serial.printf("Namespace: %-12s Key: %-16s Type: ", info.namespace_name, info.key);

    nvs_handle_t handle;
    if (nvs_open(info.namespace_name, NVS_READONLY, &handle) != ESP_OK) {
      Serial.println("  [Failed to open namespace]");
      it = nvs_entry_next(it);
      continue;
    }

    switch (info.type) {
      case NVS_TYPE_I8: {
        int8_t val;
        nvs_get_i8(handle, info.key, &val);
        Serial.printf("int8_t    = %d\n", val);
        break;
      }
      case NVS_TYPE_U8: {
        uint8_t val;
        nvs_get_u8(handle, info.key, &val);
        Serial.printf("uint8_t   = %u\n", val);
        break;
      }
      case NVS_TYPE_I16: {
        int16_t val;
        nvs_get_i16(handle, info.key, &val);
        Serial.printf("int16_t   = %d\n", val);
        break;
      }
      case NVS_TYPE_U16: {
        uint16_t val;
        nvs_get_u16(handle, info.key, &val);
        Serial.printf("uint16_t  = %u\n", val);
        break;
      }
      case NVS_TYPE_I32: {
        int32_t val;
        nvs_get_i32(handle, info.key, &val);
        Serial.printf("int32_t   = %d\n", (int)val);
        break;
      }
      case NVS_TYPE_U32: {
        uint32_t val;
        nvs_get_u32(handle, info.key, &val);
        Serial.printf("uint32_t  = %u\n", (unsigned)val);
        break;
      }
      case NVS_TYPE_I64: {
        int64_t val;
        nvs_get_i64(handle, info.key, &val);
        Serial.printf("int64_t   = %lld\n", val);
        break;
      }
      case NVS_TYPE_U64: {
        uint64_t val;
        nvs_get_u64(handle, info.key, &val);
        Serial.printf("uint64_t  = %llu\n", val);
        break;
      }
      case NVS_TYPE_STR: {
        size_t len = 0;
        nvs_get_str(handle, info.key, NULL, &len);
        char *str = (char *)malloc(len);
        if (str && nvs_get_str(handle, info.key, str, &len) == ESP_OK) {
          Serial.printf("string    = \"%s\"\n", str);
        } else {
          Serial.println("string    = [error reading]");
        }
        free(str);
        break;
      }
      case NVS_TYPE_BLOB: {
        size_t len = 0;
        nvs_get_blob(handle, info.key, NULL, &len);
        uint8_t *blob = (uint8_t *)malloc(len);
        if (blob && nvs_get_blob(handle, info.key, blob, &len) == ESP_OK) {
          Serial.printf("blob[%u]  = ", (unsigned)len);
          for (size_t i = 0; i < len; i++) {
            Serial.printf("%02X ", blob[i]);
          }
          Serial.println();
        } else {
          Serial.println("blob      = [error reading]");
        }
        free(blob);
        break;
      }
      default:
        Serial.println("unknown   = [unsupported type]");
        break;
    }

    nvs_close(handle);
    it = nvs_entry_next(it);
  }

  Serial.println("");
}


void print_nvs_stats()
{
  nvs_stats_t stats;
  esp_err_t err = nvs_get_stats(NVS_DEFAULT_PART_NAME, &stats);

  if (err != ESP_OK) {
    Serial.printf("Failed to get NVS stats: %s\n", esp_err_to_name(err));
    return;
  }

  Serial.println("NVS Usage Statistics:");
  Serial.printf("  Used entries     : %d\n", stats.used_entries);
  Serial.printf("  Free entries     : %d\n", stats.free_entries);
  Serial.printf("  Total entries    : %d\n", stats.total_entries);
  Serial.printf("  Namespace count  : %d\n", stats.namespace_count);
  Serial.println( "" );
}
