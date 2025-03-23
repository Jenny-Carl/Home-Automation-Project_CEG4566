#include <Wire.h>
#include <DHT.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_SSD1306.h>
#include <thingProperties.h>
#include <atomic>

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

// Mutex
SemaphoreHandle_t i2cMutex, displayMutex, serialMutex;

// Prototypes
void introDisplay();
void dhtTask(void *pvParameters);
void gestureTask(void *pvParameters);
void indicatorDisplay(void *pvParameters);
String readDHTTemperature();
String readDHTHumidity();
String processor(const String& var);

// Ajoutez cette section HTML avant setup()
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css">
  <style>
    html {font-family: Arial; display: inline-block; margin: 0 auto; text-align: center;}
    h2 {font-size: 3.0rem;}
    p {font-size: 3.0rem;}
    .units {font-size: 1.2rem;}
    .dht-labels{font-size: 1.5rem; vertical-align:middle; padding-bottom: 15px;}
  </style>
</head>
<body>
  <h2>ESP32 DHT Server</h2>
  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="dht-labels">Temperature</span> 
    <span id="temperature">%TEMPERATURE%</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i> 
    <span class="dht-labels">Humidity</span>
    <span id="humidity">%HUMIDITY%</span>
    <sup class="units">&percnt;</sup>
  </p>
</body>
</html>)rawliteral";

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
  xTaskCreatePinnedToCore(dhtTask, "DHT", 4096, NULL, 2, NULL, APP_CPU_NUM);
  xTaskCreatePinnedToCore(gestureTask, "Gestes", 3072, NULL, 3, NULL, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(indicatorDisplay, "OLED", 4096, NULL, 4, NULL, PRO_CPU_NUM);

  // WiFi
  WiFi.begin(SECRET_SSID, SECRET_OPTIONAL_PASS);
  Serial.print("Connexion WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnecté! IP: " + WiFi.localIP().toString());

  // Serveur Web
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", readDHTTemperature());
  });
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", readDHTHumidity());
  });
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

void gestureTask(void *pvParameters) {
  uint8_t gesture;
  
  for(;;) {
    if(gestureSensor.isGestureAvailable()) {
      gesture = gestureSensor.readGesture();
      uint32_t now = millis();
      bool currentLedState = ledState.load(std::memory_order_acquire);
      
      switch(gesture) {
        case DIR_UP: 
          digitalWrite(FAN, HIGH);
          Serial.println("↑ FAN ON");
          break;
          
        case DIR_DOWN:
          digitalWrite(FAN, LOW);
          Serial.println("↓ FAN OFF");
          break;

        case DIR_LEFT:
          analogWrite(LED_PIN, 0);
          Serial.println("← LED OFF");
          ledState.store(false);
          break;

        case DIR_RIGHT:
          analogWrite(LED_PIN, 255);
          Serial.println("→ LED ON");
          ledState.store(true);
          break;

        case DIR_NEAR:
          if(currentLedState) { // Vérification atomique
            analogWrite(LED_PIN, 50);
            ledBrightness.store(50,  std::memory_order_release);
            Serial.println("≈ LED 20%");
          } else {
            Serial.println("[ERREUR] Utilisez → pour activer la LED");
          }
          break;

        case DIR_FAR:
          if(currentLedState) { // Vérification atomique
            analogWrite(LED_PIN, 255);
            ledBrightness.store(255,  std::memory_order_release);
            Serial.println("≈ LED 100%");
          } else {
            Serial.println("[ERREUR] Utilisez → pour activer la LED");
          }
          break;
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
      display.printf("Temp: %.1f", temperature.load());
      display.print((char)247);
      display.print("C");

      // Humidité
      // display.setCursor(0, 35);
      // display.printf("Hum:  %.1f%%", humidity.load());

      // État LED
      if(ledState) display.drawBitmap(28, 36, lightIndicator, 20, 20, WHITE);
      display.setCursor(28, 55);
      display.printf("%d%%", (ledBrightness.load() * 100) / 255);

      // Ventilateur
      if(fanState.load()) {
        display.drawBitmap(2, 36, fanIndicator, 20, 20, WHITE);
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
  return var == "TEMPERATURE" ? readDHTTemperature() : 
         var == "HUMIDITY" ? readDHTHumidity() : String();
}