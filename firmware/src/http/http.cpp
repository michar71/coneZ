#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <esp_http_server.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "esp_littlefs.h"
#include "mbedtls/md5.h"
#include <dirent.h>
#include "main.h"
#include "http.h"
#include "config.h"
#include "gps.h"
#include "printManager.h"
#include "conez_usb.h"
#include "ota_lock.h"

#ifdef BOARD_HAS_GPS
extern volatile float gps_lat;
extern volatile float gps_lon;
extern volatile bool gps_pos_valid;
extern volatile float gps_alt;
extern volatile bool gps_alt_valid;
#endif

static httpd_handle_t server = NULL;


// ---- Page buffer for HTML generation ----
// ESP-IDF httpd processes requests serially in its task,
// so a single static buffer is safe.
static char page_buf[4096];
static int page_pos;

static void page_reset() { page_pos = 0; page_buf[0] = '\0'; }

static void page_cat(const char *s)
{
    int n = strlen(s);
    if (page_pos + n < (int)sizeof(page_buf)) {
        memcpy(page_buf + page_pos, s, n + 1);
        page_pos += n;
    }
}

__attribute__((format(printf, 1, 2)))
static void page_catf(const char *fmt, ...)
{
    int remain = (int)sizeof(page_buf) - page_pos;
    if (remain <= 1) return;  // no room for data + null terminator
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(page_buf + page_pos, remain, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    // vsnprintf wrote at most remain-1 chars; advance by actual, not intended, length
    if (n >= remain) n = remain - 1;
    page_pos += n;
}

static void page_cat_escaped(const char *str)
{
    while (*str && page_pos < (int)sizeof(page_buf) - 6)
    {
        if (*str == '<')      { memcpy(page_buf + page_pos, "&lt;", 4); page_pos += 4; }
        else if (*str == '>') { memcpy(page_buf + page_pos, "&gt;", 4); page_pos += 4; }
        else if (*str == '&') { memcpy(page_buf + page_pos, "&amp;", 5); page_pos += 5; }
        else                  { page_buf[page_pos++] = *str; }
        str++;
    }
    page_buf[page_pos] = '\0';
}


static void page_cat_gps()
{
#ifdef BOARD_HAS_GPS
    page_cat("<h3>GPS</h3><pre>");
    page_catf("gps_valid=%u\n", (int) gps_pos_valid);
    page_catf("date=%d  time=%d\n", get_date_raw(), get_time_raw());
    page_catf("lat=%0.6f  lon=%0.6f  alt=%dm\n", gps_lat, gps_lon, (int) gps_alt);
    page_cat("</pre><br>\n");
#else
    page_cat("<h3>GPS</h3><pre>No GPS hardware</pre><br>\n");
#endif
}


static void page_cat_partitions()
{
    page_cat("<h3>Firmware Versions in Partitions</h3><pre>");

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL)
    {
        const esp_partition_t* part = esp_partition_get(it);
        esp_app_desc_t desc;
        bool hasInfo = esp_ota_get_partition_description(part, &desc) == ESP_OK;

        page_catf("%s @ 0x%lx size 0x%lx", part->label, part->address, part->size);

        if (part == running) page_cat(" [RUNNING]");
        if (part == boot)    page_cat(" [BOOT]");

        if (hasInfo)
        {
            page_cat("\n  Version: ");
            page_cat_escaped(desc.version);
            page_cat("\n  Project: ");
            page_cat_escaped(desc.project_name);
            page_catf("\n  Built: %s %s", desc.date, desc.time);
        }
        else
        {
            page_cat("\n  <i>No descriptor info</i>");
        }

        page_cat("\n\n");
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    page_cat("</pre>");
}


static esp_err_t http_root(httpd_req_t *req)
{
    page_reset();
    page_cat("<html><body>");
    page_cat_gps();
    page_cat("<hr><br>\n");
    page_cat_partitions();
    page_cat("<hr><br>\n");
    page_cat("<a href='/config'>Configuration</a><br>\n");
    page_cat("<a href='/dir'>List Files</a><br>\n");
    page_cat("<a href='/nvs'>List NVS Parameters</a><br><br>\n");
    page_cat("<a href='/update'>Update Firmware</a><br>\n");
    page_cat("<a href='/reboot'>Reboot</a><br>\n");
    page_cat("</body></html>\n");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page_buf);
    return ESP_OK;
}


