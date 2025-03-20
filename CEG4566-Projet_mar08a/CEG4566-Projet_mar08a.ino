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

// Objets capteurs
DHT dht(DHTPIN, DHTTYPE);
SparkFun_APDS9960 gestureSensor;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

static QueueHandle_t tempQueue;

// Variables partagées
volatile bool ledState = false;
//float temperature = 0.0;
//float humidity = 0.0;

// Sémaphores
SemaphoreHandle_t i2cMutex;
SemaphoreHandle_t ledMutex;
SemaphoreHandle_t serialMutex;
SemaphoreHandle_t displayMutex;

void dhtTask(void *pvParameters);
void gestureTask(void *pvParameters);

/* Status for OLED display indicators */
bool LEDStatus = false;


void setup() {
  Serial.begin(115200);
  
  // Initialisation I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialisation capteurs
  dht.begin();
  pinMode(LED_PIN, OUTPUT);

  // Initialisation OLED
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS); 

  /* OLED display at start */
  introDisplay();

  // Création des sémaphores
  i2cMutex = xSemaphoreCreateMutex();
  ledMutex = xSemaphoreCreateMutex();
  serialMutex = xSemaphoreCreateMutex();

  // Configuration capteur de gestes
  if(xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
    if(!gestureSensor.init() || !gestureSensor.enableGestureSensor(true)) {
      Serial.println("ERREUR: Init capteur gestes");
      while(1);
    }
    gestureSensor.setGestureLEDDrive(LED_DRIVE_100MA);
    gestureSensor.setGestureGain(GGAIN_4X);
    xSemaphoreGive(i2cMutex);
  }

  tempQueue  = xQueueCreate(5, sizeof(int));

  displayMutex = xSemaphoreCreateMutex();

  // Création des tâches
  xTaskCreatePinnedToCore(
    dhtTask,
    "DHT_Task",
    2048,
    NULL,
    1,
    NULL,
    APP_CPU_NUM
  );

  xTaskCreatePinnedToCore(
    gestureTask,
    "Gesture_Task",
    2048,
    NULL,
    2,
    NULL,
    PRO_CPU_NUM
  );

  xTaskCreatePinnedToCore(
    indicatorDisplay,
    "DisplayTask",
    4096,
    NULL,
    3,
    NULL,
    PRO_CPU_NUM
  );

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
    int temperatureInt = (int)round(t);

    if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100))) {
      if(!isnan(h) && !isnan(t)) {
        xQueueSend(tempQueue, &temperatureInt, 0);
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
  
  for(;;) {
    if(xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
      if(gestureSensor.isGestureAvailable()) {
        gesture = gestureSensor.readGesture();
        xSemaphoreGive(i2cMutex);
        
        if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100))) {
          switch(gesture) {
            case DIR_UP:
              Serial.println("↑ Geste haut");
              break;
              
            case DIR_DOWN:
              Serial.println("↓ Geste bas");
              break;
              
            case DIR_LEFT:
              if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                digitalWrite(LED_PIN, LOW);
                ledState = false;
                Serial.println("← LED OFF");
                xSemaphoreGive(ledMutex);
              }
              break;
              
            case DIR_RIGHT:
              if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                digitalWrite(LED_PIN, HIGH);
                ledState = true;
                Serial.println("→ LED ON");
                xSemaphoreGive(ledMutex);
              }
              break;

            case DIR_NEAR:
              if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                analogWrite(LED_PIN, 50); // 50% (255/2 ≈ 127)
                Serial.println("≈ Proche: 50%");
                xSemaphoreGive(ledMutex);
              }
              break;
              
            case DIR_FAR:
               if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {
                ledcWrite(LED_PIN, 255); // 100%
                Serial.println("≈ Loin: 100%");
                xSemaphoreGive(ledMutex);
              }
              break;
          }
          xSemaphoreGive(serialMutex);
        }
      } else {
        xSemaphoreGive(i2cMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
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
            if(ledState) {
              display.drawBitmap(28, 36, lightIndicator, 20, 20, WHITE);
            } else {
              display.setCursor(32, 38);
              display.print("-");
            }
            xSemaphoreGive(ledMutex);
          }

          display.display();
          xSemaphoreGive(displayMutex);
        }
        xSemaphoreGive(i2cMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void introDisplay() {
  display.clearDisplay();
  
  const unsigned char intro [] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x7b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0xde, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xe3, 0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 0xc7, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xe3, 0xbf, 0x00, 0x07, 0xe0, 0x00, 0xfd, 0xc7, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0xbf, 0x00, 0x0f, 0xf0, 0x00, 0xfd, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0xbf, 0x00, 0x1e, 0x78, 0x00, 0xfd, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0xb8, 0x00, 0x7c, 0x3e, 0x00, 0x1d, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0xb8, 0x00, 0xf0, 0x0f, 0x00, 0x1d, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0xb8, 0x03, 0xe0, 0x07, 0xc0, 0x1d, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x07, 0xc0, 0x03, 0xe0, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0f, 0x00, 0x00, 0xf0, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x3e, 0x00, 0x00, 0x7c, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x7c, 0x00, 0x00, 0x3e, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0xfe, 0x00, 0x00, 0x7f, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0xfe, 0x00, 0x00, 0x7f, 0x01, 0xdb, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xdb, 0x80, 0x0e, 0x07, 0xe0, 0x70, 0x01, 0xfb, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xdf, 0x80, 0x0e, 0x1f, 0xf8, 0x70, 0x01, 0xdb, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xdf, 0x80, 0x0e, 0x3e, 0x7c, 0x70, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xdf, 0x80, 0x0e, 0x78, 0x1e, 0x70, 0x01, 0xdb, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xdf, 0x80, 0x0e, 0x73, 0xce, 0x70, 0x01, 0xfb, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xdf, 0x80, 0x0e, 0x67, 0xe6, 0x70, 0x01, 0xfb, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xcb, 0x80, 0x0e, 0x47, 0xe6, 0x70, 0x01, 0xfb, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0e, 0x06, 0x60, 0x70, 0x01, 0xd3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0e, 0x06, 0x60, 0x70, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0e, 0x06, 0x60, 0x70, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0e, 0x06, 0x60, 0x70, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0f, 0xe6, 0x67, 0xf0, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0f, 0xf6, 0x6f, 0xf0, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x0f, 0xf6, 0x6f, 0xf0, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x00, 0x06, 0x60, 0x00, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x00, 0x06, 0x7c, 0x00, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xc3, 0x80, 0x00, 0x06, 0x7f, 0xe0, 0x01, 0xc3, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xe3, 0x80, 0x00, 0x06, 0x77, 0xf0, 0x01, 0xc7, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xe3, 0x80, 0x00, 0x06, 0x67, 0x7e, 0x01, 0xc7, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0xf3, 0x80, 0x00, 0x06, 0x67, 0x7f, 0x01, 0xcf, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xfc, 0x3e, 0x67, 0x7f, 0x3f, 0xfe, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xfe, 0x7e, 0x67, 0x73, 0x3f, 0xfc, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xfc, 0xfe, 0x27, 0x73, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x07, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x02, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x00, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x00, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0xe6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x01, 0xf6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0xf6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  
  display.drawBitmap(0, 0, intro, 128, 64, WHITE);
  
  display.display();
  vTaskDelay(pdMS_TO_TICKS(4000));
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