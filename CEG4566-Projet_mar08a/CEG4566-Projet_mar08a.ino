#include <Wire.h>
#include <DHT.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_SSD1306.h>
#include <thingProperties.h>
#include <atomic>
#include "LittleFS.h"

#undef ERROR
#include <SparkFun_APDS9960.h>

// Configuration matérielle
#define DHTPIN 33
#define DHTTYPE DHT11
#define LED_PIN 5
#define FAN 26
#define SDA_PIN 21
#define SCL_PIN 22
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDRESS 0x3C
#define OLED_RESET 4
#define APDS9960_I2C_ADDR 0x39 // Adresse I2C du capteur de gestes

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
DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
SparkFun_APDS9960 gestureSensor;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
GestureSymbol activeSymbols[6];

// Variables atomiques
std::atomic<float> temperature{0};
std::atomic<float> humidity{0};
std::atomic<int> ledBrightness{0};
std::atomic<bool> ledState{false};
std::atomic<bool> fanState{false};
std::atomic<bool> tempAlert{false};
std::atomic<bool> automaticMode{true};
std::atomic<int> fanSpeed{0};

// Mutex
SemaphoreHandle_t i2cMutex, displayMutex, serialMutex, symbolsMutex;

// Prototypes
void introDisplay();
void dhtTask(void *pvParameters);
void gestureTask(void *pvParameters);
void indicatorDisplay(void *pvParameters);
void checkComponents();
void autoFanTask(void *pvParameters);
void setupServer();
String readDHTTemperature();
String readDHTHumidity();
String processor(const String& var);

/*
// Ajoutez cette section HTML avant setup()
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

     function setFanState(state) {
      fetch('/fan?state=' + state)
        .then(response => response.text())
        .then(data => location.reload());
    }

    function setLedState(state) {
      fetch('/led?state=' + state)
        .then(response => response.text())
        .then(data => location.reload());
    }
  </script>
</head>
<body>
  <div class="card">
    <h1><i class="fas fa-thermometer-half icon"></i>Station Meteo Intelligente</h1>
    
    <div class="sensor-data">
      <div class="sensor-item">
        <i class="fas fa-temperature-high" style="color: #059e8a; font-size: 32px;"></i>
        <h2>Temperature</h2>
        <p style="font-size: 24px;">%TEMPERATURE% °C</p>
      </div>
      
      <div class="sensor-item">
        <i class="fas fa-tint" style="color: #00add6; font-size: 32px;"></i>
        <h2>Humidite</h2>
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

    <div style="margin-top: 20px;">
        <h3>Controle Ventilateur</h3>
        <button class="button" style="background-color: %FANCOLOR%" 
          onclick="setFanState('on')">ON</button>
        <button class="button" style="background-color: #cccccc" 
          onclick="setFanState('off')">OFF</button>
    </div>

    <div style="margin-top: 20px;">
      <h3>Controle LED</h3>
      <button class="button" style="background-color: %LEDCOLOR%" 
        onclick="setLedState('on')">ON</button>
      <button class="button" style="background-color: #cccccc" 
        onclick="setLedState('off')">OFF</button>
    </div>
  </div>
</body>
</html>)rawliteral";
*/

