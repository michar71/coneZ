#include <Arduino.h>
#include <esp_heap_caps.h>

// Declare a pointer to hold the PSRAM-allocated array
const size_t ARRAY_SIZE = 4 * 1024; // 1MB example size

uint8_t ram_array1[4096];
uint8_t ram_array2[4096];

uint8_t* psramarray1 = nullptr; 
uint8_t* psramarray2 = nullptr; 

void setup() 
{
  Serial.begin(115200);
  delay(1000); // Give time for serial to initialize

  while (!Serial)
  {
  }  

  Serial.printf("ram_array1 allocated in RAM at address: 0x%X\n", (uint32_t)ram_array1);
  Serial.printf("ram_array2 allocated in RAM at address: 0x%X\n", (uint32_t)ram_array2);

  void* ptr1 = malloc(1024); // Small allocation, likely internal RAM
  void* ptr2 = malloc(10 * 1024); // Larger allocation, likely PSRAM

  Serial.printf("RAM Ptr1 address: 0x%08X\n", (uint32_t)ptr1);
  Serial.printf("PSRAM Ptr2 address: 0x%08X\n", (uint32_t)ptr2);

  // Free the allocated memory
  free(ptr1);
  free(ptr2);

  // Allocate memory for the array in PSRAM
  psramarray1 = (uint8_t*)ps_malloc(ARRAY_SIZE); 
  psramarray2 = (uint8_t*)ps_malloc(ARRAY_SIZE); 

  if (psramarray1 == nullptr) 
  {
    Serial.println("Failed to allocate PSRAM for psramarray1!");
    while (true); // Halt if allocation fails
  } 
  else 
  {
    Serial.printf("psramarray1 allocated in PSRAM at address: 0x%X\n", (uint32_t)psramarray1);
    // Initialize the array (optional)
    for (size_t i = 0; i < ARRAY_SIZE; i++) {
      psramarray1[i] = 0; 
    }
    Serial.println("psramarray1 initialized.");
  }  

  if (psramarray2 == nullptr) 
  {
    Serial.println("Failed to allocate PSRAM for psramarray2!");
    while (true); // Halt if allocation fails
  } 
  else 
  {
    Serial.printf("psramarray2 allocated in PSRAM at address: 0x%X\n", (uint32_t)psramarray2);
    // Initialize the array (optional)
    for (size_t i = 0; i < ARRAY_SIZE; i++) {
      psramarray2[i] = 0; 
    }
    Serial.println("psramarray2 initialized.");
  }    
}

void loop() 
{
  long t1 = millis();

  uint8_t *pr1 = psramarray1;
  uint8_t *pr2 = psramarray2; 
  uint8_t *prt = nullptr;

  // Use the PSRAM arrays for some operation
  for (int ii=0;ii<255;ii++)
  {
    for (int jj=0;jj<4096;jj++)
    {
      pr1[jj] = pr2[jj] + ii; // Example operation
    }

    //Swap pointers to simulate some operation
    prt = pr1;
    pr1 = pr2;
    pr2 = prt;
  }
  Serial.printf("Time taken for PSRAM operation: %ld ms\n", millis() - t1);

t1 = millis();

  pr1 = ram_array1;
  pr2 = ram_array2;
  prt = nullptr;

  // Use the PSRAM arrays for some operation
  for (int ii=0;ii<255;ii++)
  {
    for (int jj=0;jj<4096;jj++)
    {
      pr1[jj] = pr2[jj] + ii; // Example operation
    }
    //Swap pointers to simulate some operation
    prt = pr1;
    pr1 = pr2;
    pr2 = prt;
  }
  Serial.printf("Time taken for RAM operation: %ld ms\n", millis() - t1);

}