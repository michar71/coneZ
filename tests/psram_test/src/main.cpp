#include <Arduino.h>
#include <esp_heap_caps.h>

// Declare a pointer to hold the PSRAM-allocated array
uint8_t* largeArray = nullptr; 
const size_t ARRAY_SIZE = 1024 * 1024; // 1MB example size

void setup() 
{
  Serial.begin(115200);
  delay(1000); // Give time for serial to initialize

  void* ptr1 = malloc(1024); // Small allocation, likely internal RAM
  void* ptr2 = malloc(10 * 1024); // Larger allocation, likely PSRAM

  Serial.printf("Ptr1 address: 0x%08X\n", (uint32_t)ptr1);
  Serial.printf("Ptr2 address: 0x%08X\n", (uint32_t)ptr2);

  // Free the allocated memory
  free(ptr1);
  free(ptr2);

  // Allocate memory for the array in PSRAM
  largeArray = (uint8_t*)ps_malloc(ARRAY_SIZE); 

  if (largeArray == nullptr) {
    Serial.println("Failed to allocate PSRAM for largeArray!");
    while (true); // Halt if allocation fails
  } else {
    Serial.printf("largeArray allocated in PSRAM at address: 0x%X\n", (uint32_t)largeArray);
    // Initialize the array (optional)
    for (size_t i = 0; i < ARRAY_SIZE; i++) {
      largeArray[i] = i % 256; 
    }
    Serial.println("largeArray initialized.");
  }  
}

void loop() 
{
  // Your main loop code
}