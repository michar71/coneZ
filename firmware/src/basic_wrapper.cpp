#include "basic_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "printManager.h"
#include "FS.h"
#include <LittleFS.h>
#define FSLINK LittleFS
#define REAL_ESP32_HW
#include "basic.h"
#include "gps.h"
#include "sensors.h"

#define MAX_PARAMS 16


TaskHandle_t basic_task;
SemaphoreHandle_t basic_mutex;
char next_code[256] = {0};
int params[MAX_PARAMS];

void set_basic_param(uint8_t paramID, int val)
{
  if (paramID > MAX_PARAMS-1)
     paramID = MAX_PARAMS-1;

  //This is probably atomic....   
  params[paramID] = val;
}

int get_basic_param(int paramID)
{
      if (paramID > MAX_PARAMS-1)
     paramID = MAX_PARAMS-1;

  //This is probably atomic....   
  return params[paramID];
}

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
        //Calulate day of week

        //Calulate day of year

        //calulate leap year
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
    if (imuAvaialble())
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
        vTaskDelay(5 / portTICK_PERIOD_MS);
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
            long start = millis();
            long deadline = start + duration_ms;
            while (millis() < deadline)
            {
                vTaskDelay(1 / portTICK_PERIOD_MS);
                inc_thread_count(xPortGetCoreID());
                if (timeout_ms > 0 && (millis() - start) > timeout_ms)
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
            bool pps = get_pps();
            bool last_pps = pps;
            long t = millis();
            if (condition == CONDITON_LOW_TO_HIGH)
            {
                do{
                    last_pps = pps;
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    inc_thread_count(xPortGetCoreID());
                    pps = get_pps();
                    if (timeout_ms > 0 && (millis() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (!((last_pps == false) && (pps == true))); //Wait for PPS to go high
                return 1; //PPS Pulse received
            }
            else if (condition == CONDITON_HIGH_TO_LOW)
            {
                do{
                    last_pps = pps;
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    inc_thread_count(xPortGetCoreID());
                    pps = get_pps();
                    if (timeout_ms > 0 && (millis() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (!((last_pps == true) && (pps == false))); //Wait for PPS to go low
                return 1; //PPS Pulse received
            }
            else
            {
                return -1; //Condition not supported
            }
            break;
        }
        case EVENT_PARAM:
        {
            long t = millis();
            if (condition == CONDITON_LARGER)
            {
                do{
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (millis() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (get_basic_param(sourceID) <= triggerValue);
                return 1;
            }
            if (condition == CONDITON_SMALLER)
            {
                do{
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (millis() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (get_basic_param(sourceID) >= triggerValue);
                return 1;
            }
            else if (condition == CONDITON_EQUAL)
            {
                do{
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (millis() - t) > timeout_ms)
                    {
                        return 0; //Timeout
                    }
                } while (get_basic_param(sourceID) != triggerValue);
                return 1;
            }
            else if (condition == CONDITON_NOT_EQUAL)
            {
                do{
                    vTaskDelay(1 / portTICK_PERIOD_MS);
                    inc_thread_count(xPortGetCoreID());
                    if (timeout_ms > 0 && (millis() - t) > timeout_ms)
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

    //Start Own Thread
    basic_mutex = xSemaphoreCreateMutex();    
    xTaskCreatePinnedToCore(basic_task_fun, "BasicTask", 65535, NULL, 1, &basic_task, 0);
    //xTaskCreate(basic_task_fun, "BasicTask", 65535, NULL, 128, NULL);
}

