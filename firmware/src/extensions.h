#ifndef extensions_h
#define extensions_h

#include "main.h"
#include <math.h>

#define VERSION 1

// LED buffers
extern CRGB leds1[NUM_LEDS1];
extern CRGB leds2[NUM_LEDS2];
extern CRGB leds3[NUM_LEDS3];
extern CRGB leds4[NUM_LEDS4];

/*
Functions:
=========

General:
DONE int PRINTS(char *msg) -> Print a string to the console  
DONE uint8 LIMIT256(int val) -> Limits a valure oy 0..255
DONE int LIMIT(int val, int min, int max) -> Limits a valuer to given min/max
DONE int ABS(int) -> Absoluter of value
DONE int SIN256(int) -> Returns the sine of a value in 0..255 (0=0, 64=90, 128=180, 192=270, 255=360)
DONE int SCALE(int val, int valmin, int valmax, int rmin, int rmax) -> Scales a value from one range to another
DONE int GAMMA256(int val) -> Same lookup based Gamma function  we apply before setting the LEDS
DONE int USEGAMMA(int enable) -> Automatically apply Gamma correction to all LED values 0 = Disable, 1 = Enable

System:
DONE int TIMESTAMP(int divider)
DONE WAIT (int ms)
LOCKTIME()        -> Stores the current time 
WAITIME(ms)       -> x mS minus the time that has passed since LOCKTIME was called. This allows to have consistent timing/delays even if scripts take some (even variable) time to execute for each frame.
DONE int RANDOM(int min, int max)
DONE int VERSION()  -> Retrun basic version number
int FLAG(int ID) -> Check one of up to 8 flags from the host-process. Can be a number or 1/0 for true/false.
int WAITFOR(int event , int cond, int val, int timeout)

event = 0 -> Sync Pulse
    cond = 0 = all sync pulses, 1 = specific sync pulse ID
    val = 0 for all sync pulses or number ID of sync pulse to wait for
    timeout -> Timeout in ms, 0 = wait forever
    ret = 0 = timerout, 1 = event received    

event = 1 -> Digital Pin Change
    cond = 0 = low to high, 1 = high to low
    val = pin number
    timeout = timeout in ms, 0 = wait forever

    ret = 0 = timerout, 1 = event received    

event = 2 -> Analog Pin Change from smaller then TH to larger then TH
    cond = Threshold
    val = pin number
    timeout = timeout in ms, 0 = wait forever   

    ret = 0 = timerout, 1 = event received

event = 3 -> Analog Pin Change from larger then TH to smaller then TH
    cond = Threshold
    val = pin number
    timeout = timeout in ms, 0 = wait forever   

    ret = 0 = timerout, 1 = event received

event = 4 -> wait for system timer to reach a specific value
    cond = 0 = millisec, 1 = sec, 2 = min, 3 = hours
    val = value to wait for
    timeout = timeout in ms, 0 = wait forever   

    ret = 0 = timerout, 1 = event received    

event = 5 -> wait for GPS PPS Pulse
    cond = 0 = low, 1 = high, 2 = transition from low to high, 3 = transition from high to low
    val = 0
    timeout -> Timeout in ms, 0 = wait forever
    ret = 0 = timerout, 1 = event received, -1 = no GPS Signal  

event = 0 -> wait for Param to reach a specific state
    cond = paramID
    val = Value we are waiting for/comparing too
    timeout -> Timeout in ms, 0 = wait forever
    ret = 0 = timerout, 1 = event received   

Hardware:
uint8 READANALOG(int ch)
uint8 READPIN(int gpio)
SETPIN(int gpio)
CLEARPIN(int gpio)

Color Space: 
DONE HSVTORGBARRAY(array_h, array_s, array_v) -> Convert HSV values in an array to RGB values in an array
DONE HSVTORGBARRAY(array_h, array_s, array_v) -> Convert HSV values in an array to RGB values in an array

LED Specific:
DONE SETLEDRGB(array R, array G, array B)   -> Upload Array to LED Strip. LED Length is defined by array size. Will init LED Strip on first call. (Usually set all to black...)
DONE SETLEDCOL(uint8_t r, uint8_t g, uint8_t b) -> Set the whole LED Strip to one solid color.
DONE int GETMAXLED() -> Get the number of LEDs in Strip

Array:
DONE SHIFTARRAY(array A, int amount, int val) -> Shift the values of an array to left or right by a specific amount setting the new pixels to val
DONE ROTATEARRAY(array A, int amount)
DONE COPYARRAY(array A, array B) -> Copy the content of array A to array B. If B is smaller then A it will be truncated, if larger it will be padded with 0.
DONE RGBTOHSVARRAY(array R, array G, array B) -> Convert RGB values in an array to HSV values in an array
DONE HSVTORGBARRAY(array H, array S, array V) -> Convert HSV values in an array to RGB values in an array
DONE SETARRAY(array A, int start,int end, int value) -> Set the value of an array at a specific index
DONE SCALELIMITARRAY(array A, int perc,int min, int max) -> Scale the values of an array by a percentage but kimit it to max 255, min 0

LUTs:
There is 1 LUT in thre system. (Fast Acceess, dynamically allocated/released memory). But LUTs can be copied to/from arrays (Which are slow and eat up basic ressources...).

DONE int LOADLUT(index) -> Loads a lut form filesystem. (LUT_[index].csv so for example LUT_1.csv for index 1). Returns the number of entries or zero if failed. 
DONE int SAVELUT(index) -> Saves the LUT to the filesystem. (LUT_[index].csv so for example LUT_1.csv for index 1). Returns 1 on success, 0 on failure
DONE int LUTSIZE(index) -> Returns the size of the LUT in entries or 0 if it does not exist. Actually openes the file and reades it byte by byte to find commas unless its the current index lut already loaded so this can be used to DIM an array.
DONE int LUTTOARRAY(array) -> Copies LUT to array. If LUT is larger then array it gets truncated, if bigger its filled with zeros. Returns numbers of entries.
DONE int ARRAYTOLUT(array) -> Copies array to LUT. 
DONE int LUT(int)  -> Returns the value of the LUT at index. If no LUT is loaded it will return 0. If the index is larger then the LUT size it will return 0.

Location Based Functions. All Distances in meters, angles in Degrees:
DONE int HASORIGIN()   //Origin Data is avaliable
DONE int HASGPS() //Spped/Dir are avialable
DONE int ORIGINDIST()
DONE int ORIGINANGLE()
DONE int GPSSPEED()   //in m/s
DONE int GPSDIR()     //Angle in deg
DONE int GPSALT()     //Altitude in m
DONE int DIST(int X1, int Y1, int X2, int Y2)
DONE int ANGLE(int X1, int Y1, int X2, int Y2)

IMU based Functions:
DONE int HASGYRO() //IMU Gyro Data is avaliable
DONE int HASACC() //IMU Acc Data is avaliable
DONE int HASMAG()  //IMU Mag data is avialable
DONE int PITCH() //Pitch in degrees
DONE int ROLL() //Roll in degrees
DONE int YAW() //Yaw in degrees
DONE int ACCX() //Accelerometer X in m/s^2
DONE int ACCY() //Accelerometer Y in m/s^2
DONE int ACCZ() //Accelerometer Z in m/s^2

environmental-sensor functions:
DONE int TEMP() //returns temperature in Deg C*10 or -10000 if no temp sensor present 
DONE int HUM() //returns humidity in % or -1 if no humidity sensor present
DONE int LIGHT() //returns brightness beteen 0-4096 or -1 cif brightness senor does not exist

*/
//Function-Callbacks

