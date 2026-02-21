#include "basic_wrapper.h"
#include <cstring>
#include "printManager.h"

#ifdef INCLUDE_WASM
#include "wasm_wrapper.h"
#endif

// ---------- Params (always available) ----------

#define MAX_PARAMS 16
volatile int params[MAX_PARAMS];

void set_basic_param(uint8_t paramID, int val)
{
  if (paramID > MAX_PARAMS-1)
     paramID = MAX_PARAMS-1;
  params[paramID] = val;
}

int get_basic_param(int paramID)
{
  if (paramID > MAX_PARAMS-1)
     paramID = MAX_PARAMS-1;
  return params[paramID];
}

// ---------- Script routing ----------

static bool has_extension(const char *path, const char *ext)
{
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    return strcasecmp(path + plen - elen, ext) == 0;
}

bool set_script_program(char *path)
{
#ifdef INCLUDE_WASM
    if (has_extension(path, ".wasm")) {
        return set_wasm_program(path);
    }
#endif

#ifdef INCLUDE_BASIC
    if (has_extension(path, ".bas")) {
        return set_basic_program(path);
    }
#endif

    printfnl(SOURCE_SYSTEM, "Unknown script type: %s\n", path);
    return false;
}


// ============================================================
// BASIC interpreter (guarded by INCLUDE_BASIC)
// ============================================================
#ifdef INCLUDE_BASIC

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#define BASIC_WRAPPER_TU
#define REAL_ESP32_HW
#include "basic.h"
#include "gps.h"
#include "sensors.h"


TaskHandle_t basic_task;
SemaphoreHandle_t basic_mutex;
char next_code[256] = {0};

int8_t getLocationData(float* org_lat, float* org_lon, float* lat, float* lon, float* alt, float* speed, float* dir)
{
    int8_t err = 0;

    if (get_gpsstatus())
    {
        *org_lat = get_org_lat();
        *org_lon = get_org_lon();
        *lat = get_lat();
        *lon = get_lon();
        *alt = get_alt();
        *speed = get_speed();
        *dir = get_dir();
        err = 1;
    }
    else
    {
        err = -1; //No GPS data available
    }
    return err;
}

int8_t getDateTimeData(bool *hasDate, bool *hasTime, int *day, int *month, int *year, int *hour, int *minute, int *second, int* dayOfWeek, int *dayOfYear, bool *isLeapYear)
{
    int8_t err = 0;

    if (get_gpsstatus())
    {
        *hasDate = true;
        *hasTime = true;
        *day = get_day();
        *month = get_month();
        *year = get_year(); 
        *hour = get_hour();
        *minute = get_minute();
        *second = get_second();
        //Calculate day of week

        //Calculate day of year

        //Calculate leap year
        *dayOfWeek = get_day_of_week();
        *dayOfYear = get_dayofyear();
        *isLeapYear = get_isleapyear();
    }
    else
    {
        *hasDate = false;
        *hasTime = false;
        *day = 0;
        *month = 0;
        *year = 0;
        *hour = 0;
        *minute = 0;
        *second = 0;
        *dayOfWeek = 0;
        *dayOfYear = 0;
        *isLeapYear = false;
        err=0;
    }
    return err;
}

int8_t getIMUdata(float* roll,float* pitch,float* yaw,float* accX,float* accY,float* accZ)
{
    if (imuAvailable())
    {
        *roll = getRoll();
        *pitch = getPitch();
        *yaw = getYaw();
        *accX = getAccX();
        *accY = getAccY();
        *accZ = getAccZ();
        return GYRO_BIT | ACC_BIT; //IMU data available but only Gyro/Accelerometer data
    }
    else
    {
        *roll = 0;
        *pitch = 0;
        *yaw = 0;
        *accX = 0;
        *accY = 0;
        *accZ = 0;
        return 0; //IMU data not available
    }
}


int8_t getENVdata(float* temp, float* humidity, float* brightness)
{
    *temp = getTemp();
    *humidity = -1;
    *brightness = -1;
    return 1; //Data available
}

void reset_params(void)
{
    for (int ii=0;ii<MAX_PARAMS;ii++)
        params[ii] = 0;
}

void basic_task_fun( void * parameter )
{
    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
        inc_thread_count(xPortGetCoreID());
        if (xSemaphoreTake(basic_mutex, portMAX_DELAY) == pdTRUE)
        {
            if (next_code[0] != 0)
            {
                //Copy program path to local buffer and release mutex
                char local_code[256];
                strncpy(local_code, next_code, sizeof(local_code));
                local_code[sizeof(local_code) - 1] = '\0';
                next_code[0] = 0;
                xSemaphoreGive(basic_mutex);

                //Execute program
                printfnl(SOURCE_BASIC,"Running: %s on Core:%d\n",local_code,xPortGetCoreID());
                reset_params();
                initbasic(1);
                int res = interp(local_code);
                if (res != 0)
                {
                    printfnl(SOURCE_BASIC,"Error Exit Code: %d\n",res);
                }
                else
                {
                    printfnl(SOURCE_BASIC,"DONE\n");
                }
            }
            else
            {
                xSemaphoreGive(basic_mutex);
            }
        }  
        
    }
}

