#include "arduino_secrets.h"
/* 
  Sketch generated by the Arduino IoT Cloud Thing "CEG4566-Projet"
  https://create.arduino.cc/cloud/things/d376d5b6-1a92-4a59-b84e-4c591c6e9aa0 

  Arduino IoT Cloud Variables description

  The following variables are automatically generated and updated when changes are made to the Thing

  float humidity;
  float temperature;
  bool redLight_1;

  Variables which are marked as READ/WRITE in the Cloud Thing will also have functions
  which are called when their values are changed from the Dashboard.
  These functions are generated with the Thing and added at the end of this sketch.
*/

#include "thingProperties.h"
#include <DHT.h>
#include <DHT_U.h>
#include <AsyncTCP.h>
#include <gpio_viewer.h>

#define DHTPIN 18
#define DHTType DHT11

DHT dht(DHTPIN,DHTType);

int Pin_Led = 5;

void setup() {
  // Initialize serial and wait for port to open:
  Serial.begin(115200);
  // This delay gives the chance to wait for a Serial Monitor without blocking if none is found
  delay(1500); 

  // Defined in thingProperties.h
  initProperties();
  pinMode(Pin_Led,OUTPUT);

  dht.begin();

  // Connect to Arduino IoT Cloud
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  
  /*
     The following function allows you to obtain more information
     related to the state of network and IoT Cloud connection and errors
     the higher number the more granular information you’ll get.
     The default is 0 (only errors).
     Maximum is 4
 */
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();
}

void loop() {
  ArduinoCloud.update();
  // Your code here 
  
  delay(2000);

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  
}

/*
  Since RedLight1 is READ_WRITE variable, onRedLight1Change() is
  executed every time a new value is received from IoT Cloud.
*/
void onRedLight1Change()  {
  // Add your code here to act upon RedLight1 change
  Serial.println(redLight_1);

  if(redLight_1){
    digitalWrite(Pin_Led,HIGH);
  } else{
    digitalWrite(Pin_Led,LOW);
  }
}

