#include <Wire.h>
#include <DHT.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_SSD1306.h>
//#include <thingProperties.h>
#include <atomic>
#include "LittleFS.h"

#undef ERROR
#include <SparkFun_APDS9960.h>

// Configuration matérielle
//#define DHTPIN 33
//#define DHTTYPE DHT11
#define LED_PIN 5
#define FAN 26
//#define SDA_PIN 21
//#define SCL_PIN 22
//#define OLED_WIDTH 128
//#define OLED_HEIGHT 64
#define OLED_ADDRESS 0x3C
//#define OLED_RESET 4

// Bitmaps PROGMEM
const unsigned char arrowUpBitmap[] PROGMEM = {0x18,0x3C,0x7E,0xFF,0xFF,0x3C,0x3C,0x3C};
const unsigned char arrowDownBitmap[] PROGMEM = {0x3C,0x3C,0x3C,0xFF,0xFF,0x7E,0x3C,0x18};
const unsigned char arrowLeftBitmap[] PROGMEM = {0x18,0x30,0x7F,0xFF,0xFF,0x7F,0x30,0x18};
const unsigned char arrowRightBitmap[] PROGMEM = {0x18,0x0C,0xFE,0xFF,0xFF,0xFE,0x0C,0x18};
const unsigned char dotFilledBitmap[] PROGMEM = {0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C};
const unsigned char dotEmptyBitmap[] PROGMEM = {0x00,0x3C,0x42,0x42,0x42,0x3C,0x00,0x00};
const unsigned char lightIndicator[] PROGMEM = {
  0x00,0x60,0x00,0x01,0xfc,0x00,0x07,0xc6,0x00,0x07,0xc2,0x00,0x0f,0xf1,0x00,0x0f,
  0xf1,0x00,0x0f,0xfb,0x00,0x0f,0xff,0x00,0x0f,0xff,0x00,0x07,0xfe,0x00,0x07,0xfe,
  0x00,0x03,0xfc,0x00,0x01,0xfc,0x00,0x01,0xf8,0x00,0x01,0x00,0x00,0x01,0x80,0x00,
  0x01,0xf8,0x00,0x01,0xf8,0x00,0x00,0xf0,0x00,0x00,0x00,0x00
};
const unsigned char fanIndicator[] PROGMEM = {
  0x01,0xf8,0x00,0x07,0x0e,0x00,0x0d,0xc3,0x00,0x1b,0xe1,0x80,0x11,0xe0,0x80,0x30,
  0xe0,0xc0,0x30,0x66,0xc0,0x30,0x7f,0xc0,0x33,0xff,0xc0,0x33,0xde,0xc0,0x17,0xc0,
  0x80,0x1b,0x81,0x80,0x0f,0x83,0x00,0x06,0x06,0x00,0x03,0xfc,0x00,0x00,0x00,0x00,
  0x00,0xf0,0x00,0x01,0xf8,0x00,0x07,0xfe,0x00,0x07,0xfe,0x00
};

struct GestureSymbol {
  const unsigned char* bitmap;
  uint32_t displayUntil;
};

// Objets capteurs
DHT dht(33, DHT11);
AsyncWebServer server(80);
SparkFun_APDS9960 gestureSensor;
Adafruit_SSD1306 display(128, 64, &Wire, 4);
GestureSymbol activeSymbols[6];

// Variables atomiques
std::atomic<float> temperature{0};
std::atomic<float> humidity{0};
std::atomic<int> ledBrightness{0};
std::atomic<bool> ledState{false};
std::atomic<bool> fanState{false};
std::atomic<bool> tempAlert{false};
std::atomic<bool> automaticMode{true};

// Mutex
SemaphoreHandle_t i2cMutex, displayMutex, serialMutex, symbolsMutex;

// Prototypes
void introDisplay();
void dhtTask(void *pvParameters);
void gestureTask(void *pvParameters);
void indicatorDisplay(void *pvParameters);
void autoFanTask(void *pvParameters);
void setupServer();
String readDHTTemperature();
String readDHTHumidity();
String processor(const String& var);

