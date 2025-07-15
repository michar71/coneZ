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


void http_dir()
{
    server.send( 200, "text/plain", "FIXME..." );
}


void http_nvs()
{
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
