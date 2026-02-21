#include <stdint.h>
#include <string.h>
#include <esp_http_server.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "esp_littlefs.h"
#include <dirent.h>
#include "main.h"
#include "http.h"
#include "config.h"
#include "gps.h"
#include "printManager.h"
#include "conez_usb.h"

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
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(page_buf + page_pos, sizeof(page_buf) - page_pos, fmt, ap);
    va_end(ap);
    if (n > 0 && page_pos + n < (int)sizeof(page_buf))
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
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "FIXME...");
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

    const esp_partition_t *part;
    esp_ota_handle_t ota_handle = 0;

    if (is_firmware) {
        part = esp_ota_get_next_update_partition(NULL);
        if (!part) {
            httpd_resp_sendstr(req, "FAIL: no OTA partition");
            return ESP_FAIL;
        }
        esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
        if (err != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA begin failed: %s", esp_err_to_name(err));
            httpd_resp_sendstr(req, "FAIL: begin error");
            return ESP_FAIL;
        }
    } else {
        // Filesystem: unmount before erasing partition
        esp_vfs_littlefs_unregister("spiffs");
        part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (!part) {
            httpd_resp_sendstr(req, "FAIL: no filesystem partition");
            return ESP_FAIL;
        }
        esp_err_t err = esp_partition_erase_range(part, 0, part->size);
        if (err != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA erase failed: %s", esp_err_to_name(err));
            httpd_resp_sendstr(req, "FAIL: erase error");
            return ESP_FAIL;
        }
    }

    // Stream body chunks
    char buf[1024];
    int remaining = req->content_len;
    size_t offset = 0;
    while (remaining > 0) {
        int toread = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int recv_len = httpd_req_recv(req, buf, toread);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (is_firmware) esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        esp_err_t err;
        if (is_firmware) {
            err = esp_ota_write(ota_handle, buf, recv_len);
        } else {
            err = esp_partition_write(part, offset, buf, recv_len);
            offset += recv_len;
        }
        if (err != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA write failed: %s", esp_err_to_name(err));
            if (is_firmware) esp_ota_abort(ota_handle);
            httpd_resp_sendstr(req, "FAIL: write error");
            return ESP_FAIL;
        }
        remaining -= recv_len;
    }

    if (is_firmware) {
        esp_err_t err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            printfnl(SOURCE_SYSTEM, "OTA end failed: %s", esp_err_to_name(err));
            httpd_resp_sendstr(req, "FAIL: verify error");
            return ESP_FAIL;
        }
        esp_ota_set_boot_partition(part);
    }

    printfnl(SOURCE_SYSTEM, "OTA success: %d bytes", req->content_len);
    httpd_resp_sendstr(req, "OK — rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}


int http_setup()
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    cfg.stack_size = 4096;
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