//Location function [float pointers to return org_lat,org_long,lat,long,alt,speed,dir]
//Return Values:
//-2 = Data Format error
//-1 = No Sat Link
//0 = No Valid Data
// 1= Valid data
typedef int8_t (*CallbackLocationFunction)(float*,float*,float*,float*,float*,float*,float*);
CallbackLocationFunction Loc_Func = NULL;


//IMU Function [float pointer to return Roll, Pitch, Yaw, AccX, AccY, AccZ]
//It is assumed that the IMU processing uses its own sensor fusion to determine 
//best possible solution for R/P/Y including filtering and all the other fasncy processing solutions.
//Return Values:
//-1= IMU Comm error
//0 = No valid Data
//>0 Valid Data
//The lower 3 bit determine what features are available:
//Bit 0 = Gyro
//Bit 1 = Acc
//Bit 2 = Mag
//So a return value of 5 means Gyro and Mag datas are available 
typedef int8_t (*CallbackIMUFunction)(float*,float*,float*,float*,float*,float*);
CallbackIMUFunction IMU_Func = NULL;
#define GYRO_BIT 0x01
#define ACC_BIT 0x02
#define MAG_BIT 0x04

//ENV Function [float pointers to return Temp [Deg C] ,Hum [%], Brightness [0...4096]
//Return Values:
//0 = No valid Data
//>0 Valid Data
//Return -10000 if no temp sensor
//Return -1 if no Humidity Sensor
//Return -1 if no brightness sensor
typedef int8_t (*CallbackENVFunction)(float*,float*,float*);
CallbackENVFunction ENV_Func = NULL;

//SYNC_function (int pulseID, int timeout_ms)
//This function is called to wait for sync pulses.
// pulseID = 0 for all sync pulses, 1 for sync pulse 1, 2 for sync pulse 2 etc.
// timeout_ms = 0 for no timeout, >0 for timeout in ms
//Return Values:
//0 = Timeout
//1 = Sync Pulse received
typedef int8_t (*CallbackSYNCFunction)(int,int);
CallbackSYNCFunction SYNC_Func = NULL;
#define EVENT_PULSE 0
#define EVENT_DIGITAL 1
#define EVENT_ANALOG_UP 2
#define EVENT_ANALOG_DOWN 3
#define EVENT_TIMER 4

//int PARAM_function (int paramID)
//Returns the value of the parameter paramID. Return 0 if parasmID does not exist...
//paramID 0 should be used to termiante a program if set to 1 if its in a loop.
typedef int (*CallbackPARAMFunction)(int);
CallbackPARAMFunction PARAM_Func = NULL;

//Gamma-Table LUT
const uint8_t  gamma8[] = {
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2,
2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5,
5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10,
10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
90, 92, 93, 95, 96, 98, 99, 101, 102, 104, 105, 107, 109, 110, 112, 114,
115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138, 140, 142,
144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169, 171, 173, 175,
177, 180, 182, 184, 186, 189, 191, 193, 196, 198, 200, 203, 205, 208, 210, 213,
215, 218, 220, 223, 225, 228, 231, 233, 236, 239, 241, 244, 247, 249, 252, 255
};

//------------------------------------
//USER DEFINED FUNCTIONS
//------------------------------------
#define PRINTS_T "PRINTS"

#define LIMIT256_T "LIMIT256"
#define LIMIT_T "LIMIT"
#define SCALE_T "SCALE"
#define SIN256_T "SIN256"
#define GAMMA256_T "GAMMA256"
#define USEGAMMA_T "USEGAMMA"


#define ABS_T "ABS"
#define WAIT_T "WAIT"
#define RANDOM_T "RANDOM"
#define TIMESTAMP_T "TIMESTAMP"
#define GETMAXLED_T "GETMAXLED"
#define SETLEDRGB_T "SETLEDRGB"
#define SETLEDCOL_T "SETLEDCOL"

#define SETARRAY_T "SETARRAY"
#define SHIFTARRAY_T "SHIFTARRAY"
#define ROTATEARRAY_T "ROTATEARRAY"
#define COPYARRAY_T "COPYARRAY"
#define SCALELIMITARRAY_T "SCALELIMITARRAY"
#define RGBTOHSVARRAY_T "RGBTOHSVARRAY"
#define HSVTORGBARRAY_T "HSVTORGBARRAY"
#define LOADLUT_T "LOADLUT"
#define SAVELUT_T "SAVELUT"
#define LUTSIZE_T "LUTSIZE"
#define LUTTOARRAY_T "LUTTOARRAY"
#define ARRAYTOLUT_T "ARRAYTOLUT"
#define LUT_T "LUT"

#define HASORIGIN_T "HASORIGIN"
#define HASGPS_T "HASGPS"
#define ORIGINDIST_T "ORIGINDIST"
#define ORIGINANGLE_T "ORIGINANGLE"
#define GPSSPEED_T "GPSSPEED"
#define GPSDIR_T "GPSDIR"
#define DIST_T "DIST"
#define ANGLE_T "ANGLE"

#define HASGYRO_T "HASGYRO"
#define HASACC_T "HASACC"
#define HASMAG_T "HASMAG"
#define PITCH_T "PITCH"
#define ROLL_T "ROLL"
#define YAW_T "YAW"
#define ACCX_T "ACCX"
#define ACCY_T "ACCY"
#define ACCZ_T "ACCZ"

#define TEMP_T "TEMP"
#define HUM_T "HUM"
#define BRIGHT_T "BRIGHT"

#define VERSION_T "VERSION"
#define WAITFOR_T "WAITFOR"
#define GETPARAM_T "GETPARAM"

//-------------------------------------
//Real HW dependecies.... We can ifdef this with stubs or PC functions for testing on other platform
//-------------------------------------
#ifdef REAL_ESP32_HW

#include "FastLED.h"

int getNumLeds()
{
    return NUM_LEDS1; //Return the number of LEDs in the strip
}

void setLEDr(int pos, int val)
{
    leds1[pos].r = (uint8_t)val;
}

void setLEDg(int pos, int val)
{
    leds1[pos].g = (uint8_t)val;
}

void setLEDb(int pos, int val)
{
    leds1[pos].b = (uint8_t)val;
}

void updateLEDs()
{
    FastLED.show(); //Update the LED strip with the new values
}

