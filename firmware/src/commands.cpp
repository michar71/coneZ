#include "commands.h"
#include <LittleFS.h>
#include <FS.h>
#include <SimpleSerialShell.h>
#include "basic_wrapper.h"

extern Stream* OutputStream;

//Serial/Telnet Shell comamnds

void renameFile(fs::FS &fs, const char *path1, const char *path2) 
{
    OutputStream->printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2)) {
      OutputStream->println("- file renamed");
    } else {
      OutputStream->println("- rename failed");
    }
  }
  
  void deleteFile(fs::FS &fs, const char *path) 
  {
    OutputStream->printf("Deleting file: %s\r\n", path);
    if (fs.remove(path)) {
      OutputStream->println("- file deleted");
    } else {
      OutputStream->println("- delete failed");
    }
  }


void listDir(fs::FS &fs, const char *dirname, uint8_t levels) 
{
  OutputStream->printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    OutputStream->println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    OutputStream->println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      OutputStream->print("  DIR : ");
      OutputStream->println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      OutputStream->print("  FILE: ");
      OutputStream->print(file.name());
      OutputStream->print("\tSIZE: ");
      OutputStream->println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char *path) 
{
    OutputStream->printf("Listing file: %s\r\n", path);
    OutputStream->println();
  
    File file = fs.open(path);
    if (!file || file.isDirectory()) 
    {
      OutputStream->println("- failed to open file for reading");
      return;
    }
  
    while (file.available()) 
    {
      OutputStream->write(file.read());
    }
    file.close();
  }
  
  void writeFile(fs::FS &fs, const char *path, const char *message) 
  {
    OutputStream->printf("Writing file: %s\r\n", path);
  
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      OutputStream->println("- failed to open file for writing");
      return;
    }
    if (file.print(message)) 
    {
      OutputStream->println("- file written");
    } 
    else 
    {
      OutputStream->println("- write failed");
    }
    file.close();
  }

/*
Commands
*/
int test(int argc, char **argv) 
{
  OutputStream->println("Test function called");
  OutputStream->print(argc);
  OutputStream->println(" Arguments");
  for (int ii=0;ii<argc;ii++)
  {
    OutputStream->print("Argument ");
    OutputStream->print(ii);
    OutputStream->print(" : ");
    OutputStream->println(argv[ii]);
  }  
  return 0;
};

int delFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        OutputStream->println("Wrong argument count");
        return 1;
    }
    deleteFile(FSLINK,argv[1]);
    return 0;
}

int renFile(int argc, char **argv) 
{
    if (argc != 3)
    {
        OutputStream->println("Wrong argument count");
        return 1;
    }
    renameFile(FSLINK,argv[1], argv[2]);
    return 0;
}

int listFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        OutputStream->println("Wrong argument count");
        return 1;
    }

    readFile(FSLINK,argv[1]); 
    OutputStream->printf("");
    return 0;
}

int listDir(int argc, char **argv) 
{
    if (argc != 1)
    {
        OutputStream->println("Wrong argument count");
        return 1;
    }
    listDir(FSLINK,"/",1); 
    return 0;
}

int loadFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        OutputStream->println("Wrong argument count");
        return 1;       
    }
    else
    {
        int linecount = 0;
        int charcount = 0;
        char line[256];
        char inchar;
        bool isDone = false;
        OutputStream->print("Ready for file. Press CTRL+Z to end transmission and save file");
        OutputStream->println(argv[1]);
        //Flush serial buffer
        OutputStream->flush();
        //create file
        File file = FSLINK.open(argv[1], FILE_WRITE);
        if (!file) 
        {
            OutputStream->println("- failed to open file for writing");
            return 1;
        }

        do
        {
            //Get one character from serial port
            if (OutputStream->available())
            {
                inchar = OutputStream->read();
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
                        OutputStream->print("Line ");
                        OutputStream->print(linecount+1);
                        OutputStream->println(" too long");
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
                          OutputStream->println("Write Error");
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

        OutputStream->print(linecount);
        OutputStream->println(" Lines written to file");
        return 0;
    }
}

int runBasic(int argc, char **argv) 
{
    if (argc != 2)
    {
        OutputStream->println("Wrong argument count");
        return 1;       
    }
    else
    {
        if (false == set_basic_program(OutputStream,argv[1]))
          OutputStream->println("BASSIC code already running");        
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
    shell.addCommand(F("run"), runBasic);
    
    //System commands

    //Basic Commands
}

void run_commands(void)
{
    shell.executeIfInput();
}

void setCLIEcho(bool echo)
{
  shell.setEcho(echo);
}