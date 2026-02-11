#include "lut.h"
#include <LittleFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "printManager.h"

int* pLUT = NULL;
int lutSize = 0;
int currentLUTIndex = -1; //-1 means no LUT loaded

static SemaphoreHandle_t lut_mutex = NULL;

void lutMutexInit(void) {
    lut_mutex = xSemaphoreCreateMutex();
}

static inline bool lut_lock(void) {
    return lut_mutex && xSemaphoreTake(lut_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}
static inline void lut_unlock(void) {
    if (lut_mutex) xSemaphoreGive(lut_mutex);
}

//Try to open LUT and check number of elements
//Returns number of elements
//0 = Failed to count elements
//-1 = LUT does not exists
int checkLut(uint8_t index)
{
    if (!lut_lock()) return -1;

    //Try to open the file
    String filename = String("/LUT_") + String(index) + ".csv";
    File file = LittleFS.open(filename.c_str(), FILE_READ);
    if (!file)
    {
        lut_unlock();
        printfnl(SOURCE_BASIC,"LUT %d does not exists\n", index);
        return -1; //LUT does not exists
    }
    int count = 0;
    char c;
    while (file.available())
    {
        c = file.read();
        if (c == ',')
            count++;
    }
    file.close();
    if (count == 0)
    {
        lut_unlock();
        printfnl(SOURCE_BASIC,"LUT %d is empty\n", index);
        return 0; //LUT is empty
    }
    lut_unlock();
    return count + 1; //Return number of elements
}

int loadLut(uint8_t index)
{
    if (!lut_lock()) return 0;

    //Check if we already have a LUT loaded
    if (currentLUTIndex == index)
    {
        int sz = lutSize;
        lut_unlock();
        return sz; //Return size of current LUT
    }

    lut_unlock();  // release before calling checkLut (which takes lock itself)

    //Check if the LUT exists and get the size
    int size = checkLut(index);
    if (size < 0)
        return 0; //LUT does not exists or is empty

    if (!lut_lock()) return 0;

    //Allocate memory for the LUT
    if (pLUT != NULL)
        free(pLUT); //Free previous LUT memory

    pLUT = (int*)calloc(size, sizeof(int));
    if (pLUT == NULL)
    {
        lut_unlock();
        return 0; //Memory allocation failed
    }

    //Open the file and read the values into the LUT
    String filename = String("/LUT_") + String(index) + ".csv";
    File file = LittleFS.open(filename.c_str(), FILE_READ);
    if (!file)
    {
        free(pLUT);
        pLUT = NULL;
        lut_unlock();
        return 0; //Failed to open file
    }

    int i = 0;
    String value;
    while (file.available())
    {
        char c = file.read();
        if (c == ',')
        {
            pLUT[i++] = value.toInt();
            value = ""; //Reset value for next read
        }
        else
        {
            value += c; //Append character to value
        }
    }

    //Read last value (after last comma)
    if (value.length() > 0 && i < size)
        pLUT[i++] = value.toInt();

    file.close();

    lutSize = i; //Set the size of the LUT
    currentLUTIndex = index; //Set current LUT index
    lut_unlock();
    return lutSize; //Return size of loaded LUT
}

int saveLut(uint8_t index)
{
    if (!lut_lock()) return 0;

    //Check if we have a LUT loaded
    if (pLUT == NULL || lutSize <= 0)
    {
        lut_unlock();
        return 0; //No LUT to save
    }

    //Open the file for writing
    String filename = String("/LUT_") + String(index) + ".csv";
    File file = LittleFS.open(filename.c_str(), FILE_WRITE);
    if (!file)
    {
        lut_unlock();
        return 0; //Failed to open file
    }

    //Write the values to the file
    for (int i = 0; i < lutSize; i++)
    {
        file.print(pLUT[i]);
        if (i < lutSize - 1)
            file.print(","); //Add comma between values
    }

    file.close();
    lut_unlock();
    return 1; //Success
}

void lutReset(void)
{
    if (!lut_lock()) return;
    if (pLUT != NULL)
    {
        free(pLUT);
        pLUT = NULL;
    }
    lutSize = 0;
    currentLUTIndex = -1;
    lut_unlock();
}