void setup() {
  Serial.begin(115200);
  while(!Serial); // Attendre connection Serial
  
  // Initialisation matérielle
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  // Init OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
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
  WiFi.begin(SECRET_SSID, SECRET_OPTIONAL_PASS);
  Serial.print("Connexion WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnecté! IP: " + WiFi.localIP().toString());

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
  uint32_t lastCheck = 0;
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

    // Gestion des temporisations séparées
    uint32_t currentTime = millis();
    
    if(currentTime - lastUpdate > 5000) {
      Serial.println("[STATUS] System OK");
      lastUpdate = millis();
    }

     if(currentTime - lastCheck > 10000) {
      checkComponents();
      lastCheck = currentTime;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void checkComponents() {
  bool oledOk = false;
  bool fanOk = false;
  bool apdsOk = false;

  // Vérification OLED
  if(xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
    Wire.beginTransmission(OLED_ADDRESS);
    oledOk = (Wire.endTransmission() == 0);
    xSemaphoreGive(i2cMutex);
  }

  // Vérification Ventilateur
  int fanStatePin = digitalRead(FAN);
  bool expectedFanState = fanState.load();
  fanOk = (fanStatePin == (expectedFanState ? HIGH : LOW));

  // Vérification Capteur Gestes (APDS-9960)
  if(xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
    Wire.beginTransmission(0x39); // Adresse I2C de l'APDS-9960
    apdsOk = (Wire.endTransmission() == 0);
    xSemaphoreGive(i2cMutex);
  }

  // Affichage des résultats
  if(xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100))) {
    Serial.println("\n[VERIFICATION COMPOSANTS]");
    Serial.print("OLED: ");
    Serial.println(oledOk ? "Fonctionnel" : "ERREUR - Non detecte");
    Serial.print("Ventilateur: ");
    Serial.println(fanOk ? "Etat correct" : "ERREUR - Incoherence etat");
    Serial.print("Capteur Gestes: ");
    Serial.println(apdsOk ? "Connecte" : "ERREUR - Non detecte");
    Serial.println();
    xSemaphoreGive(serialMutex);
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
  if(var == "AUTOCOLOR") return automaticMode.load() ? "#4CAF50" : "#808080";
  if(var == "MANUALCOLOR") return automaticMode.load() ? "#808080" : "#2196F3";
  if(var == "FANCOLOR") return fanState.load() ? "#4CAF50" : "#cccccc";
  if(var == "LEDCOLOR") return ledState.load() ? "#FFD700" : "#cccccc";
  if(var == "TEMPERATURE") return readDHTTemperature();
  if(var == "HUMIDITY") return readDHTHumidity();
  return String();
}

String currentMode = "Automatic";  // default
void setupServer() {
  server.serveStatic("/images", LittleFS, "/images");

  // Serve ESP32 internal web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
  request->send(LittleFS, "/index.html", "text/html", false, processor);
  });

   server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });

  // Temperature for external app
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", readDHTTemperature());
  });

  // Humidity for external app
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", readDHTHumidity());
  });

  // Handle mode switching (for ESP32 web interface)
  server.on("/mode", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("mode")) {
      String mode = request->getParam("mode")->value();

      // Appliquer le changement sur la variable logique
      automaticMode.store(mode == "auto");

      // Enregistrer aussi dans currentMode pour l’interface
      currentMode = mode;

      // Éteindre le ventilateur si on repasse en automatique
      if (automaticMode.load()) {
        digitalWrite(FAN, LOW);
        fanState.store(false);
      }

      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Missing mode param");
    }
  });

  server.on("/currentmode", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", currentMode);
});

  // Get LED state and brightness
  server.on("/led/state", HTTP_GET, [](AsyncWebServerRequest *request){
    String state = ledState.load() ? "on" : "off";
    int brightness = ledBrightness.load();
    request->send(200, "application/json", "{\"state\":\"" + state + "\",\"brightness\":" + brightness + "}");
  });
  Serial.println("Route /led/state chargée");

  // ESP32 web UI uses GET for LED toggle
  server.on("/led", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("state")) {
      String state = request->getParam("state")->value();
      bool on = (state == "on");

      
      if (on && ledBrightness.load() == 0) {
        ledBrightness.store(128); // Valeur par défaut
      }

      analogWrite(LED_PIN, on ? ledBrightness.load() : 0);
      ledState.store(on);

      request->send(200, "text/plain", "OK");
    }
  });

  server.on("/fan/state", HTTP_GET, [](AsyncWebServerRequest *request){
    String state = fanState.load() ? "on" : "off";
    int speed = fanSpeed.load();  // use the stored value instead of analogRead
    request->send(200, "application/json", "{\"state\":\"" + state + "\",\"speed\":" + speed + "}");
  });

  // ESP32 web UI uses GET for Fan toggle (optional)
  server.on("/fan", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("state")) {
      String state = request->getParam("state")->value();
      bool on = (state == "on");
      digitalWrite(FAN, on ? HIGH : LOW);
      fanState.store(on);
      request->send(200, "text/plain", "OK");
    }
  });

  // External App: Toggle LED (POST)
  server.on("/led", HTTP_POST, [](AsyncWebServerRequest *request){
  if (request->hasParam("state", true)) {
    String state = request->getParam("state", true)->value();
    bool on = (state == "on");

    if (on && ledBrightness.load() == 0) {
      ledBrightness.store(128); // Valeur par défaut
    }

    analogWrite(LED_PIN, on ? ledBrightness.load() : 0);
    ledState.store(on);

    request->send(200, "text/plain", "LED " + state);
  }
});

  // External App: Set LED brightness
  server.on("/led/intensity", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      int val = request->getParam("value", true)->value().toInt();
      ledBrightness.store(val);
      if (ledState.load()) {
        analogWrite(LED_PIN, val);
      }
      request->send(200, "text/plain", "LED Intensity: " + String(val));
    }
  });

  // External App: Toggle Fan (POST)
  server.on("/fan", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("state", true)) {
      String state = request->getParam("state", true)->value();
      bool on = (state == "on");
      digitalWrite(FAN, on ? HIGH : LOW);
      fanState.store(on);
      request->send(200, "text/plain", "Fan " + state);
    }
  });

  // External App: Set Fan speed
  server.on("/fan/intensity", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      int val = request->getParam("value", true)->value().toInt();
      fanSpeed.store(val);  // store the speed
      digitalWrite(FAN, val); // using ledcWrite if you're using PWM setup
      request->send(200, "text/plain", "Fan Intensity: " + String(val));
    }
  });
  
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

}