unsigned long getTimestamp()
{
    return millis(); //Return the current timestamp in milliseconds
}

#else
int getNumLeds()
{
    return 0; //Return the number of LEDs in the strip
}

void setLEDr(int pos, int val)
{
}

void setLEDg(int pos, int val)
{
}

void setLEDb(int pos, int val)
{
}

void updateLEDs()
{
    //Update the LED strip with the new values
}

unsigned long getTimestamp()
{
    return 0; //Return the current timestamp in milliseconds
}
#endif

//------------------------
//Location-based Functions
//------------------------

#define EARTH_RADIUS_METERS 6378137.0

// Converts latitude and longitude (in degrees) into x/y offsets (in meters)
// from the Equator and Prime Meridian.
// x = east-west offset (longitude), y = north-south offset (latitude)
void latlon_to_meters(float latitude_deg, float longitude_deg,
                      float *x_offset_meters, float *y_offset_meters) {
    // Convert latitude to radians for cosine calculation
    double lat_rad = latitude_deg * (M_PI / 180.0);

    // North-south offset from equator (meters)
    *y_offset_meters = EARTH_RADIUS_METERS * (M_PI / 180.0) * latitude_deg;

    // East-west offset from Prime Meridian (meters), adjusted by latitude
    *x_offset_meters = EARTH_RADIUS_METERS * cos(lat_rad) * (M_PI / 180.0) * longitude_deg;
}

// Structure to hold result
typedef struct {
    float distance;   // Distance in meters
    float bearing_deg;  // Bearing in degrees
} GeoResult;


//For large distsnces this is the correct formula
GeoResult calculate_geo(float lat1, float lon1, float lat2, float lon2) {
    GeoResult result;

    // Convert degrees to radians
    float lat1_rad = lat1 * DEG_TO_RAD;
    float lat2_rad = lat2 * DEG_TO_RAD;
    float delta_lat = (lat2 - lat1) * DEG_TO_RAD;
    float delta_lon = (lon2 - lon1) * DEG_TO_RAD;

    // Haversine formula for distance
    float a = sinf(delta_lat / 2.0f) * sinf(delta_lat / 2.0f) +
              cosf(lat1_rad) * cosf(lat2_rad) *
              sinf(delta_lon / 2.0f) * sinf(delta_lon / 2.0f);
    float c = 2.0f * atanf(sqrtf(a) / sqrtf(1.0f - a));
    result.distance = EARTH_RADIUS_METERS * c;

    // Formula for initial bearing
    float y = sinf(delta_lon) * cosf(lat2_rad);
    float x = cosf(lat1_rad) * sinf(lat2_rad) -
              sinf(lat1_rad) * cosf(lat2_rad) * cosf(delta_lon);
    float bearing_rad = atan2f(y, x);
    float bearing_deg = bearing_rad * RAD_TO_DEG;

    // Normalize to 0–360
    if (bearing_deg < 0.0f) {
        bearing_deg += 360.0f;
    }
    result.bearing_deg = bearing_deg;

    return result;
}

//For small distances or flat-earthers this is totally fine (Ignoring that earth is a sphere)
GeoResult xy_to_polar(float x1, float y1, float x2, float y2) 
{
    GeoResult result;

    float dx = x2 - x1;
    float dy = y2 - y1;

    result.distance = sqrtf(dx * dx + dy * dy);
    float angle_rad = atan2f(dy, dx);
    float angle_deg = angle_rad * (180.0f / 3.14159265f);

    // Normalize to 0–360 degrees
    if (angle_deg < 0.0f)
        angle_deg += 360.0f;

    result.bearing_deg = angle_deg;
    return result;
}
//-------------------------------------
//LUTs
//-------------------------------------
int* pLUT = NULL;
int lutSize = 0;
int currentLUTIndex = -1; //-1 means no LUT loaded

bool useGamma = false;

//Try to open LUT and check number of elements
//Returns number of elements
//0 = Failed to count elements
//-1 = LUT does not exists
int checkLut(uint8_t index)
{
    //Try to open the file
    String filename = String("/LUT_") + String(index) + ".csv";
    File file = FSLINK.open(filename.c_str(), FILE_READ);
    if (!file)
    {
        OS->printf("LUT %d does not exists\n", index);
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
        OS->printf("LUT %d is empty\n", index);
        return 0; //LUT is empty
    }
    return count + 1; //Return number of elements
}

