#include <Wire.h>
#include <Update.h>
#include <DHT.h>
#include <WiFi.h>
//#include <ArduinoIoTCloud.h>
#include <Adafruit_SSD1306.h>
#include <Arduino_ConnectionHandler.h>
#include "thingProperties.h"
#include "arduino_secrets.h"

#undef ERROR
#include <SparkFun_APDS9960.h>
// Configuration matérielle
#define DHTPIN 33
#define DHTTYPE DHT11

#define LED_PIN 5
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

#define SDA_PIN 21
#define SCL_PIN 22

/* Setting OLED display parameters */
#define OLED_WIDTH 128          // OLED display width, in pixels
#define OLED_HEIGHT 64          // OLED display height, in pixels
#define OLED_ADDRESS 0x3C       // i2c address for OLED display
#define OLED_RESET 4 

/* Fan pin */          
#define FAN 26               

// Bitmaps des symboles (8x8 pixels)
const unsigned char arrowUpBitmap[] PROGMEM = {
  0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x3C, 0x3C, 0x3C
};

const unsigned char arrowDownBitmap[] PROGMEM = {
  0x3C, 0x3C, 0x3C, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
};

const unsigned char arrowLeftBitmap[] PROGMEM = {
  0x18, 0x30, 0x7F, 0xFF, 0xFF, 0x7F, 0x30, 0x18
};

const unsigned char arrowRightBitmap[] PROGMEM = {
  0x18, 0x0C, 0xFE, 0xFF, 0xFF, 0xFE, 0x0C, 0x18
};

const unsigned char dotFilledBitmap[] PROGMEM = {
  0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C
};

const uint8_t dotEmptyBitmap[] = {
  0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00
};

// Structure pour gestion des symboles
struct GestureSymbol {
  const unsigned char* bitmap;
  uint32_t displayUntil;
};


// Objets capteurs
DHT dht(DHTPIN, DHTTYPE);
SparkFun_APDS9960 gestureSensor;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
GestureSymbol activeSymbols[6];

static QueueHandle_t tempQueue;

// Variables partagées
volatile bool ledState;// = false;
volatile int ledBrightness=0;
volatile bool fanState = false;
float temperature ;
float humidity;

// Sémaphores
SemaphoreHandle_t i2cMutex;
SemaphoreHandle_t ledMutex;
SemaphoreHandle_t serialMutex;
SemaphoreHandle_t displayMutex;
SemaphoreHandle_t symbolsMutex;
SemaphoreHandle_t fanMutex;

void dhtTask(void *pvParameters);
void gestureTask(void *pvParameters);

/* Status for OLED display indicators */
bool LEDStatus = false;
bool FANStatus = false;

void setup() {
  Serial.begin(115200);
  
  // Initialisation I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  
  // Initialisation capteurs
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN, OUTPUT);

  // Initialisation OLED
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS); 

  symbolsMutex = xSemaphoreCreateMutex();
  memset(activeSymbols, 0, sizeof(activeSymbols));

  /* OLED display at start */
  introDisplay();

  // Création des sémaphores
  i2cMutex = xSemaphoreCreateMutex();
  ledMutex = xSemaphoreCreateMutex();
  serialMutex = xSemaphoreCreateMutex();
  fanMutex = xSemaphoreCreateMutex();

  /* Defining task handles */
  TaskHandle_t autoFan_handle = NULL;

  // Configuration capteur de gestes
  if(xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
    //gestureSensor.reset(); // Ajouter cette ligne
    delay(100);
    if(!gestureSensor.init() || !gestureSensor.enableGestureSensor(true)) {
      Serial.println("ERREUR: Init capteur gestes");
      while(1);
    }
    gestureSensor.setGestureLEDDrive(LED_DRIVE_100MA);
    gestureSensor.setGestureGain(GGAIN_4X);
    xSemaphoreGive(i2cMutex);
  }

  tempQueue  = xQueueCreate(1, sizeof(int));

  displayMutex = xSemaphoreCreateMutex();

  // Création des tâches
  xTaskCreatePinnedToCore(dhtTask,"DHT_Task",2048,NULL,2,NULL,APP_CPU_NUM);

  xTaskCreatePinnedToCore(gestureTask,"Gesture_Task",3072,NULL,3,NULL,PRO_CPU_NUM);

  xTaskCreatePinnedToCore(indicatorDisplay,"DisplayTask",6144,NULL,4,NULL,PRO_CPU_NUM);

  xTaskCreatePinnedToCore (autoFanTask, "AutoFanTask", 2048, NULL, 1, &autoFan_handle, PRO_CPU_NUM); 

  /* Suspending auto mode tasks at start */
  //vTaskSuspend (autoFan_handle);

  // Suppression de la tâche loop
  vTaskDelete(NULL);
}

void loop() {
  // Non utilisé
}