// Ajoutez cette section HTML avant setup()
/*
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css">
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 800px;
      margin: 0 auto;
      padding: 20px;
      background-color: #f5f5f5;
    }
    .card {
      background: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 2px 5px rgba(0,0,0,0.1);
      margin-bottom: 20px;
    }
    .sensor-data {
      display: flex;
      justify-content: space-around;
      margin: 20px 0;
    }
    .sensor-item {
      text-align: center;
    }
    .mode-control {
      text-align: center;
      margin-top: 30px;
    }
    .button {
      padding: 12px 25px;
      margin: 10px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      font-size: 16px;
      transition: all 0.3s;
    }
    .auto-btn {
      background-color: %AUTOCOLOR%;
      color: white;
    }
    .manual-btn {
      background-color: %MANUALCOLOR%;
      color: white;
    }
    .current-mode {
      font-size: 18px;
      color: #666;
      margin-top: 15px;
    }
    .icon {
      font-size: 24px;
      margin-right: 10px;
    }
  </style>
  <script>
    function setMode(mode) {
      fetch('/mode?mode=' + mode)
        .then(response => response.text())
        .then(data => {
          if(data === 'OK') {
            location.reload();
          }
        });
    }
  </script>
</head>
<body>
  <div class="card">
    <h1><i class="fas fa-thermometer-half icon"></i>Station Météo Intelligente</h1>
    
    <div class="sensor-data">
      <div class="sensor-item">
        <i class="fas fa-temperature-high" style="color: #059e8a; font-size: 32px;"></i>
        <h2>Température</h2>
        <p style="font-size: 24px;">%TEMPERATURE% °C</p>
      </div>
      
      <div class="sensor-item">
        <i class="fas fa-tint" style="color: #00add6; font-size: 32px;"></i>
        <h2>Humidité</h2>
        <p style="font-size: 24px;">%HUMIDITY% %%</p>
      </div>
    </div>

    <div class="mode-control">
      <button class="button auto-btn" onclick="setMode('auto')">
        <i class="fas fa-robot"></i> Mode Automatique
      </button>
      
      <button class="button manual-btn" onclick="setMode('manual')">
        <i class="fas fa-hand-paper"></i> Mode Manuel
      </button>
      
      <p class="current-mode">
        Mode actuel : <strong>%MODE%</strong>
      </p>
    </div>
  </div>
</body>
</html>)rawliteral";*/


void setup() {
  Serial.begin(115200);
  //while(!Serial); // Attendre connection Serial
  
  // Initialisation matérielle
  Wire.begin(21, 22);
  Wire.setClock(100000);

  // Init OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Erreur OLED!");
    while(1);
  }
  introDisplay();

  

  // Init DHT
  dht.begin();
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN, OUTPUT);

  // Init capteur gestes
  // gestureSensor.init();
  // gestureSensor.enableGestureSensor(true);
  // gestureSensor.setGestureLEDDrive(LED_DRIVE_100MA);
  // gestureSensor.setGestureGain(GGAIN_4X);

  // Mutex
  i2cMutex = xSemaphoreCreateMutex();
  displayMutex = xSemaphoreCreateMutex();
  serialMutex = xSemaphoreCreateMutex();
  symbolsMutex = xSemaphoreCreateMutex();

  memset(activeSymbols, 0, sizeof(activeSymbols));

  if(!i2cMutex || !displayMutex || !serialMutex || !symbolsMutex) {
    Serial.println("Erreur création mutex!");
    while(1);
  }

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

  // Tâches
  xTaskCreatePinnedToCore(dhtTask, "DHT", 4096, NULL, 1, NULL, APP_CPU_NUM);
  xTaskCreatePinnedToCore(gestureTask, "Gestes", 4096, NULL, 1, NULL, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(indicatorDisplay, "OLED", 4096, NULL, 2, NULL, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(autoFanTask, "AutoFan", 2048, NULL, 1, NULL, PRO_CPU_NUM);

  // WiFi
  WiFi.begin("SECRET_SSID", "SECRET_OPTIONAL_PASS");
  Serial.print("Connexion WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nConnecté! IP: ");
  Serial.println(WiFi.localIP());

  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  setupServer();
  server.begin();
  
}

void loop() { vTaskDelete(NULL); }

// Tâches ---------------------------------------------------------------------
void dhtTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const int maxRetries = 5;
  
  for(;;) {
    int retry = 0;
    float h, t;
    
    do {
      h = dht.readHumidity();
      t = dht.readTemperature();
      retry++;
    } while((isnan(h) || isnan(t)) && (retry < maxRetries));

    if(retry < maxRetries) {
      temperature.store(t);
      humidity.store(h);
      tempAlert.store(t >= 33.0);
      
      if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(50))) {
        Serial.print("Temp: ");
        Serial.print(t);
        Serial.print("C | Hum: ");
        Serial.print(h);
        Serial.println("%");
        xSemaphoreGive(serialMutex);
      }
    } else {
      Serial.println("[ERREUR] Capteur DHT non répondant");
    }
    
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
  }
}

