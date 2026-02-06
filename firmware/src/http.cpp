#include <Arduino.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <LittleFS.h>
#include <FS.h>
#include "main.h"
#include "http.h"
#include "gps.h"

#ifdef BOARD_HAS_GPS
#include <TinyGPSPlus.h>
extern TinyGPSPlus gps;
extern volatile float gps_lat;
extern volatile float gps_lon;
extern volatile bool gps_pos_valid;
extern volatile float gps_alt;
extern volatile bool gps_alt_valid;
#endif

WebServer server(80);



String html_escape( const char* str )
{
    String escaped;
    while (*str)
    {
        if (*str == '<') escaped += "&lt;";
        else if (*str == '>') escaped += "&gt;";
        else if (*str == '&') escaped += "&amp;";
        else escaped += *str;
        str++;
    }
 
    return escaped;
}


String http_get_gps()
{
#ifdef BOARD_HAS_GPS
    char buf[128];

    String out = "<h3>GPS</h3><pre>";

    snprintf( buf, sizeof( buf ), "gps_valid=%u\n", (int) gps_pos_valid );
    out += buf;
    snprintf( buf, sizeof( buf ), "date=%d  time=%d\n", gps.date.isValid() ? gps.date.value() : -1, gps.time.isValid() ? gps.time.value() : -1 );
    out += buf;
    snprintf( buf, sizeof( buf ), "lat=%0.6f  lon=%0.6f  alt=%dm\n", gps_lat, gps_lon, (int) gps_alt  );
    out += buf;

    out += "</pre><br>\n";

    return out;
#else
    return "<h3>GPS</h3><pre>No GPS hardware</pre><br>\n";
#endif
}


String getPartitionInfoHTML()
{
    String out = "<h3>Firmware Versions in Partitions</h3><pre>";

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL)
    {
        const esp_partition_t* part = esp_partition_get(it);
        esp_app_desc_t desc;
        bool hasInfo = esp_ota_get_partition_description(part, &desc) == ESP_OK;

        out += String(part->label) + " @ 0x" + String(part->address, HEX);
        out += " size 0x" + String(part->size, HEX);

        if (part == running) out += " [RUNNING]";
        if (part == boot)    out += " [BOOT]";

        if (hasInfo)
        {
            out += "\n  Version: ";
            out += html_escape(desc.version);
            out += "\n  Project: ";
            out += html_escape(desc.project_name);
            out += "\n  Built: ";
            out += String(desc.date) + " " + desc.time;
        }
        else
        {
            out += "\n  <i>No descriptor info</i>";
        }

        out += "\n\n";
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    out += "</pre>";
    return out;
}


void http_root()
{
    String page = "<html><body>";

    page += http_get_gps();

    page += "<hr><br>\n";

    page += getPartitionInfoHTML();

    page += "<hr><br>\n";
    page += "<a href='/dir'>List Files</a><br>\n";
    page += "<a href='/nvs'>List NVS Parameters</a><br><br>\n";
    page += "<a href='/update'>Update Firmware</a><br>\n";
    //page += "<form method='POST' action='/reboot'><input type='submit' value='Reboot'></form>";
    page += "<a href='/reboot'>Reboot</a><br>\n";
    page += "</body></html>\n";
    server.send(200, "text/html", page);
}


void http_reboot()
{
    server.send(200, "text/plain", "Rebooting...");
    delay(1000);
    ESP.restart();
}


// Show LittleFS directory listing.
String http_dir_list( fs::FS &fs, const char *dirname, uint8_t levels = 3 )
{
    String out = "Directory: ";
    out += dirname;
    if( strcmp( dirname, "/" ) )
        out += "/";

    out += "\n";

    File root = fs.open( dirname );

    if( !root || !root.isDirectory() )
    {
        out += " - failed to open directory\n";
        return out;
    }

    File file = root.openNextFile();
    while( file )
    {
        if( !file.isDirectory() )
        {
            out += "  ";
            out += dirname;
            if( strcmp( dirname, "/" ) )
                out += "/";
            out += file.name();
            out += "   ";
            out += file.size();
            out += " bytes";
            out += "\n";

        }

        file = root.openNextFile();
    }
 
    out += "\n";

    // Now list subdirectories
    bool any_dirs_listed = false;
    root.rewindDirectory();
    file = root.openNextFile();
    while( file )
    {
        if (file.isDirectory())
        {
            //out += "  [DIR]   ";
            //out += dirname;
            //if( strcmp( dirname, "/" ) )
            //    out += "/";
            //out += file.name();
            //out += "/\n";

            if (levels)
            {
                out += http_dir_list( fs, file.path(), levels - 1 );
                any_dirs_listed = true;
            }
        }

        file = root.openNextFile();
    }

    if( any_dirs_listed )
        out += "\n";

    return out;
}


void http_dir()
{
    char buf[16];
    String out = "<html><body>\n";
    
    out += "<h3>LittleFS directory listing:</h3><hr>\n<pre>";

    if (!littlefs_mounted) {
        out += "LittleFS not mounted.</pre>";
        server.send( 200, "text/html", out );
        return;
    }

    // Start from the root directory.
    out += http_dir_list( FSLINK, "/" );

    out += "<br><hr>";

    // Filesystem space stats:
    size_t total = FSLINK.totalBytes();
    size_t used = FSLINK.usedBytes();

    out += "<pre>Total bytes: ";
    out += total;
    out += "\nUsed bytes:  ";
    out += used;

    // Calculate used space %
    if( total < 1 )     // Avoid divide by 0 if no filesystem.
        total = 1;

    snprintf( buf, sizeof( buf ), "   (%0.1f%%)", ( (float) used / (float) total ) * 100.0 );
    out += buf;

    out += "\nFree bytes:  ";
    out += ( total - used );

    snprintf( buf, sizeof( buf ), "   (%0.1f%%)", ( (float) ( total - used ) / ( float) total ) * 100.0 );
    out += buf;

    out += "\n</pre>";

    server.send( 200, "text/html", out );
}


// Show NVS key/value config contents.
void http_nvs()
{
    String out = "<html><body>\n";

    server.send( 200, "text/plain", "FIXME..." );
}


int http_setup()
{
    server.on( "/", http_root );
    server.on( "/reboot", http_reboot );
    server.on( "/dir", http_dir );
    server.on( "/nvs", http_nvs );
    ElegantOTA.begin( &server );
    server.begin();

    return 0;
}


int http_loop()
{
    server.handleClient();
    return 0;
}
