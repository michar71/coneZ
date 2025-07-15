#include <Arduino.h>

#define LED_PIN 40

SemaphoreHandle_t serialMutex;
const int ledPin = LED_PIN; // Example: Using an LED for visual indication
long cnt1 = 0;
long cnt2 = 0;

void task1(void *pvParameters) 
{
  char str1[256];
  sprintf(str1,"%d Task 1 - Running on Core %d\n",millis(),xPortGetCoreID());
  Serial.print(str1);
  for (;;) 
  {
    if (xSemaphoreTake(serialMutex, 200) == pdTRUE) 
    {
      sprintf(str1,"%d Task 1 - Acquired serial mutex on Core %d\n",millis(),xPortGetCoreID());
      Serial.print(str1);
      digitalWrite(ledPin, HIGH);
      delay(100);
      digitalWrite(ledPin, LOW);
      cnt1++;
      sprintf(str1,"%d Task 1 - Release serial mutex on Core %d\n",millis());
      Serial.print(str1);
      xSemaphoreGive(serialMutex);
    } 
    else 
    {
      sprintf(str1,"%d Task 1 - Failed to acquire serial mutex\n",millis());
      Serial.print(str1);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void task2(void *pvParameters) 
{
  char str2[256];
  sprintf(str2,"%d Task 2 - Running on Core %d\n",millis(),xPortGetCoreID());
  Serial.print(str2);
  for (;;) 
  {
    if (xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) 
    {
      sprintf(str2,"%d Task 2 - Acquired serial mutex on Core %d\n",millis(),xPortGetCoreID());
      Serial.print(str2);
      digitalWrite(ledPin, HIGH);
      delay(200);
      digitalWrite(ledPin, LOW);
      cnt2++;
      sprintf(str2,"%d Task 2 - Release serial mutex on Core %d\n",millis());
      Serial.print(str2);
      xSemaphoreGive(serialMutex);
    } 
    else 
    {
      sprintf(str2,"%d Task 2 - Failed to acquire serial mutex\n",millis());
      Serial.print(str2);
    }
    vTaskDelay(150 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  unsigned long t_start = millis();
  while (!Serial)
  {
      if( millis() - t_start > 15 * 1000 )
        break;
  }  

  serialMutex = xSemaphoreCreateMutex();
  if (serialMutex == NULL) 
  {
    Serial.println("Failed to create mutex");
    while (1); // Handle error appropriately
  }

  xTaskCreatePinnedToCore(
    task1,      /* Task function. */
    "Task1",    /* Name of task. */
    10000,      /* Stack size of task */
    NULL,       /* Parameter passed as input to the task */
    1,          /* Priority of the task */
    NULL,       /* Task handle. */
    1           /* Core where the task should run */
  );

  xTaskCreatePinnedToCore(
    task2,      /* Task function. */
    "Task2",    /* Name of task. */
    10000,      /* Stack size of task */
    NULL,       /* Parameter passed as input to the task */
    1,          /* Priority of the task */
    NULL,       /* Task handle. */
    0           /* Core where the task should run */
  );
  char str4[256];
  sprintf(str4,"%d Setup/Loop - Running on Core %d\n",millis(),xPortGetCoreID());
  Serial.print(str4);
}

void loop() 
{
  char str3[256];
  // Empty loop as all the logic is in the tasks.
  // You could add other tasks here, or use the loop() function to perform work on core 1
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay to avoid blocking other cores

  //Not mutex protected, should be interrupted
  for (int ii=0;ii<10000;ii++)
  {
    float s = sin(ii);
    sprintf(str3,"%d  Sin(%d) = %f\n",millis(),ii,s);
    Serial.print(str3);
  }  
  //Mutex Protrcted. Should not be interrupted
  if (xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) 
  {
      sprintf(str3,"%d Loop - Acquired serial mutex on Core %d\n",millis(),xPortGetCoreID());
      Serial.print(str3);
      digitalWrite(ledPin, HIGH);
      delay(50);
      digitalWrite(ledPin, LOW);
      delay(50);    
      digitalWrite(ledPin, HIGH);
      delay(50);
      digitalWrite(ledPin, LOW);    
      sprintf(str3,"%d TASK COUNT -> Task 1 Count: %d Task 2 Count: %d\n",millis(),cnt1,cnt2);
      Serial.print(str3);
      sprintf(str3,"%d Loop - Release serial mutex on Core %d\n",millis());
      Serial.print(str3);
      xSemaphoreGive(serialMutex);  
  }      
}