const char* PARAM_MODE = "mode";
void autoFanTask(void *pvParameters) {
  for(;;) {
    if(automaticMode.load()) {
      float t = temperature.load();
      
      if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100))) {
        if(t > 33.0 && !fanState.load()) {
          digitalWrite(FAN, HIGH);
          fanState.store(true);
          Serial.println("[AUTO] Ventilateur ACTIVÉ");
        }
        else if(t <= 30.0 && fanState.load()) {
          digitalWrite(FAN, LOW);
          fanState.store(false);
          Serial.println("[AUTO] Ventilateur DÉSACTIVÉ");
        }
        xSemaphoreGive(serialMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


void gestureTask(void *pvParameters) {
  uint8_t gesture;
  const uint32_t displayDuration = 2000;
  
  for(;;) {
    if(xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
      if(gestureSensor.isGestureAvailable()) {
        gesture = gestureSensor.readGesture();
        xSemaphoreGive(i2cMutex);
        uint32_t now = millis();
        bool currentLedState = ledState.load(std::memory_order_acquire);

        if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100))) {
          switch(gesture) {
            case DIR_UP:
              if(!automaticMode.load()) { 
                digitalWrite(FAN, HIGH);
                fanState.store(true);
                Serial.println("↑ FAN ON");
                activeSymbols[0] = {arrowUpBitmap, now + displayDuration};
              }
              break;
              
            case DIR_DOWN:
              if(!automaticMode.load()) {
                digitalWrite(FAN, LOW);
                fanState.store(false);
                Serial.println("↓ FAN OFF");
                activeSymbols[1] = {arrowDownBitmap, now + displayDuration};
              }
              break;

            case DIR_LEFT:
              analogWrite(LED_PIN, 0);
              ledBrightness.store(0);
              ledState.store(false);
              Serial.println("← LED OFF");
              activeSymbols[2] = {arrowLeftBitmap, now + displayDuration};
              break;

            case DIR_RIGHT:
              analogWrite(LED_PIN, 255);
              ledBrightness.store(255);
              ledState.store(true);
              Serial.println("→ LED ON");
              activeSymbols[3] = {arrowRightBitmap, now + displayDuration};
              break;

            case DIR_NEAR:
              if(currentLedState) { // Vérification atomique
                analogWrite(LED_PIN, 50);
                ledBrightness.store(50);
                ledBrightness.store(50,  std::memory_order_release);
                Serial.println("≈ LED 20%");
                activeSymbols[4] = {dotEmptyBitmap, now + displayDuration};
              } else {
                Serial.println("[ERREUR] Utilisez → pour activer la LED");
              }
              break;

            case DIR_FAR:
              if(currentLedState) { // Vérification atomique
                analogWrite(LED_PIN, 255);
                ledBrightness.store(255);
                ledBrightness.store(255,  std::memory_order_release);
                Serial.println("≈ LED 100%");
                activeSymbols[5] = {dotFilledBitmap, now + displayDuration};
              } else {
                Serial.println("[ERREUR] Utilisez → pour activer la LED");
              }
              break;
          }
          xSemaphoreGive(serialMutex);
        }
      } else {
        xSemaphoreGive(i2cMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void indicatorDisplay(void *pvParameters) {
  uint32_t lastUpdate = 0;
  for(;;) {
    if(xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100))) {
      display.clearDisplay();
      
      // Température
      display.setTextSize(2);
      display.setCursor(0, 10);
      //display.printf("Temp: %.1fC", temperature.load());
      display.printf("Temp: %d", (int)temperature.load());
      display.print((char)247);
      display.print("C");

      // Humidité
      // display.setCursor(0, 35);
      // display.printf("Hum:  %.1f%%", humidity.load());

      // État LED
      if(ledState) display.drawBitmap(28, 36, lightIndicator, 20, 20, WHITE);
      // display.setCursor(28, 55);
      // display.printf("%d%%", (ledBrightness.load() * 100) / 255);

      // Ventilateur
      if(fanState.load()) {
        display.drawBitmap(2, 36, fanIndicator, 20, 20, WHITE);
      }

      uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      display.setCursor(58, 36);
      if(xSemaphoreTake(symbolsMutex, pdMS_TO_TICKS(50))) {
          for(int i = 0; i < 6; i++) {
            if(activeSymbols[i].bitmap && (now < activeSymbols[i].displayUntil)) {
              display.drawBitmap(56, 36, activeSymbols[i].bitmap, 8, 8, WHITE);
            }
          }
        xSemaphoreGive(symbolsMutex);
      }

      display.display();
      xSemaphoreGive(displayMutex);
    }
    
    if(millis() - lastUpdate > 5000) {
      Serial.println("[STATUS] System OK");
      lastUpdate = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// Helpers ---------------------------------------------------------------------
void introDisplay() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(10, 20);
  display.print("Systeme");
  display.setCursor(20, 40);
  display.print("Pret!");
  display.display();
  delay(2000);
  display.clearDisplay();
  display.display();
}

String readDHTTemperature() {
  return String(temperature.load());
}

String readDHTHumidity() {
  return String(humidity.load());
}

String processor(const String& var) {
  if(var == "MODE") return automaticMode.load() ? "Automatique" : "Manuel";
  //if(var == "AUTOCOLOR") return automaticMode.load() ? "#4CAF50" : "#808080";
  //if(var == "MANUALCOLOR") return automaticMode.load() ? "#808080" : "#2196F3";
  if(var == "TEMPERATURE") return readDHTTemperature();
  if(var == "HUMIDITY") return readDHTHumidity();
  return String();
}

void setupServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    //request->send_P(200, "text/html", index_html, processor);
    request->send(LittleFS, "/index.html", "text/html", processor);
  });

  server.on("/mode", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("mode")) {
      String mode = request->getParam("mode")->value();
      automaticMode.store(mode == "auto");
      
      if(automaticMode.load()) {
        digitalWrite(FAN, LOW);
        fanState.store(false);
      }
      request->send(200, "text/plain", "OK");
    }
  });
}