int loadLut(uint8_t index)
{
    //Check if we already have a LUT loaded
    if (currentLUTIndex == index)
    {
        return lutSize; //Return size of current LUT
    }

    //Check if the LUT exists and get the size
    int size = checkLut(index);
    if (size < 0)
        return 0; //LUT does not exists or is empty

    //Allocate memory for the LUT
    if (pLUT != NULL)
        free(pLUT); //Free previous LUT memory

    pLUT = (int*)calloc(size, sizeof(int));
    if (pLUT == NULL)
    {
        return 0; //Memory allocation failed
    }

    //Open the file and read the values into the LUT
    String filename = String("/LUT_") + String(index) + ".csv";
    File file = FSLINK.open(filename.c_str(), FILE_READ);
    if (!file)
    {
        free(pLUT);
        pLUT = NULL;
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
    return lutSize; //Return size of loaded LUT
}
int saveLut(uint8_t index)
{
    //Check if we have a LUT loaded
    if (pLUT == NULL || lutSize <= 0)
    {
        return 0; //No LUT to save
    }

    //Open the file for writing
    String filename = String("/LUT_") + String(index) + ".csv";
    File file = FSLINK.open(filename.c_str(), FILE_WRITE);
    if (!file)
    {
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
    return 1; //Success
}   

int LUT_()
{
    int val = *sp;  //Pull value from Stack
    if (val<0) 
    {
        bad((char*)"LUT: NEGATIVE INDEX");
        return 0;
    }
    if (pLUT == NULL || currentLUTIndex < 0)
    {
        bad((char*)"LUT: NO LUT LOADED");
        return 0; //No LUT loaded
    }
    if (val >= lutSize)
    {
        bad((char*)"LUT: INDEX OUT OF BOUNDS");
        return 0; //Index out of bounds
    }
    *sp = pLUT[val]; //Push the value from the LUT to the stack
    STEP;
}


int LOADLUT_()
{
    int index = *sp;  //Pull value from Stack
    if (index<0) 
    {
        bad((char*)"LOADLUT: NEGATIVE INDEX");
        return 0;
    }
    int size = loadLut(index);
    if (size <= 0)
    {
        bad((char*)"LOADLUT: FAILED TO LOAD LUT");
        return 0; //Failed to load LUT
    }
    *sp = size; //Push the size of the LUT to the stack
    STEP;
}

int SAVELUT_()
{
    int index = *sp;  //Pull value from Stack
    if (index<0) 
    {
        bad((char*)"SAVELUT: NEGATIVE INDEX");
        return 0;
    }
    int result = saveLut(index);
    if (result <= 0)
    {
        bad((char*)"SAVELUT: FAILED TO SAVE LUT");
        return 0; //Failed to save LUT
    }
    *sp = result; //Push the result to the stack
    STEP;
}   

int LUTTOARRAY_()
{
    Val *arr = (Val*)*sp;  //Pull array from stack
    //Validate array
    if (arr == 0)
    {
        bad((char*)"LUTTOARRAY: BAD ARRAY POINTER");
        return 0;
    } 

    if (pLUT == NULL || currentLUTIndex < 0)
    {
        bad((char*)"LUTTOARRAY: NO LUT LOADED");
        return 0; //No LUT loaded
    }

    //Copy LUT to array
    int size = lutSize < arr[0] ? lutSize : arr[0]; //Limit to array size
    for (int ii=1; ii<=size; ii++)
    {
        arr[ii] = pLUT[ii-1]; //Copy LUT value to array
    }
    
    arr[0] = size; //Set the first element to the size of the LUT

    *sp = 0; //Push 0 to the stack
    STEP;    
}   

int ARRAYTOLUT_()
{
    Val *arr = (Val*)*sp;      //Pull array from stack
    //Validate array
    if (arr == 0)
    {
        bad((char*)"ARRAYTOLUT: BAD ARRAY POINTER");
        return 0;
    }

    if (pLUT != NULL)
        free(pLUT); 

    pLUT = (int*)calloc(arr[0], sizeof(int)); //Allocate memory for LUT
    if (pLUT == NULL)
    {
        bad((char*)"ARRAYTOLUT: MEMORY ALLOCATION FAILED");
        return 0; //Memory allocation failed
    }
    lutSize = arr[0]; //Set the size of the LUT
    currentLUTIndex = -1; //No index set yet
    
    //Copy array to LUT
    int size = arr[0] < lutSize ? arr[0] : lutSize; //Limit to LUT size
    for (int ii=1; ii<=size; ii++)
    {
        pLUT[ii-1] = arr[ii]; //Copy array value to LUT
    }

    *sp = 0; //Push 0 to the stack to indicate success
    STEP;    
}

int LUTSIZE_()
{
    int index = *sp;  //Pull value from Stack and rewind stack
    if (index<0) 
    {
        bad((char*)"LUTSIZE: NEGATIVE INDEX");
        return 0;
    }
    //If we have a LUT loaded just return the size of the current LUT
    if (pLUT != NULL && currentLUTIndex == index)
    {
        *sp = lutSize; //If we already have the LUT loaded, return its size
        STEP;
    }

    //If not check the LUT on FS
    int size = checkLut(index);
    if (size < 0)
    {
        bad((char*)"LUTSIZE: LUT DOES NOT EXISTS");
        return 0; //LUT does not exists
    }
    *sp = size; //Push the size of the LUT to the stack
    STEP;
}   

//------------------------------------
//General Commands
//------------------------------------


int PRINTS_() 
{ 
    //Output the string on the Run-time Stack and return for next command.
	OS->println((char*)*sp++); 
    STEP; 
}


//This looks for a keyword and adds the associated function pointer to the program queue.
int kwdhook_(char *msg) 
{
	if (!strcmp(msg,PRINTS_T))
    {
		expr(), emit(PRINTS_);STEP;
    }  
    else
		return 0;
}


int VERSION_() 
{ 
    *sp=VERSION; //Push back to to the stack
    STEP;
}

int ABS_() 
{ 
    int val = *sp;  //Pull value from Stack and rewind stack
    if (val<0) 
        val = -val; //Make it positive
    *sp=val; //Push back to to the stack
    STEP;
}

int WAIT_() 
{ 
    int val = *sp;  //Pull value from Stack and rewind stack
    if (val<0)
        val = 0;

    unsigned long start = getTimestamp();
    do
    {
        yield();    
    } 
    while (getTimestamp() < (start + val));
     
    *sp=0; //Push back to to the stack
    STEP;
}

int GETMAXLED_()
{
    *sp=getNumLeds(); //Push back to to the stack
    STEP;
}

int SETLEDRGB_()
{
    //Pull 3 arrays from stack
    Val *arr_b = (Val*)*sp++;  
    Val *arr_g = (Val*)*sp++;  
    Val *arr_r = (Val*)*sp;      

    //Validate arrays
    if ((arr_r == 0) || (arr_g == 0) || (arr_b == 0))
    {
        bad((char*)"SETLEDRGB: BAD ARRAY POINTER");
        return 0;
    } 

    if ((arr_r[0] != getNumLeds()) || (arr_g[0] != getNumLeds()) || (arr_b[0] != getNumLeds()))
    {
        bad((char*)"SETLEDRGB: WRONG ARRAY LENGTH");
        return 0;
    } 
    //copy arrays to LED array
    for (int ii=0;ii<getNumLeds();ii++)
    { 
        if (useGamma)
        {
            setLEDr(ii, gamma8[arr_r[ii+1]]);
            setLEDg(ii, gamma8[arr_g[ii+1]]);
            setLEDb(ii, gamma8[arr_b[ii+1]]);
        }
        else
        {
            setLEDr(ii, arr_r[ii+1]);
            setLEDg(ii, arr_g[ii+1]);
            setLEDb(ii, arr_b[ii+1]);       
        }                
    }
    //Show LED 
    updateLEDs();

    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;    
}

int RGBTOHSVARRAY_()
{
    //Pull 3 arrays from stack
    Val *arr_b = (Val*)*sp++;  
    Val *arr_g = (Val*)*sp++;  
    Val *arr_r = (Val*)*sp;      

    //Validate arrays
    if ((arr_r == 0) || (arr_g == 0) || (arr_b == 0))
    {
        bad((char*)"RGBTOHSVARRAY: BAD ARRAY POINTER");
        return 0;
    } 

    if ((arr_r[0] != arr_g[0]) || (arr_g[0] != arr_b[0]) || (arr_b[0] != arr_r[0]))
    {
        bad((char*)"RGBTOHSVARRAY: ARRAY LENGTH NOT MATCHING");
        return 0;
    } 


    //copy arrays to LED array
    CRGB rgb;
    CHSV hsv;
    for (int ii=1;ii<=arr_r[0];ii++)
    {
        rgb.r = arr_r[ii];
        rgb.g = arr_g[ii];
        rgb.b = arr_b[ii];

        hsv2rgb_rainbow( hsv, rgb);  //Convert RGB to HSV
        
        arr_r[ii] = hsv.h; //Store Hue
        arr_g[ii] = hsv.s; //Store Saturation
        arr_b[ii] = hsv.v; //Store Brightness             
    }

    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;    
}

int HSVTORGBARRAY_()
{
    //Pull 3 arrays from stack
    Val *arr_b = (Val*)*sp++;  
    Val *arr_g = (Val*)*sp++;  
    Val *arr_r = (Val*)*sp;      

    //Validate arrays
    if ((arr_r == 0) || (arr_g == 0) || (arr_b == 0))
    {
        bad((char*)"HSVTORGBARRAY: BAD ARRAY POINTER");
        return 0;
    } 

    if ((arr_r[0] != arr_g[0]) || (arr_g[0] != arr_b[0]) || (arr_b[0] != arr_r[0]))
    {
        bad((char*)"HSVTORGBARRAY: ARRAY LENGTH NOT MATCHING");
        return 0;
    } 


    //copy arrays to LED array
    CRGB rgb;
    CHSV hsv;
    for (int ii=1;ii<=arr_r[0];ii++)
    {
        hsv.h = arr_r[ii];
        hsv.s = arr_g[ii];
        hsv.v = arr_b[ii];

        rgb = rgb2hsv_approximate( hsv);  //Convert HSV to RGB
        
        arr_r[ii] = rgb.r; //Store Hue
        arr_g[ii] = rgb.g; //Store Saturation
        arr_b[ii] = rgb.b; //Store Brightness             
    }

    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;    
}

int SETLEDCOL_()
{
    //Pull 3 colors from stack
    int b = (int)*sp++;  
    int g = (int)*sp++;  
    int r = (int)*sp;      

    r = constrain(r,0,255); //Limit to 0..255
    g = constrain(g,0,255); //Limit to 0..255
    b = constrain(b,0,255); //Limit to 0..255

    //copy arrays to LED array
    for (int ii=0;ii<getNumLeds();ii++)
    {
        if (useGamma)
        {
            setLEDr(ii, gamma8[r]);
            setLEDg(ii, gamma8[g]);
            setLEDb(ii, gamma8[b]);
        }
        else
        {
            setLEDr(ii, r);
            setLEDg(ii, g);
            setLEDb(ii, b);
        }             
    }
    //Show LED 
    updateLEDs();

    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;    
}

//Hmm.. Do we scale from the whole 4 bit integer? Thats a huge number space.... 
//Or do we asume the numbers are limited to 2 bytes? (0..65535)
int SCALE_() 
{ 
    int rmax = (int)*sp++;    
    int rmin = (int)*sp++;
    int valmax = (int)*sp++;
    int valmin = (int)*sp++;    
    int val = *sp;  //Pull value from Stack
    int res = map(val,valmin, valmax,rmin,rmax);
    *sp=val; //Push back to the stack
    STEP;
}

int LIMIT256_() 
{ 
    int val = *sp;  //Pull value from Stack
    if (val<0)        //Limit it to 9..255
        val = 0; 
    if (val>255)
        val = 255; 
    *sp=val; //Push result to the stack
    STEP;
}

int SIN256_()
{
    int val = *sp;  //Pull value from Stack
    double sinval = sin(((double)val/255.0)* 2 * PI);
    double scaledval = ((sinval + 1) / 2) * 255;
    *sp=(int)scaledval; //Push result to the stack
    STEP; 
}

int GAMMA256_()
{
    int val = *sp;  //Pull value from Stack
    *sp=(int)gamma8[val]; //Push result to the stack
    STEP; 
}

int USEGAMMA_()
{
    int val = *sp;  //Pull value from Stack
    if (val == 0)
        useGamma = false;
    else
        useGamma = true;
    *sp= 0;
    STEP; 
}

int COPYARRAY_()
{
    //Pull 2 arrays from stack
    Val *arr_b = (Val*)*sp++;  
    Val *arr_a = (Val*)*sp;   
    
    int size_a = (int)arr_a[0];
    int size_b = (int)arr_b[0];

    //copy arrays to LED array
    for (int ii=1;ii<=size_a;ii++)
    {
        if (ii>size_b) //If we are larger then the target array we stop
            break;
        arr_b[ii] = arr_a[ii]; //Copy value from A to B
    }

    if (size_b > size_a) //If we are larger then the source array we fill the rest with 0
    {
        for (int ii=size_a+1;ii<=size_b;ii++)
        {
            arr_b[ii] = 0; //Fill with 0
        }
    }

    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;    
}

int ROTATEARRAY_()
{
    //Pull the values off the stack
    int amount = (int)*sp++;
    Val *arr = (Val*)*sp;  

    if (arr == 0)
    {
        bad((char*)"SETARRAY: BAD ARRAY POINTER");
        return 0;
    }

    int size = (int)arr[0];

    //We do this in an annoyingly slow way by just repeating it n times
    //There are faster ways to do this but that requires more buffer memeory
    //or memeory allocation. So we keep it simple for the moment.
    for (int ii=0;ii<abs(amount);ii++)
    {
        int tmp=0;
        if (amount>0)
        {
            tmp = arr[size];
            for (int jj=size;jj>1;jj--)
                arr[jj] = arr[jj-1];
            arr[1] = tmp;    
        }
        else
        {
            tmp = arr[1];
            for (int jj=1;jj<size-1;jj++)
                arr[jj] = arr[jj+1];
            arr[size] = tmp;    
        }
    }

    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;
}

int SHIFTARRAY_()
{
    //Pull the values off the stack
    int val = (int)*sp++;
    int amount = (int)*sp++;
    Val *arr = (Val*)*sp;  

    if (arr == 0)
    {
        bad((char*)"SETARRAY: BAD ARRAY POINTER");
        return 0;
    }

    int size = (int)arr[0];

    //Check if we shift more then the array size
    if (abs(amount) > size)
    {
        bad((char*)"SHIFTARRAY: SHIFT AMOUNT LARGER THEN ARRAY SIZE");
        return 0;
    }   

    int ii =0;
    if (amount>0)
    {
        for (ii= size-amount; ii>0; ii--)
        {
            arr[ii+amount] = arr[ii]; //Shift right
        }

        for (ii=1;ii<=amount;ii++)
        {
            arr[ii] = val; //Set new values
        }
    }
    else if (amount<0)
    {
        for (ii=1; ii<=size+amount; ii++)
        {
            arr[ii] = arr[ii-amount]; //Shift left
        }

        for (ii=size+amount+1;ii<=size;ii++)
        {
            arr[ii] = val; //Set new values
        }
    }
    else
    {
        //If amount is 0 we do nothing
    }

    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;
}

int SETARRAY_()
{ 
    //Pull the values off the stack
    int val = (int)*sp++;
    int end = (int)*sp++;
    int start = (int)*sp++;
    Val *arr = (Val*)*sp;  

    if (arr == 0)
    {
        bad((char*)"SETARRAY: BAD ARRAY POINTER");
        return 0;
    }

    int size = (int)arr[0];

    //Check if the array indices are valid
    if (start<1 || end<1 || start>end || end>size)
    {
        bad((char*)"SETARRAY: INDEX OUT OF BOUNDS");
        return 0;
    }

    //Set Array
    int i;
    for (i=start; i<=end; i++)
        arr[i] = val; //Set value in array


    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;
}

int SCALELIMITARRAY_()
{
   //Pull the values off the stack
    int max = (int)*sp++;
    int min = (int)*sp++;
    int perc = (int)*sp++;
    Val *arr = (Val*)*sp;  

    if (arr == 0)
    {
        bad((char*)"SCALELIMITARRAY: BAD ARRAY POINTER");
        return 0;
    }

    int size = (int)arr[0];

    //Set Array
    int i;
    for (i=1; i<=size; i++)
    {
        arr[i] = (int)((float)arr[i]*((float)perc/100.0)); //We cast to int without rounding so stuff converges to 0
        if (arr[i] < min)
            arr[i] = min;
        if (arr[i] > max)
            arr[i] = max;    
    }
    //Hmm... How do we deal with no return??? Just return a dummy value?
    *sp = 0; //Push 0 to the stack
    STEP;
}

int RANDOM_()
{
    int max = (int)*sp++;
    int min = (int)*sp;  //Pull value from Stack and rewind stack
    int rnd = (int)random(min,max);
    *sp=rnd; //Push back to to the stack
    STEP;
}

int LIMIT_()
{
    int max = (int)*sp++;
    int min = (int)*sp++;
    int val = (int)*sp;  //Pull value from Stack and rewind stack
    if (val>max)
        val = max;
    else if (val < min) 
        val = min;   
    *sp=val; //Push back to to the stack
    STEP;
}

int TIMESTAMP_()
{
    int div = (int)*sp;
    int ts = (int)(getTimestamp()/div);
    *sp=ts; //Push back to to the stack
    STEP;
}

int HASORIGIN_()
{
    if (NULL == Loc_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float oLat = 0;
        float oLon = 0;
        float lat = 0;
        float lon = 0;
        float alt = 0;
        float speed = 0;
        float dir = 0;

        int8_t res = Loc_Func(&oLat,&oLon,&lat,&lon,&alt,&speed,&dir);
        if (((oLat == 0) && (oLon == 0)) || (res != 1))
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
    }
    *sp=1; //Push back to to the stack
    STEP;
}

int HASGPS_()
{
    if (NULL == Loc_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float oLat = 0;
        float oLon = 0;
        float lat = 0;
        float lon = 0;
        float alt = 0;
        float speed = 0;
        float dir = 0;

        int8_t res = Loc_Func(&oLat,&oLon,&lat,&lon,&alt,&speed,&dir);
        if (((lat == 0) && (lon == 0)) || (res != 1))
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
    }
    *sp=1; //Push back to to the stack
    STEP;
}

int ORIGINDIST_()
{
    if (NULL == Loc_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;        
    }

    float oLat = 0;
    float oLon = 0;
    float lat = 0;
    float lon = 0;
    float alt = 0;
    float speed = 0;
    float dir = 0;

    int8_t res = Loc_Func(&oLat,&oLon,&lat,&lon,&alt,&speed,&dir);
    if (res != 1)
    {
        *sp=0; //Push back to to the stack
        STEP;
    } 

    float xom1 = 0;
    float yom1 = 0;
    latlon_to_meters(oLat, oLon, &xom1,  &yom1);
    float xom2 = 0;
    float yom2 = 0;
    latlon_to_meters(lat, lon, &xom2,  &yom2);
    GeoResult gr = xy_to_polar(xom1, yom1, xom2, yom2); 

    *sp=(int)round(gr.distance); //Push back to to the stack
    STEP;  
}

int ORIGINANGLE_()
{
    if (NULL == Loc_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;        
    }

    float oLat = 0;
    float oLon = 0;
    float lat = 0;
    float lon = 0;
    float alt = 0;
    float speed = 0;
    float dir = 0;

    int8_t res = Loc_Func(&oLat,&oLon,&lat,&lon,&alt,&speed,&dir);
    if (res != 1)
    {
        *sp=0; //Push back to to the stack
        STEP;
    } 

    float xom1 = 0;
    float yom1 = 0;
    latlon_to_meters(oLat, oLon, &xom1,  &yom1);
    float xom2 = 0;
    float yom2 = 0;
    latlon_to_meters(lat, lon, &xom2,  &yom2);
    GeoResult gr = xy_to_polar(xom1, yom1, xom2, yom2); 

    *sp=(int)round(gr.bearing_deg); //Push back to to the stack
    STEP;  
}

int GPSSPEED_()
{
    if (NULL == Loc_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;        
    }

    float oLat = 0;
    float oLon = 0;
    float lat = 0;
    float lon = 0;
    float alt = 0;
    float speed = 0;
    float dir = 0;

    int8_t res = Loc_Func(&oLat,&oLon,&lat,&lon,&alt,&speed,&dir);
    if (res != 1)
    {
        *sp=0; //Push back to to the stack
        STEP;
    } 
    *sp=(int)round(speed); //Push back to to the stack
    STEP; 
}

int GPSDIR_()
{
    if (NULL == Loc_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;        
    }

    float oLat = 0;
    float oLon = 0;
    float lat = 0;
    float lon = 0;
    float alt = 0;
    float speed = 0;
    float dir = 0;

    int8_t res = Loc_Func(&oLat,&oLon,&lat,&lon,&alt,&speed,&dir);
    if (res != 1)
    {
        *sp=0; //Push back to to the stack
        STEP;
    } 
    *sp=(int)round(dir); //Push back to to the stack
    STEP; 
}

int GPSALT_()
{
    if (NULL == Loc_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;        
    }

    float oLat = 0;
    float oLon = 0;
    float lat = 0;
    float lon = 0;
    float alt = 0;
    float speed = 0;
    float dir = 0;

    int8_t res = Loc_Func(&oLat,&oLon,&lat,&lon,&alt,&speed,&dir);
    if (res != 1)
    {
        *sp=0; //Push back to to the stack
        STEP;
    } 
    *sp=(int)round(alt); //Push back to to the stack
    STEP; 
}

int DIST_ ()
{
    int Y2 = (int)*sp++;
    int X2 = (int)*sp++;
    int Y1 = (int)*sp++;    
    int X1 = (int)*sp;  //Pull value from Stack and rewind stack
    GeoResult res = xy_to_polar(X1,Y1,X2,Y2);
 
    *sp=(int)round(res.distance); //Push back to to the stack
    STEP;
}

int ANGLE_ ()
{
    int Y2 = (int)*sp++;
    int X2 = (int)*sp++;
    int Y1 = (int)*sp++;    
    int X1 = (int)*sp;  //Pull value from Stack and rewind stack
    GeoResult res = xy_to_polar(X1,Y1,X2,Y2);
 
    *sp=(int)round(res.bearing_deg); //Push back to to the stack
    STEP;
}

int HASGYRO_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if ((res & GYRO_BIT) == 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
    }
    *sp=1; //Push back to to the stack
    STEP;
}

int HASACC_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if ((res & ACC_BIT) == 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
    }
    *sp=1; //Push back to to the stack
    STEP;
}

