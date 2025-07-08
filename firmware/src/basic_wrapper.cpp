#include "basic_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "FS.h"
#include <LittleFS.h>
#define FSLINK LittleFS
#define REAL_ESP32_HW
#include "basic.h"

Stream* BOutputStream = NULL;

TaskHandle_t basic_task;
SemaphoreHandle_t basic_mutex;
char next_code[256] = {0};

void basic_task_fun( void * parameter )
{
    for(;;)
    {
        if (xSemaphoreTake(basic_mutex, portMAX_DELAY) == pdTRUE) 
        {
            if (next_code[0] != 0)
            {
                //Execute program
                BOutputStream->print("RUNNING ");
                BOutputStream->print(next_code);
                BOutputStream->print(" ON CORE ");
                BOutputStream->println(xPortGetCoreID());
                initbasic(BOutputStream,1);      
                int res = interp(next_code);
                if (res != 0)
                {
                    BOutputStream->print("Error Exit Code: ");
                    BOutputStream->println(res);
                }   
                else 
                {
                    BOutputStream->println("DONE");
                }
                //Reset Exec Code
                next_code[0] = 0;
            }
        }
        xSemaphoreGive(basic_mutex);
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

void setup_basic()
{
    //Set Callback Functions

    //Start Own Thread
   basic_mutex = xSemaphoreCreateMutex();    
   xTaskCreatePinnedToCore(basic_task_fun, "BasicTask", 10000, NULL, 2, &basic_task, 1);
   //xTaskCreate(basic_task_fun, "BasicTask", 10000, NULL, 2, NULL);
}

