#include "basic_wrapper.h"


void setup_basic()
{
    //Set Callback Functions

    //Start Own Thread
}

void basic_thread()
{
    //Wait for incoming execution filename via Mutex

    //If basic is not running
       //run basic program
    //Else show error  
    
    
    /*
    if (argc == 2)
    {
        Serial.print("RUNNING ");
        Serial.println(argv[1]);
        initbasic(1);        
        int res = interp(argv[1]);
        if (res != 0)
        {
            Serial.print("Error Exit Code: ");
            Serial.println(res);
        }   
        else 
            Serial.println("DONE");
        return 0;
    }
    else
    {
        Serial.println("Wrong argument count");
        return 1;
    }    
    */
}

void set_basic_program(Stream *output,char* prog)
{
    //Set Basic program name with mutex
}