int HASMAG_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if ((res & MAG_BIT) == 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
    }
    *sp=1; //Push back to to the stack
    STEP;
}

int PITCH_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=pitch; //Push back to to the stack
        STEP;
    }
}

int ROLL_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=roll; //Push back to to the stack
        STEP;
    }
}

int YAW_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=yaw; //Push back to to the stack
        STEP;
    }
}

int ACCX_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=accX; //Push back to to the stack
        STEP;
    }
}

int ACCY_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=accY; //Push back to to the stack
        STEP;
    }
}

int ACCZ_()
{
    if (NULL == IMU_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float roll = 0;
        float pitch = 0;
        float yaw = 0;
        float accX = 0;
        float accY = 0;
        float accZ = 0;

        int8_t res = IMU_Func(&roll,&pitch,&yaw,&accX,&accY,&accZ);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=accY; //Push back to to the stack
        STEP;
    }
}

int TEMP_()
{
    if (NULL == ENV_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float temp = 0;
        float hum = 0;
        float bright = 0;

        int8_t res = ENV_Func(&temp,&hum,&bright);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        if (temp <= -10000)
        {
            *sp=-10000; //Push back to to the stack
            STEP;
        }
        else
        {
            *sp=(int)round(temp*10); //Push back to to the stack
            STEP;
        }
    }
}