// Tâches ---------------------------------------------------------------------

void dhtTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  for(;;) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int temperatureInt = (int)t;

    if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100))) {
      if(!isnan(h) && !isnan(t)) {
        //xQueueSend(tempQueue, &temperatureInt, 0);
        xQueueOverwrite(tempQueue, &temperatureInt);
        temperature = t;
        humidity = h;
        Serial.printf("[DHT] Temp: %.1fC Hum: %.1f%%\n", t, h);
      } else {
        Serial.println("[DHT] Erreur lecture");
      }
      xSemaphoreGive(serialMutex);
    }
    
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
  }
}

void gestureTask(void *pvParameters) {
  uint8_t gesture = 0;
  const uint32_t displayDuration = 2000;
  
  for(;;) {
    if(xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
      if(gestureSensor.isGestureAvailable()) {
        gesture = gestureSensor.readGesture();
        xSemaphoreGive(i2cMutex);
        
        if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100))) {
          uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
          switch(gesture) {
            case DIR_UP:
              if(xSemaphoreTake(fanMutex, pdMS_TO_TICKS(100))) {
                digitalWrite(FAN,HIGH);
                fanState = true;
                Serial.println("↑ [FAN] ACTIVATION");
                if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(100))) {
                  activeSymbols[0] = {arrowUpBitmap, now + displayDuration};
                  xSemaphoreGive(symbolsMutex);
                }
                xSemaphoreGive(fanMutex);
              }
              break;
              
            case DIR_DOWN:
              if(xSemaphoreTake(fanMutex, pdMS_TO_TICKS(100))) {
                digitalWrite(FAN,LOW);
                fanState = false;
                Serial.println("↓ [FAN] DESACTIVATION");
                if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(100))) {
                  activeSymbols[1] = {arrowDownBitmap, now + displayDuration};
                  xSemaphoreGive(symbolsMutex);
                }
                xSemaphoreGive(fanMutex);
              }
              break;
              
            case DIR_LEFT:
              if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                analogWrite(LED_PIN, 0);
                ledBrightness = 0;
                ledState = false;
                Serial.println("← LED OFF");
                if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(100))) {
                  activeSymbols[2] = {arrowLeftBitmap, now + displayDuration};
                  xSemaphoreGive(symbolsMutex);
                }
                xSemaphoreGive(ledMutex);
              }
              break;
              
            case DIR_RIGHT:
              if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                analogWrite(LED_PIN, 255);
                ledBrightness = 255;
                ledState = true;
                Serial.println("→ LED ON");
                if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(100))) {
                  activeSymbols[3] = {arrowRightBitmap, now + displayDuration};
                  xSemaphoreGive(symbolsMutex);
                }
                xSemaphoreGive(ledMutex);
              }
              break;

            case DIR_NEAR:
              if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                if(ledState){
                  ledBrightness = 50;
                  analogWrite(LED_PIN, ledBrightness); 
                  Serial.println("≈ Proche: 2%");
                  if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(100))) {
                    activeSymbols[4] = {dotEmptyBitmap, now + displayDuration};
                    xSemaphoreGive(symbolsMutex);
                  }
                }
                xSemaphoreGive(ledMutex);
              }
              break;
              
            case DIR_FAR:
               if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                if(ledState){
                  ledBrightness = 255;
                  analogWrite(LED_PIN, ledBrightness); // 100%
                  Serial.println("≈ Loin: 100%");
                  if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(100))) {
                    activeSymbols[5] = {dotFilledBitmap, now + displayDuration};
                    xSemaphoreGive(symbolsMutex);
                  }
                }
                xSemaphoreGive(ledMutex);
              }
              break;
          }
          xSemaphoreGive(serialMutex);
          xSemaphoreGive(symbolsMutex);
        }
      } else {
        xSemaphoreGive(i2cMutex);
      }
    }else {
    Serial.println("[Gesture] Échec prise i2cMutex");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void autoFanTask(void *pvParameter) {
  int tempValue;
  const unsigned char fanIndicator [] PROGMEM = {
    0x01, 0xf8, 0x00, 0x07, 0x0e, 0x00, 0x0d, 0xc3, 0x00, 0x1b, 0xe1, 0x80, 0x11, 0xe0, 0x80, 0x30, 
	  0xe0, 0xc0, 0x30, 0x66, 0xc0, 0x30, 0x7f, 0xc0, 0x33, 0xff, 0xc0, 0x33, 0xde, 0xc0, 0x17, 0xc0, 
	  0x80, 0x1b, 0x81, 0x80, 0x0f, 0x83, 0x00, 0x06, 0x06, 0x00, 0x03, 0xfc, 0x00, 0x00, 0x00, 0x00, 
	  0x00, 0xf0, 0x00, 0x01, 0xf8, 0x00, 0x07, 0xfe, 0x00, 0x07, 0xfe, 0x00
  };
  
  for (;;) { 
    xQueueReceive(tempQueue, &tempValue, portMAX_DELAY);    
    
    if (tempValue >= 33) {
      //SerialBT.print ("Fan on?"); 
      digitalWrite(FAN,HIGH);
      FANStatus = true;
      display.drawBitmap(2, 36, fanIndicator, 20, 20, WHITE);
    }
    else if (tempValue < 33) {
      //SerialBT.print ("Fan off?");
      digitalWrite(FAN,LOW);
      FANStatus = false; 
      display.setCursor(6, 38);
      display.print("-");
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

void indicatorDisplay(void *pvParameters) {
  int tempValue;
  uint32_t lastUpdate = 0;
  //const TickType_t xDelay = pdMS_TO_TICKS(200);
  
  // Bitmap stocké en PROGMEM
  const unsigned char lightIndicator [] PROGMEM = {
    0x00, 0x60, 0x00, 0x01, 0xfc, 0x00, 0x07, 0xc6, 0x00, 0x07, 0xc2, 0x00, 0x0f, 0xf1, 0x00, 0x0f, 
  	0xf1, 0x00, 0x0f, 0xfb, 0x00, 0x0f, 0xff, 0x00, 0x0f, 0xff, 0x00, 0x07, 0xfe, 0x00, 0x07, 0xfe, 
  	0x00, 0x03, 0xfc, 0x00, 0x01, 0xfc, 0x00, 0x01, 0xf8, 0x00, 0x01, 0x00, 0x00, 0x01, 0x80, 0x00, 
  	0x01, 0xf8, 0x00, 0x01, 0xf8, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00
  };

  const unsigned char fanIndicator [] PROGMEM = {
    0x01, 0xf8, 0x00, 0x07, 0x0e, 0x00, 0x0d, 0xc3, 0x00, 0x1b, 0xe1, 0x80, 0x11, 0xe0, 0x80, 0x30, 
	  0xe0, 0xc0, 0x30, 0x66, 0xc0, 0x30, 0x7f, 0xc0, 0x33, 0xff, 0xc0, 0x33, 0xde, 0xc0, 0x17, 0xc0, 
	  0x80, 0x1b, 0x81, 0x80, 0x0f, 0x83, 0x00, 0x06, 0x06, 0x00, 0x03, 0xfc, 0x00, 0x00, 0x00, 0x00, 
	  0x00, 0xf0, 0x00, 0x01, 0xf8, 0x00, 0x07, 0xfe, 0x00, 0x07, 0xfe, 0x00
  };

  for(;;) {
    if(xTaskGetTickCount() - lastUpdate > 5000) {
      Serial.println("[WATCHDOG] Display task alive");
      lastUpdate = xTaskGetTickCount();
    }
    if(xQueueReceive(tempQueue, &tempValue, portMAX_DELAY)) {
      if(xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) { 
        if(xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100))) {
          display.clearDisplay();
          
          // Affichage température
          display.setTextSize(2);
          display.setCursor(0, 10);
          display.printf("Temp: %d", tempValue);
          display.print((char)247);
          display.print("C");

          // Affichage état LED
          if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(50))) {
            if(ledBrightness > 0) {
              display.drawBitmap(28, 36, lightIndicator, 20, 20, WHITE);
            } else {
              display.setCursor(32, 38);
              display.print("-");
            }
            xSemaphoreGive(ledMutex);
          }

          // Affichage symboles gestes
          uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
          display.setCursor(58, 40);
          display.print("-");
          if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(50))) {
              for(int i = 0; i < 6; i++) {
                if(activeSymbols[i].bitmap && (now < activeSymbols[i].displayUntil)) {
                  display.drawBitmap(56, 36, activeSymbols[i].bitmap, 8, 8, WHITE);
                }
              }
            xSemaphoreGive(symbolsMutex);
          }

          // Affichage état LED
          if(xSemaphoreTake(fanMutex, pdMS_TO_TICKS(50))) {
            if(fanState) {
              display.drawBitmap(2, 36, fanIndicator, 20, 20, WHITE);
            } else {
              display.setCursor(6, 38);
              display.print("-");
            }
            xSemaphoreGive(fanMutex);
          }

          display.display();
          xSemaphoreGive(displayMutex);
        }
        xSemaphoreGive(i2cMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void introDisplay() {
  display.clearDisplay();

  display.setTextColor(WHITE);                                // Set the color
  display.setTextSize(2);                                     // Set the font size
  display.setCursor(6,10);                                    // Set the cursor coordinates
  display.print("Smart Home");
  display.setCursor(6,40);
  display.print("Automation");

  display.display();
  vTaskDelay(pdMS_TO_TICKS(4000));
  display.clearDisplay();
}