static esp_err_t http_reboot(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}


// Show LittleFS directory listing (appends to page_buf).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void page_cat_dir_list(const char *dirname, uint8_t levels = 3)
{
    page_catf("Directory: %s%s\n", dirname, strcmp(dirname, "/") ? "/" : "");

    char fpath[128];
    lfs_path(fpath, sizeof(fpath), dirname);
    DIR *d = opendir(fpath);
    if (!d) {
        page_cat(" - failed to open directory\n");
        return;
    }

    // First pass: list files
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        char entpath[128];
        snprintf(entpath, sizeof(entpath), "%s/%s", fpath, ent->d_name);
        struct stat st;
        if (stat(entpath, &st) != 0) continue;
        if (!S_ISDIR(st.st_mode)) {
            page_catf("  %s%s%s   %u bytes\n",
                dirname, strcmp(dirname, "/") ? "/" : "", ent->d_name, (unsigned)st.st_size);
        }
    }
    page_cat("\n");

    // Second pass: recurse into subdirectories
    bool any_dirs_listed = false;
    rewinddir(d);
    while ((ent = readdir(d)) != NULL) {
        char entpath[128];
        snprintf(entpath, sizeof(entpath), "%s/%s", fpath, ent->d_name);
        struct stat st;
        if (stat(entpath, &st) != 0) continue;
        if (S_ISDIR(st.st_mode) && levels > 0) {
            char subdir[128];
            snprintf(subdir, sizeof(subdir), "%s%s%s",
                     dirname, strcmp(dirname, "/") ? "/" : "", ent->d_name);
            page_cat_dir_list(subdir, levels - 1);
            any_dirs_listed = true;
        }
    }
    closedir(d);

    if (any_dirs_listed)
        page_cat("\n");
}
#pragma GCC diagnostic pop


static esp_err_t http_dir(httpd_req_t *req)
{
    page_reset();
    page_cat("<html><body>\n");
    page_cat("<h3>LittleFS directory listing:</h3><hr>\n<pre>");

    if (!littlefs_mounted) {
        page_cat("LittleFS not mounted.</pre>");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, page_buf);
        return ESP_OK;
    }

    // Start from the root directory.
    page_cat_dir_list("/");

    page_cat("<br><hr>");

    // Filesystem space stats:
    size_t total = 0, used = 0;
    esp_littlefs_info("spiffs", &total, &used);

    // Avoid divide by 0 if no filesystem.
    if( total < 1 )
        total = 1;

    page_catf("<pre>Total bytes: %u\n", (unsigned)total);
    page_catf("Used bytes:  %u   (%0.1f%%)\n", (unsigned)used,
        ((float)used / (float)total) * 100.0f);
    page_catf("Free bytes:  %u   (%0.1f%%)\n", (unsigned)(total - used),
        ((float)(total - used) / (float)total) * 100.0f);
    page_cat("</pre>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page_buf);
    return ESP_OK;
}