int HUM_()
{
    if (NULL == ENV_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float temp = 0;
        float hum = 0;
        float bright = 0;

        int8_t res = ENV_Func(&temp,&hum,&bright);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=(int)round(hum); //Push back to to the stack
        STEP;
    }
}

int BRIGHT_()
{
    if (NULL == ENV_Func)
    {
        *sp=0; //Push back to to the stack
        STEP;
    }
    else
    {
        float temp = 0;
        float hum = 0;
        float bright = 0;

        int8_t res = ENV_Func(&temp,&hum,&bright);
        if (res < 0)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }
        *sp=(int)round(bright); //Push back to to the stack
        STEP;
    }
}

int GETPARAM_()
{
    int val = *sp;  //Pull value from Stack and rewind stack
    if (PARAM_Func == NULL)
    {
        *sp=0; //Push back to to the stack
        STEP;  
    }
    if (val < 0)
        val = 0;
    val = PARAM_Func(val);
    *sp=val; //Push back to to the stack
    STEP;
}

int WAITFOR_()
{
    int timeout = (int)*sp++;
    int value = (int)*sp++;
    int condition = (int)*sp++;    
    int event = (int)*sp;  //Pull value from Stack and rewind stack

    if (event == EVENT_PULSE)
    {
        if (SYNC_Func == NULL)
        {
            *sp=0; //Push back to to the stack
            STEP;
        }

        uint8_t ret = SYNC_Func(value,timeout); //Call the sync function
        if (ret == 0) //If we timed out
        {
            *sp=0; //Push back to to the stack
            STEP;            
        }
        else
        {
            *sp=1; //Push back to to the stack
            STEP; 
        }
    }
    else
    {
        bad((char*)"WAITFOR: EVENT NOT SUPPORTED");
        return 0;
    }


}

