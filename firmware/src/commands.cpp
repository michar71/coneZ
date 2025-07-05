#include "commands.h"
#include <LittleFS.h>
#include <FS.h>
#include <SimpleSerialShell.h>

//Serial/Telnet Shell comamnds

void renameFile(fs::FS &fs, const char *path1, const char *path2) 
{
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2)) {
      Serial.println("- file renamed");
    } else {
      Serial.println("- rename failed");
    }
  }
  
  void deleteFile(fs::FS &fs, const char *path) 
  {
    Serial.printf("Deleting file: %s\r\n", path);
    if (fs.remove(path)) {
      Serial.println("- file deleted");
    } else {
      Serial.println("- delete failed");
    }
  }


void listDir(fs::FS &fs, const char *dirname, uint8_t levels) 
{
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char *path) 
{
    Serial.printf("Listing file: %s\r\n", path);
    Serial.println();
  
    File file = fs.open(path);
    if (!file || file.isDirectory()) 
    {
      Serial.println("- failed to open file for reading");
      return;
    }
  
    while (file.available()) 
    {
      Serial.write(file.read());
    }
    file.close();
  }
  
  void writeFile(fs::FS &fs, const char *path, const char *message) {
    Serial.printf("Writing file: %s\r\n", path);
  
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      Serial.println("- failed to open file for writing");
      return;
    }
    if (file.print(message)) 
    {
      Serial.println("- file written");
    } 
    else 
    {
      Serial.println("- write failed");
    }
    file.close();
  }

/*
Commands
*/
int test(int argc, char **argv) 
{
  Serial.println("Test function called");
  Serial.print(argc);
  Serial.println(" Arguments");
  for (int ii=0;ii<argc;ii++)
  {
    Serial.print("Argument ");
    Serial.print(ii);
    Serial.print(" : ");
    Serial.println(argv[ii]);
  }  
  return 0;
};

int delFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        Serial.println("Wrong argument count");
        return 1;
    }
    deleteFile(FSLINK,argv[1]);
    return 0;
}

int renFile(int argc, char **argv) 
{
    if (argc != 3)
    {
        Serial.println("Wrong argument count");
        return 1;
    }
    renameFile(FSLINK,argv[1], argv[2]);
    return 0;
}

int listFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        Serial.println("Wrong argument count");
        return 1;
    }

    readFile(FSLINK,argv[1]); 
    Serial.printf("");
    return 0;
}

int listDir(int argc, char **argv) 
{
    if (argc != 1)
    {
        Serial.println("Wrong argument count");
        return 1;
    }
    listDir(FSLINK,"/",1); 
    return 0;
}

int loadFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        Serial.println("Wrong argument count");
        return 1;       
    }
    else
    {
        int linecount = 0;
        int charcount = 0;
        char line[256];
        char inchar;
        bool isDone = false;
        Serial.print("Ready for file. Press CTRL+Z to end transmission and save file");
        Serial.println(argv[1]);
        //Flush serial buffer
        Serial.flush();
        //create file
        File file = FSLINK.open(argv[1], FILE_WRITE);
        if (!file) 
        {
            Serial.println("- failed to open file for writing");
            return 1;
        }

        do
        {
            //Get one character from serial port
            if (Serial.available())
            {
                inchar = Serial.read();
                //Check if its a break character
                if (inchar == 0x1A) 
                {
                    //Break loop 
                    break;
                }
                else
                {
                    //Wait for a full line
                    line[charcount] = inchar;
                    charcount++;
                    if (charcount>254)
                    {
                        Serial.print("Line ");
                        Serial.print(linecount+1);
                        Serial.println(" too long");
                        break;
                    }
                    if (inchar == '\n')
                    {
                        //Write line
                        if (file.print(line)) 
                        {
                        } 
                        else 
                        {
                          Serial.println("Write Error");
                          file.close();
                          return 1;
                        }
                        //increase line counter
                        linecount++;
                        //clear line
                        charcount = 0;
                        line[0] = 0;
            
                    }
                }
            }
        }
        while (isDone == false);
        //close file
        file.close();

        Serial.print(linecount);
        Serial.println(" Lines written to file");
        return 0;
    }
}

void init_commands(Stream *dev)
{
    shell.attach(*dev);

    //Test Commands
    shell.addCommand(F("test"), test);

    //file Sydstem commands
    shell.addCommand(F("dir"), listDir);    
    shell.addCommand(F("list"), listFile);
    shell.addCommand(F("ren"), renFile);
    shell.addCommand(F("del"), delFile);
    shell.addCommand(F("load"), loadFile);   
    
    //System commands

    //Basic Commands
}

void run_commands(void)
{
    shell.executeIfInput();
}