static esp_err_t http_nvs(httpd_req_t *req)
{
    page_reset();
    page_cat("<html><body>\n");
    page_cat("<h3>NVS Contents</h3><hr>\n");

    nvs_stats_t stats;
    if (nvs_get_stats(NVS_DEFAULT_PART_NAME, &stats) == ESP_OK) {
        page_cat("<pre>");
        page_catf("Entries: %u used / %u total  (%u free, %u namespaces)\n",
                  (unsigned)stats.used_entries,
                  (unsigned)stats.total_entries,
                  (unsigned)stats.free_entries,
                  (unsigned)stats.namespace_count);
        page_cat("</pre>\n");
    }

    nvs_iterator_t it = NULL;
    nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
    if (it == NULL) {
        page_cat("<p>No NVS entries found.</p></body></html>");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, page_buf);
        return ESP_OK;
    }

    page_cat("<pre>");
    const char *prev_ns = "";
    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        if (strcmp(info.namespace_name, prev_ns) != 0) {
            if (prev_ns[0]) page_cat("\n");
            page_catf("[%s]\n", info.namespace_name);
            prev_ns = info.namespace_name;
        }

        nvs_handle_t handle;
        if (nvs_open(info.namespace_name, NVS_READONLY, &handle) != ESP_OK) {
            page_catf("  %-16s = [open failed]\n", info.key);
            nvs_entry_next(&it);
            continue;
        }

        switch (info.type) {
        case NVS_TYPE_I8: {
            int8_t v = 0; nvs_get_i8(handle, info.key, &v);
            page_catf("  %-16s i8   = %d\n", info.key, v);
            break;
        }
        case NVS_TYPE_U8: {
            uint8_t v = 0; nvs_get_u8(handle, info.key, &v);
            page_catf("  %-16s u8   = %u\n", info.key, v);
            break;
        }
        case NVS_TYPE_I16: {
            int16_t v = 0; nvs_get_i16(handle, info.key, &v);
            page_catf("  %-16s i16  = %d\n", info.key, v);
            break;
        }
        case NVS_TYPE_U16: {
            uint16_t v = 0; nvs_get_u16(handle, info.key, &v);
            page_catf("  %-16s u16  = %u\n", info.key, v);
            break;
        }
        case NVS_TYPE_I32: {
            int32_t v = 0; nvs_get_i32(handle, info.key, &v);
            page_catf("  %-16s i32  = %d\n", info.key, (int)v);
            break;
        }
        case NVS_TYPE_U32: {
            uint32_t v = 0; nvs_get_u32(handle, info.key, &v);
            page_catf("  %-16s u32  = %u\n", info.key, (unsigned)v);
            break;
        }
        case NVS_TYPE_I64: {
            int64_t v = 0; nvs_get_i64(handle, info.key, &v);
            page_catf("  %-16s i64  = %lld\n", info.key, (long long)v);
            break;
        }
        case NVS_TYPE_U64: {
            uint64_t v = 0; nvs_get_u64(handle, info.key, &v);
            page_catf("  %-16s u64  = %llu\n", info.key, (unsigned long long)v);
            break;
        }
        case NVS_TYPE_STR: {
            size_t len = 0;
            if (nvs_get_str(handle, info.key, NULL, &len) == ESP_OK && len > 0) {
                char *str = (char *)malloc(len);
                if (str && nvs_get_str(handle, info.key, str, &len) == ESP_OK) {
                    page_catf("  %-16s str  = \"", info.key);
                    page_cat_escaped(str);
                    page_cat("\"\n");
                } else {
                    page_catf("  %-16s str  = [read error]\n", info.key);
                }
                free(str);
            } else {
                page_catf("  %-16s str  = [empty]\n", info.key);
            }
            break;
        }
        case NVS_TYPE_BLOB: {
            size_t len = 0;
            nvs_get_blob(handle, info.key, NULL, &len);
            page_catf("  %-16s blob[%u] = ", info.key, (unsigned)len);
            if (len > 0) {
                const size_t max_hex = 32;
                uint8_t *blob = (uint8_t *)malloc(len);
                if (blob && nvs_get_blob(handle, info.key, blob, &len) == ESP_OK) {
                    size_t show = (len < max_hex) ? len : max_hex;
                    for (size_t i = 0; i < show; i++)
                        page_catf("%02X ", blob[i]);
                    if (len > max_hex)
                        page_catf("... (%u more)", (unsigned)(len - max_hex));
                } else {
                    page_cat("[read error]");
                }
                free(blob);
            }
            page_cat("\n");
            break;
        }
        default:
            page_catf("  %-16s ?    = [unsupported type %d]\n", info.key, info.type);
            break;
        }

        nvs_close(handle);
        nvs_entry_next(&it);
    }

    page_cat("</pre>\n</body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page_buf);
    return ESP_OK;
}


