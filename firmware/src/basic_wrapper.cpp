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
                //Execute program
                printfnl(SOURCE_BASIC,"Running: %s on Core:%d\n",next_code,xPortGetCoreID());
                reset_params();
                initbasic(1);      
                int res = interp(next_code);
                if (res != 0)
                {
                    printfnl(SOURCE_BASIC,"Error Exit Code: %d\n",res);
                }   
                else 
                {
                    printfnl(SOURCE_BASIC,"DONE\n");
                }
                //Reset Exec Code
                next_code[0] = 0;
            }
            xSemaphoreGive(basic_mutex);
        }  
        
    }
}

bool set_basic_program(char* prog)
{
    if (xSemaphoreTake(basic_mutex, 1000) == pdTRUE) 
    {
        strcpy(next_code,prog);
        xSemaphoreGive(basic_mutex);
        return true;
    }
    return false;
}


void setup_basic()
{
    //Set Callback Functions
    register_param_callback(get_basic_param);

    //Start Own Thread
    basic_mutex = xSemaphoreCreateMutex();    
    xTaskCreatePinnedToCore(basic_task_fun, "BasicTask", 65535, NULL, 1, &basic_task, 0);
    //xTaskCreate(basic_task_fun, "BasicTask", 65535, NULL, 128, NULL);
}