//We get the name of the function and the number of arguments.
//Validate here if the number of arguments is correct and then push the function to the progam buffer.
//In the function we pull the arguments off the dstasck and put the result back on the stack.
int funhook_(char *msg, int n) 
{   
	if (!strcmp(msg,LIMIT256_T))
    {
        if (n!=1) 
        {
            bad((char*)"LIMIT256: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(LIMIT256_);STEP;
    }
	if (!strcmp(msg,SCALE_T))
    {
        if (n!=5) 
        {
            bad((char*)"SCALE: 5 ARGUMENTS REQUIRED");
            return 0;
        }
        emit(SCALE_);STEP;
    }    
	if (!strcmp(msg,LIMIT_T))
    {
        if (n!=3) 
        {
            bad((char*)"LIMIT: 3 ARGUMENTS REQUIRED");
            return 0;
        }
        emit(LIMIT_);STEP;
    }    
	if (!strcmp(msg,ABS_T))
    {
        if (n!=1) 
        {
            bad((char*)"ABS: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(ABS_);STEP;
    }    
	if (!strcmp(msg,SIN256_T))
    {
        if (n!=1) 
        {
            bad((char*)"ABS: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(SIN256_);STEP;
    }        
	if (!strcmp(msg,SETARRAY_T))
    {
        if (n!=4) 
        {
            bad((char*)"SETARRAY: 4 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(SETARRAY_);STEP;
    }     
	if (!strcmp(msg,ROTATEARRAY_T))
    {
        if (n!=2) 
        {
            bad((char*)"ROTATEARRAY: 2 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(ROTATEARRAY_);STEP;
    }   
	if (!strcmp(msg,SHIFTARRAY_T))
    {
        if (n!=3) 
        {
            bad((char*)"SHIFTARRAY: 3 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(SHIFTARRAY_);STEP;
    }       
	if (!strcmp(msg,COPYARRAY_T))
    {
        if (n!=2) 
        {
            bad((char*)"COPYARRAY: 2 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(COPYARRAY_);STEP;
    }              
	if (!strcmp(msg,GETMAXLED_T))
    {
        if (n!=0) 
        {
            bad((char*)"GETMAXLED: NO ARGUMENT REQUIRED");
            return 0;
        }        
		emit(GETMAXLED_);STEP;
    }  
	if (!strcmp(msg,WAIT_T))
    {
        if (n!=1) 
        {
            bad((char*)"WAIT: 1 ARGUMENT REQUIRED");
            return 0;
        }        
		emit(WAIT_);STEP;
    }  
	if (!strcmp(msg,SETLEDRGB_T))
    {
        if (n!=3) 
        {
            bad((char*)"SETLEDRGB: 3 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(SETLEDRGB_);STEP;
    }    
    	if (!strcmp(msg,SETLEDCOL_T))
    {
        if (n!=3) 
        {
            bad((char*)"SETLEDCOL: 3 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(SETLEDCOL_);STEP;
    }  
 	if (!strcmp(msg,TIMESTAMP_T))
    {
        if (n!=1) 
        {
            bad((char*)"TIMESTAMP: 1 ARGUMENT REQUIRED");
            return 0;
        }        
		emit(TIMESTAMP_);STEP;
    }      
 	if (!strcmp(msg,RANDOM_T))
    {
        if (n!=2) 
        {
            bad((char*)"RANDOM: 2 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(RANDOM_);STEP;
    }      
 	if (!strcmp(msg,SCALELIMITARRAY_T))
    {
        if (n!=4) 
        {
            bad((char*)"SCALELIMITARRAY: 4 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(SCALELIMITARRAY_);STEP;
    }      
	if (!strcmp(msg,RGBTOHSVARRAY_T))
    {
        if (n!=3) 
        {
            bad((char*)"RGBTOHSVARRAY: 3 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(RGBTOHSVARRAY_);STEP;
    }   
	if (!strcmp(msg,HSVTORGBARRAY_T))
    {
        if (n!=3) 
        {
            bad((char*)"HSVTORGBARRAY: 3 ARGUMENTS REQUIRED");
            return 0;
        }        
		emit(HSVTORGBARRAY_);STEP;
    }      
    if (!strcmp(msg,LOADLUT_T))
    {
        if (n!=1) 
        {
            bad((char*)"LOADLUT: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(LOADLUT_);STEP;
    }
    if (!strcmp(msg,SAVELUT_T))
    {
        if (n!=1) 
        {
            bad((char*)"SAVELUT: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(SAVELUT_);STEP;
    }
    if (!strcmp(msg,LUTSIZE_T))
    {
        if (n!=1) 
        {
            bad((char*)"LUTSIZE: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(LUTSIZE_);STEP;
    }
    if (!strcmp(msg,LUTTOARRAY_T))
    {
        if (n!=1) 
        {
            bad((char*)"LUTTOARRAY: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(LUTTOARRAY_);STEP;
    }
    if (!strcmp(msg,ARRAYTOLUT_T))
    {
        if (n!=1) 
        {
            bad((char*)"ARRAYTOLUT: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(ARRAYTOLUT_);STEP;
    }
    if (!strcmp(msg,LUT_T))
    {
        if (n!=1) 
        {
            bad((char*)"LUT: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(LUT_);STEP;
    }
    if (!strcmp(msg,USEGAMMA_T))
    {
        if (n!=1) 
        {
            bad((char*)"USEGAMMA: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(USEGAMMA_);STEP;
    }    
    if (!strcmp(msg,GAMMA256_T))
    {
        if (n!=1) 
        {
            bad((char*)"GAMMA256: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(GAMMA256_);STEP;
    }    
    if (!strcmp(msg,HASORIGIN_T))
    {
        if (n!=0) 
        {
            bad((char*)"HASORIGIN: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(HASORIGIN_);STEP;
    }   
    if (!strcmp(msg,HASGPS_T))
    {
        if (n!=0) 
        {
            bad((char*)"HASGPS: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(HASGPS_);STEP;
    }   
    if (!strcmp(msg,ORIGINDIST_T))
    {
        if (n!=0) 
        {
            bad((char*)"ORIGINDIST: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(ORIGINDIST_);STEP;
    }       
    if (!strcmp(msg,ORIGINANGLE_T))
    {
        if (n!=0) 
        {
            bad((char*)"ORIGINANGLE: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(ORIGINANGLE_);STEP;
    }   
    if (!strcmp(msg,GPSSPEED_T))
    {
        if (n!=0) 
        {
            bad((char*)"GPSSPEED: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(GPSSPEED_);STEP;
    }  
    if (!strcmp(msg,GPSDIR_T))
    {
        if (n!=0) 
        {
            bad((char*)"GPSDIR: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(GPSDIR_);STEP;
    }             
    if (!strcmp(msg,DIST_T))
    {
        if (n!=4) 
        {
            bad((char*)"DIST: 4 ARGUMENTS REQUIRED");
            return 0;
        }
        emit(DIST_);STEP;
    }    
    if (!strcmp(msg,ANGLE_T))
    {
        if (n!=4) 
        {
            bad((char*)"ANGLE: 4 ARGUMENTS REQUIRED");
            return 0;
        }
        emit(ANGLE_);STEP;
    }  
    if (!strcmp(msg,HASGYRO_T))
    {
        if (n!=0) 
        {
            bad((char*)"HASGYRO: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(HASGYRO_);STEP;
    }  
    if (!strcmp(msg,HASACC_T))
    {
        if (n!=0) 
        {
            bad((char*)"HASACC: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(HASACC_);STEP;
    }  
    if (!strcmp(msg,HASMAG_T))
    {
        if (n!=0) 
        {
            bad((char*)"HASMAG: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(HASMAG_);STEP;
    }      
    if (!strcmp(msg,ROLL_T))
    {
        if (n!=0) 
        {
            bad((char*)"ROLL: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(ROLL_);STEP;
    }  
    if (!strcmp(msg,PITCH_T))
    {
        if (n!=0) 
        {
            bad((char*)"PITCH: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(PITCH_);STEP;
    }  
    if (!strcmp(msg,YAW_T))
    {
        if (n!=0) 
        {
            bad((char*)"YAW: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(YAW_);STEP;
    }          
    if (!strcmp(msg,ACCX_T))
    {
        if (n!=0) 
        {
            bad((char*)"ACCX: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(ACCX_);STEP;
    }  
    if (!strcmp(msg,ACCY_T))
    {
        if (n!=0) 
        {
            bad((char*)"ACCY: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(ACCY_);STEP;
    }    
    if (!strcmp(msg,ACCZ_T))
    {
        if (n!=0) 
        {
            bad((char*)"ACCZ: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(ACCZ_);STEP;
    }      
    if (!strcmp(msg,TEMP_T))
    {
        if (n!=0) 
        {
            bad((char*)"TEMP: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(HUM_);STEP;
    }        
    if (!strcmp(msg,TEMP_T))
    {
        if (n!=0) 
        {
            bad((char*)"HUM: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(HUM_);STEP;
    }       
    if (!strcmp(msg,BRIGHT_T))
    {
        if (n!=0) 
        {
            bad((char*)"BRIGHT: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(BRIGHT_);STEP;
    }         
    if (!strcmp(msg,VERSION_T))
    {
        if (n!=0) 
        {
            bad((char*)"VERSION: NO ARGUMENTS REQUIRED");
            return 0;
        }
        emit(VERSION_);STEP;
    }   
    if (!strcmp(msg,WAITFOR_T))
    {
        if (n!=4) 
        {
            bad((char*)"WAITFOR: 4 ARGUMENTS REQUIRED");
            return 0;
        }
        emit(WAITFOR_);STEP;
    }  
    if (!strcmp(msg,GETPARAM_T))
    {
        if (n!=1) 
        {
            bad((char*)"GETPARAM: 1 ARGUMENT REQUIRED");
            return 0;
        }
        emit(GETPARAM_);STEP;
    }      
    //If we reach here we did not find a matching function                   
    else	
		return 0;
}

//-------------------------------------
void registerhook() 
{
    kwdhook=kwdhook_;
    funhook=funhook_;
}


//Location Callback provides Origin Latitude, Orgin Longitude, Latitude, Longitude, Altitude, Speed, Course
void register_location_callback(CallbackLocationFunction func)
{
    Loc_Func = func;
}


//IMU Callback provides Roll, Pitch, Yaw, AccX, AccY, AccZ,  
void register_imu_callback(CallbackIMUFunction func)
{
    IMU_Func = func;    
}

//SYNC Callback provides external sync pulse syncronisation
void register_sync_callback(CallbackSYNCFunction func)
{
    SYNC_Func = func;    
}

//PARAM Callback provides access to external parameters
void register_param_callback(CallbackPARAMFunction func)
{
    PARAM_Func = func;    
}
#endif