static esp_err_t http_config_get(httpd_req_t *req)
{
    const char *msg = "";
    char qbuf[32];
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < sizeof(qbuf)) {
        httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf));
        char val[2];
        if (httpd_query_key_value(qbuf, "saved", val, sizeof(val)) == ESP_OK)
            msg = "Settings saved. Reboot to apply non-debug changes.";
        else if (httpd_query_key_value(qbuf, "reset", val, sizeof(val)) == ESP_OK)
            msg = "Settings reset to defaults. Reboot to apply non-debug changes.";
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, config_get_html(msg));
    return ESP_OK;
}


static esp_err_t http_config_post(httpd_req_t *req)
{
    int body_len = req->content_len;
    if (body_len <= 0 || body_len >= (int)sizeof(page_buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad form data");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < body_len) {
        int ret = httpd_req_recv(req, page_buf + received, body_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }
        received += ret;
    }
    page_buf[received] = '\0';

    config_set_from_web(page_buf);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/config?saved=1");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t http_config_reset(httpd_req_t *req)
{
    config_reset();
    config_apply_debug();

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/config?reset=1");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t http_update_page(httpd_req_t *req)
{
    page_reset();
    page_cat("<html><body>");
    page_cat("<h2>Firmware Update</h2>");
    page_cat("<script>"
        "function doUpload(){"
            "var f=document.getElementById('file').files[0];"
            "if(!f){alert('No file selected');return;}"
            "var t=document.querySelector('input[name=\"type\"]:checked').value;"
            "var s=document.getElementById('status');"
            "s.textContent='Uploading '+f.name+' ('+f.size+' bytes)...';"
            "fetch('/update?type='+t,{method:'POST',body:f})"
            ".then(r=>r.text())"
            ".then(txt=>{s.textContent=txt;})"
            ".catch(e=>{s.textContent='Error: '+e;});"
        "}"
    "</script>");
    page_cat("<form onsubmit='doUpload();return false;'>");
    page_cat("<input type='radio' name='type' value='firmware' checked> Firmware ");
    page_cat("<input type='radio' name='type' value='filesystem'> Filesystem<br><br>");
    page_cat("<input type='file' id='file'><br><br>");
    page_cat("<input type='submit' value='Upload'>");
    page_cat("</form>");
    page_cat("<div id='status'></div><hr>");
    page_cat_partitions();
    page_cat("</body></html>");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page_buf);
    return ESP_OK;
}


// Apply an OTA upload to the inactive app slot (firmware) or the LittleFS data
// partition (filesystem). Caller holds the ota_lock for the whole call. On success
// it reboots and never returns; on any failure it returns ESP_FAIL and the caller
// releases the lock.
// Heap scratch for the OTA handler. Kept off the (4 KB) httpd task stack: a 1 KB
// stream/read-back bounce buffer + the filesystem MD5 state. DRAM, not PSRAM -- buf
// is handed to httpd_req_recv / esp_ota_write / esp_partition_read+write, which
// access it by real address, and this board's PSRAM is not memory-mapped (it is
// reached through the SPI driver's psram_read/write, so a PSRAM handle can't be
// passed to those APIs). ~1.1 KB, held only for the duration of one upload.
typedef struct {
    char                buf[1024];   // recv + read-back bounce buffer
    mbedtls_md5_context md5;         // fs: stream hash, then reused for the read-back
} ota_scratch_t;

static esp_err_t http_update_apply(httpd_req_t *req, bool is_firmware)
{
    ota_scratch_t *s = (ota_scratch_t *) malloc(sizeof(ota_scratch_t));
    if (!s) { httpd_resp_sendstr(req, "FAIL: no memory"); return ESP_FAIL; }

    const esp_partition_t *part = NULL;
    esp_ota_handle_t ota_handle = 0;
    bool ota_open = false, md5_open = false;
    const size_t SECTOR = 4096;
    int    remaining = req->content_len;
    size_t offset = 0, erased = 0;

    if (is_firmware) {
        part = esp_ota_get_next_update_partition(NULL);
        if (!part) { httpd_resp_sendstr(req, "FAIL: no OTA partition"); goto fail; }
        esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
        if (err != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA begin failed: %s", esp_err_to_name(err));
            httpd_resp_sendstr(req, "FAIL: begin error"); goto fail;
        }
        ota_open = true;
    } else {
        part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (!part) { httpd_resp_sendstr(req, "FAIL: no filesystem partition"); goto fail; }
        // LENGTH CHECK FIRST -- a LittleFS image fills the whole partition (its block
        // count is derived from the partition size). Reject a wrong-size upload now,
        // while the FS is still MOUNTED + intact, so a truncated/oversized image
        // can't even start to brick it.
        if (req->content_len != (int)part->size) {
            char m[96];
            snprintf(m, sizeof(m), "FAIL: fs image must be exactly %u bytes (got %d)",
                     (unsigned)part->size, req->content_len);
            printfnl(SOURCE_SYSTEM, "FS upload rejected: %s", m);
            httpd_resp_sendstr(req, m); goto fail;
        }
        // Committed: unmount so config/cue/dist don't touch the FS while we rewrite.
        esp_vfs_littlefs_unregister("spiffs");
        littlefs_mounted = false;
        mbedtls_md5_init(&s->md5); mbedtls_md5_starts(&s->md5); md5_open = true;
    }

    // Stream body chunks (s->buf). Firmware -> esp_ota_write (erases on demand).
    // Filesystem -> raw partition write, erasing each 4 KB sector JUST BEFORE we
    // write into it (not one big up-front erase, which would block the httpd task
    // for seconds and risk a watchdog reset); MD5 the bytes for a post-write verify.
    while (remaining > 0) {
        int toread = remaining < (int)sizeof(s->buf) ? remaining : (int)sizeof(s->buf);
        int recv_len = httpd_req_recv(req, s->buf, toread);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            goto fail;
        }

        esp_err_t err = ESP_OK;
        if (is_firmware) {
            err = esp_ota_write(ota_handle, s->buf, recv_len);
        } else {
            while (err == ESP_OK && erased < offset + (size_t)recv_len) {
                err = esp_partition_erase_range(part, erased, SECTOR);
                if (err == ESP_OK) erased += SECTOR;
            }
            if (err == ESP_OK) err = esp_partition_write(part, offset, s->buf, recv_len);
            if (err == ESP_OK) {
                mbedtls_md5_update(&s->md5, (const unsigned char *)s->buf, recv_len);
                offset += recv_len;
            }
        }
        if (err != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA write failed: %s", esp_err_to_name(err));
            httpd_resp_sendstr(req, "FAIL: write error"); goto fail;
        }
        remaining -= recv_len;
    }

    if (is_firmware) {
        esp_err_t err = esp_ota_end(ota_handle); ota_open = false;   // handle consumed
        if (err != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA end failed: %s", esp_err_to_name(err));
            httpd_resp_sendstr(req, "FAIL: verify error"); goto fail;
        }
        esp_err_t serr = esp_ota_set_boot_partition(part);   // final image validity check
        if (serr != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA set_boot_partition failed: %s", esp_err_to_name(serr));
            httpd_resp_sendstr(req, "FAIL: image rejected (boot partition not set)"); goto fail;
        }
    } else {
        // VERIFY: read the partition back and compare its MD5 to what we streamed --
        // catches a bad flash write BEFORE we reboot into a corrupt FS. On a mismatch
        // we do NOT reboot; the FS is left unmounted (writes are guarded) so the
        // operator simply re-uploads.
        uint8_t want[16]; mbedtls_md5_finish(&s->md5, want);
        mbedtls_md5_starts(&s->md5);                 // reuse the context for the read-back
        for (size_t pos = 0; pos < part->size; ) {
            size_t n = part->size - pos; if (n > sizeof(s->buf)) n = sizeof(s->buf);
            if (esp_partition_read(part, pos, s->buf, n) != ESP_OK) {
                httpd_resp_sendstr(req, "FAIL: verify read error"); goto fail;
            }
            mbedtls_md5_update(&s->md5, (const unsigned char *)s->buf, n);
            pos += n;
            if ((pos & 0xFFFF) == 0) vTaskDelay(1);   // yield ~every 64 KB
        }
        uint8_t got[16]; mbedtls_md5_finish(&s->md5, got); md5_open = false; mbedtls_md5_free(&s->md5);
        if (memcmp(want, got, 16) != 0) {
            printfnl(SOURCE_SYSTEM, "FS verify MISMATCH -- not rebooting; re-upload");
            httpd_resp_sendstr(req, "FAIL: verify mismatch (FS not committed; re-upload)");
            goto fail;
        }
        printfnl(SOURCE_SYSTEM, "FS verify OK (md5 match)");
    }

    free(s);
    printfnl(SOURCE_SYSTEM, "OTA success: %d bytes", req->content_len);
    httpd_resp_sendstr(req, "OK — rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;   // never reached

fail:
    if (ota_open) esp_ota_abort(ota_handle);
    if (md5_open) mbedtls_md5_free(&s->md5);
    free(s);
    return ESP_FAIL;
}

static esp_err_t http_update_post(httpd_req_t *req)
{
    // Read query param "type"
    char qbuf[32], typeval[16] = "firmware";
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < sizeof(qbuf)) {
        httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf));
        httpd_query_key_value(qbuf, "type", typeval, sizeof(typeval));
    }

    bool is_firmware = (strcmp(typeval, "filesystem") != 0);
    const char *label = is_firmware ? "firmware" : "filesystem";

    printfnl(SOURCE_SYSTEM, "OTA %s upload: %d bytes", label, req->content_len);

    // Serialize against the LoRa dist firmware OTA (same inactive slot) and any
    // other update; interleaved erase/write would corrupt the image.
    if (!ota_lock_acquire(is_firmware ? "http-fw" : "http-fs")) {
        printfnl(SOURCE_SYSTEM, "OTA refused: %s already in progress",
                 ota_lock_owner() ? ota_lock_owner() : "another update");
        httpd_resp_sendstr(req, "FAIL: another update in progress (LoRa OTA?)");
        return ESP_FAIL;
    }

    esp_err_t r = http_update_apply(req, is_firmware);   // reboots on success
    ota_lock_release();                                  // only reached on failure
    return r;
}


int http_setup()
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    cfg.stack_size = 4096;   // OTA handler keeps its 1 KB buffer + MD5 state on the heap
    cfg.core_id = 1;

    if (httpd_start(&server, &cfg) != ESP_OK) {
        usb_printf("HTTP server failed to start\n");
        return -1;
    }

    static const httpd_uri_t routes[] = {
        { "/",             HTTP_GET,  http_root,         NULL },
        { "/reboot",       HTTP_GET,  http_reboot,       NULL },
        { "/dir",          HTTP_GET,  http_dir,          NULL },
        { "/nvs",          HTTP_GET,  http_nvs,          NULL },
        { "/config",       HTTP_GET,  http_config_get,   NULL },
        { "/config",       HTTP_POST, http_config_post,  NULL },
        { "/config/reset", HTTP_POST, http_config_reset, NULL },
        { "/update",       HTTP_GET,  http_update_page,  NULL },
        { "/update",       HTTP_POST, http_update_post,  NULL },
    };
    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++)
        httpd_register_uri_handler(server, &routes[i]);

    usb_printf("HTTP server started on port 80\n");
    return 0;
}


int http_loop()
{
    // esp_http_server runs in its own FreeRTOS task — no polling needed
    return 0;
}
