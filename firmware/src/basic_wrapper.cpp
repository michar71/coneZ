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
SemaphoreHandle_t basic_Mutex;
char exec_code[256] = {0};
char next_code[256] = {0};

void basic_task_fun( void * parameter )
{
    BOutputStream->println("BASIC Task Created on core ");
    BOutputStream->println(xPortGetCoreID());
    for(;;)
    {
        if (exec_code[0] != 0)
        {
            //Execute program
            BOutputStream->print("RUNNING ");
            BOutputStream->println(exec_code);
            initbasic(BOutputStream,1);      
            int res = interp(exec_code);
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
            exec_code[0] = 0;
        }
        else
        {
            if (xSemaphoreTake(basic_Mutex, portMAX_DELAY) == pdTRUE) 
            {
                if (next_code[0] != 0)
                {
                    strcpy(exec_code,next_code);
                    next_code[0] = 0;
                }
            }
        } 
    }
}

void set_basic_program(Stream *output,char* prog)
{
    if (xSemaphoreTake(basic_Mutex, portMAX_DELAY) == pdTRUE) 
    {
        BOutputStream = output;
        strcpy(next_code,prog);
        xSemaphoreGive(basic_Mutex);
    }
}

void setup_basic()
{
    //Set Callback Functions

    //Start Own Thread
    xTaskCreatePinnedToCore(basic_task_fun, "BasicTask", 10000, NULL, 1, &basic_task, 0);
}

