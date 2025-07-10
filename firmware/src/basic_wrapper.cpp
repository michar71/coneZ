#include "basic_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "FS.h"
#include <LittleFS.h>
#define FSLINK LittleFS
#define REAL_ESP32_HW
#include "basic.h"

#define MAX_PARAMS 16

Stream* BOutputStream = NULL;

TaskHandle_t basic_task;
SemaphoreHandle_t basic_mutex;
SemaphoreHandle_t terminal_mutex;
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
        esp_task_wdt_reset();
        if (xSemaphoreTake(basic_mutex, portMAX_DELAY) == pdTRUE) 
        {
            if (next_code[0] != 0)
            {
                //Execute program
                take_terminal();
                BOutputStream->print("RUNNING ");
                BOutputStream->print(next_code);
                BOutputStream->print(" ON CORE ");
                BOutputStream->println(xPortGetCoreID());
                give_terminal();
                reset_params();
                initbasic(BOutputStream,1);      
                int res = interp(next_code);
                if (res != 0)
                {
                    take_terminal();
                    BOutputStream->print("Error Exit Code: ");
                    BOutputStream->println(res);
                    give_terminal();
                }   
                else 
                {
                    take_terminal();
                    BOutputStream->println("DONE");
                    give_terminal();
                }
                //Reset Exec Code
                next_code[0] = 0;
            }
            xSemaphoreGive(basic_mutex);
        }  
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

bool set_basic_program(Stream *output,char* prog)
{
    if (xSemaphoreTake(basic_mutex, 1000) == pdTRUE) 
    {
        BOutputStream = output;
        strcpy(next_code,prog);
        xSemaphoreGive(basic_mutex);
        return true;
    }
    return false;
}

void take_terminal(void)
{
    xSemaphoreTake(terminal_mutex, portMAX_DELAY);
}

void give_terminal(void)
{
    xSemaphoreGive(terminal_mutex);
}

void setup_basic()
{
    //Set Callback Functions
    register_param_callback(get_basic_param);

    //Start Own Thread
   basic_mutex = xSemaphoreCreateMutex();    
   terminal_mutex = xSemaphoreCreateMutex();    
   //xTaskCreatePinnedToCore(basic_task_fun, "BasicTask", 65535, NULL, 128, &basic_task, 1);
   xTaskCreate(basic_task_fun, "BasicTask", 65535, NULL, 128, NULL);
}