bool set_basic_program(char* prog)
{
    if (xSemaphoreTake(basic_mutex, 1000) == pdTRUE)
    {
        strncpy(next_code, prog, sizeof(next_code) - 1);
        next_code[sizeof(next_code) - 1] = '\0';
        xSemaphoreGive(basic_mutex);

        // Create task on first use
        if (basic_task == NULL)
            xTaskCreatePinnedToCore(basic_task_fun, "BasicTask", 16384, NULL, 1, &basic_task, tskNO_AFFINITY);

        return true;
    }
    return false;
}

int8_t getSyncEvent(int event, int sourceID, int condition, int triggerValue, int timeout_ms)
{
    switch (event)
    {
        case EVENT_SYNC_PULSE:
        {
            return -1; //Event not supported
            //Need to add support here to wait for a specific sync pule via LoRa.
            //Couyld be we want to repsond to all of them or a specific one.
        }
        case EVENT_DIGITAL_PIN:
        {
            return -1; //Event not supported
        }
        case EVENT_ANALOG_PIN:
        {
            return -1; //Event not supported
        }
        case EVENT_SYS_TIMER:
        {
            //For Sys-Timer the condition options are different.
            //First we take the trigger value and convert it to a duration in milliseconds,
            long duration_ms = triggerValue;
            switch (condition)
            {
                case CONDITON_HOUR:
                    duration_ms *= 3600000; //Convert to ms
                    break;
                case CONDITON_MINUTE:
                    duration_ms *= 60000; //Convert to ms
                    break;
                case CONDITON_SECOND:
                    duration_ms *= 1000; //Convert to ms
                    break;
                case CONDITON_MS:
                    //Already in ms
                    break;
                default:
                    break;
            }
            long start = uptime_ms();
            long deadline = start + duration_ms;
            while (uptime_ms() < deadline)
            {
                vTaskDelay(pdMS_TO_TICKS(1));
                inc_thread_count(xPortGetCoreID());
                if (timeout_ms > 0 && (uptime_ms() - start) > timeout_ms)
                {
                    return 0; //Timeout
                }
            }
            return 1; //Event received
        }            
        case EVENT_GPS_PPS:
        {
            //First chekc if GPS has lock
            if (!get_gpsstatus())
            {
                return -1; //No GPS Signal
            }
            //Now we can wait for the PPS Pulse
            long t = uptime_ms();
            if (condition == CONDITON_LOW_TO_HIGH)
            {
                // Use ISR flag for sub-ms rising edge detection
                get_pps_flag();  // clear any stale flag
                do{
                    vTaskDelay(pdMS_TO_TICKS(1));
                    inc_thread_count(xPortGetCoreID());
                    if (get_pps_flag())
                    {
                        return 1; //PPS rising edge received
                    }
                    if (timeout_ms > 0 && (uptime_ms() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (true);
            }
            else if (condition == CONDITON_HIGH_TO_LOW)
            {
                // Wait for rising edge via ISR flag, then poll for falling edge
                get_pps_flag();  // clear any stale flag
                // First wait for rising edge
                while (!get_pps_flag()) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (uptime_ms() - t) > timeout_ms)
                        return 0;
                }
                // Now poll for falling edge
                while (get_pps()) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (uptime_ms() - t) > timeout_ms)
                        return 0;
                }
                return 1; //PPS falling edge received
            }
            else
            {
                return -1; //Condition not supported
            }
            break;
        }
        case EVENT_PARAM:
        {
            long t = uptime_ms();
            if (condition == CONDITON_LARGER)
            {
                do{
                    vTaskDelay(pdMS_TO_TICKS(1));
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (uptime_ms() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (get_basic_param(sourceID) <= triggerValue);
                return 1;
            }
            if (condition == CONDITON_SMALLER)
            {
                do{
                    vTaskDelay(pdMS_TO_TICKS(1));
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (uptime_ms() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (get_basic_param(sourceID) >= triggerValue);
                return 1;
            }
            else if (condition == CONDITON_EQUAL)
            {
                do{
                    vTaskDelay(pdMS_TO_TICKS(1));
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (uptime_ms() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (get_basic_param(sourceID) != triggerValue);
                return 1;
            }
            else if (condition == CONDITON_NOT_EQUAL)
            {
                do{
                    vTaskDelay(pdMS_TO_TICKS(1));
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (uptime_ms() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (get_basic_param(sourceID) == triggerValue);
                return 1;
            }    
            else
            {
                return -1; //Condition not supported
            }                 
        }
        default:
        {
            return -1; //Unknown event
        }
    }


    return 0;
}
void setup_basic()
{
    //Set Callback Functions
    register_location_callback(getLocationData);
    register_param_callback(get_basic_param);
    register_datetime_callback(getDateTimeData);
    register_imu_callback(getIMUdata);
    register_sync_callback(getSyncEvent);
    register_env_callback(getENVdata);

    //Create mutex (task created on first run)
    basic_mutex = xSemaphoreCreateMutex();
    //xTaskCreate(basic_task_fun, "BasicTask", 65535, NULL, 128, NULL);
}

#endif // INCLUDE_BASIC

