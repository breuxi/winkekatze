#include <FS.h>   // SPIFFS Filesystem

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <PubSubClient.h>

#include <stdlib.h>

#include <Adafruit_NeoPixel.h>

// WifiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

extern "C" {
#include "user_interface.h"
}

char mqtt_server[255] = "";
char mqtt_port[6] = "1883";
char mqtt_username[34] = "";
char mqtt_password[34] = "";
char cat_name[34] = "fridolin";

//original wink electronic, directly connected to D1

const int servoPin = 5; //Wemos D1 Mini: "D1"


// LEDs connected to GPIO5. They are running with a lower voltage (around 4.3V) so the 3.3V output level is enough to trigger high
const int LEDPin =  14;  //Wemos D1 Mini: "D5"
const int numLEDs = 2;

//Trigger pin for forced Setup
const int TRIGGER_PIN = 4; //Wemos D1 Mini: "D2"

//Timer, for changing color etc.
os_timer_t ledTimer;


Adafruit_NeoPixel pixels = Adafruit_NeoPixel(numLEDs, LEDPin, NEO_RGB + NEO_KHZ800);

auto RED = pixels.Color(255,0,0);
auto GREEN = pixels.Color(0, 255,0);
auto BLUE = pixels.Color(0, 0, 255);

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

//color stuff
uint32_t eyecolor = 0;
int ledmode = 0;
int blinkCounter = 0;
int blinkSpeed = 1;

int ledrBrightness = 255;
int ledgBrightness = 255;
int ledbBrightness = 255;
bool ledrFadein = true;
bool ledgFadein = true;
bool ledbFadein = true;
bool ledrPower = true;
bool ledgPower = true;
bool ledbPower = true;

void setup_wifi();
void callback(char* topic, byte* payload, unsigned int length);
void ledTimerCallback(void *pArg);

void setEyeColor(){
  for ( int i = 0; i < numLEDs; i++ ) {
    pixels.setPixelColor(i, pixels.Color(ledrBrightness*ledrPower, ledgBrightness*ledgPower, ledbBrightness*ledbPower));
  }
  pixels.show();
}


void eye_debug(uint32_t color) {
  for ( int i = 0; i < numLEDs; i++ ) {
    pixels.setPixelColor(i, color);
  }
  pixels.show();
};

//flag for saving data
bool shouldSaveConfig = false;

void configModeCallback(WiFiManager *myWiFiManager) {
  eye_debug(pixels.Color(255,0,255));
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void readConfig() {
  //clean FS, for testing
  //SPIFFS.format();

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(cat_name, json["cat_name"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

}

void setup() {
  // if we didn't use the serial output we could gain 2 GPIOs.
  Serial.begin(115200);

  Serial.write("monicat starts");

  eyecolor = pixels.Color(64, 64, 255);

  pixels.begin();

  eye_debug(GREEN);

  //turn wink mechanism off
  pinMode(servoPin, OUTPUT);
  digitalWrite(servoPin, LOW);

  //check if Setup is enforced (user pressed Button while booting...)
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    SPIFFS.format();
    ESP.eraseConfig();
  };

  readConfig();
  setup_wifi();

  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(callback);


  eye_debug(pixels.Color(0,255,255));
  delay(100);

  eye_debug(pixels.Color(0,0,0));
  //arm Timer
  os_timer_setfn(&ledTimer, ledTimerCallback, NULL);
  os_timer_arm(&ledTimer, 10, true);
}

void setup_wifi() {
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_username("mqtt_username", "mqtt username", mqtt_username, 34);
  WiFiManagerParameter custom_mqtt_password("mqtt_password", "mqtt password", mqtt_password, 34);
  WiFiManagerParameter custom_cat_name("cat_name", "the cat's name", cat_name, 32);


  WiFiManager wifiManager;


  eye_debug(BLUE);
  //set config save notify callback

  //also reset wifi settings when pressed...
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    wifiManager.resetSettings();
  };



  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setAPCallback(configModeCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_cat_name);

  wifiManager.setConfigPortalTimeout(600);

  if (!wifiManager.autoConnect("Winkekatze", "geheimgeheim")) {
    eye_debug(RED);
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(cat_name, custom_cat_name.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["cat_name"] = cat_name;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Callback! Topic:  ");
  Serial.print(String(topic));
  Serial.print("\n");

  if ( String(topic) == String(cat_name) + "/paw/command" ) {
    char* p = (char*)malloc(length + 1);
    p[length] = 0;

    // Copy the payload to the new buffer
    memcpy(p, payload, length);

    String winkstr(p);

    Serial.print("/paw/command Payload: ");
    Serial.print(winkstr);
    Serial.print("\n");
    if ( winkstr == "wink" ) {
      digitalWrite(servoPin, HIGH);
    } else if ( winkstr == "nowink" ) {
      digitalWrite(servoPin, LOW);
    }

    free(p);
  } else if ( String(topic) == String(cat_name) + "/command" || String(topic) == "winkekatze/allcats" ) {
    //TODO: do something that all cats need to do
  } else if ( String(topic) == String(cat_name)+"/eye/color/set" ) {
    char* p = (char*)malloc(length + 1);
    p[length] = 0;

    // Copy the payload to the new buffer
    memcpy(p, payload, length);

    String colstr(p);

    Serial.print("Color Payload: ");
    Serial.print(colstr);
    Serial.print("\n");
    if ( colstr == "pink" ) {
      ledrPower = true;
      ledgPower = false;
      ledbPower = true;
    } else if ( colstr == "red" ) {
      ledrPower = true;
      ledgPower = false;
      ledbPower = false;
    } else if ( colstr == "green" ) {
      ledrPower = false;
      ledgPower = true;
      ledbPower = false;
    } else if ( colstr == "blue" ) {
      ledrPower = false;
      ledgPower = false;
      ledbPower = true;
    } else if ( colstr == "cyan" ) {
      ledrPower = false;
      ledgPower = true;
      ledbPower = true;
    } else if ( colstr == "yellow" ) {
      ledrPower = true;
      ledgPower = true;
      ledbPower = false;
    } else if ( colstr == "dark" ) {
      ledrPower = false;
      ledgPower = false;
      ledbPower = false;
    } else if ( colstr == "white" ) {
      ledrPower = true;
      ledgPower = true;
      ledbPower = true;
    }

    //set color based on globals.
    setEyeColor();

    free(p);
  } else if ( String(topic) == String(cat_name)+"/eye/mode/set" ) {
    char* p = (char*)malloc(length + 1);
    p[length] = 0;

    // Copy the payload to the new buffer
    memcpy(p, payload, length);

    String modestr(p);

    Serial.print("Mode Payload: ");
    Serial.print(modestr);
    Serial.print("\n");
    if ( modestr == "solid" ) {
      //set solid colors
      ledmode = 0 ;
      // ensure full brigthness
      ledrBrightness = 255;
      ledgBrightness = 255;
      ledbBrightness = 255;
      //set color based on globals.
      setEyeColor();
    } else if ( modestr == "warp" ) {
      ledmode = 1;
      // ensure zero brigthness at start.
      ledrBrightness = 0;
      ledgBrightness = 0;
      ledbBrightness = 0;
      //eyecolor will be set in ledTimer callback, not here.
    } else if ( modestr == "blink" ) {
      ledmode = 2;
      //eyecolor will be set in ledTimer callback, not here.
    }

    free(p);
  } else if ( String(topic) == String(cat_name)+"/eye/speed/set" ) {
    char* p = (char*)malloc(length + 1);
    p[length] = 0;

    // Copy the payload to the new buffer
    memcpy(p, payload, length);

    String speedstr(p);

    Serial.print("Speed Payload: ");
    Serial.print(speedstr);
    Serial.print("\n");
    if ( speedstr == "slow" ) {
      blinkSpeed = 1;
    } else if ( speedstr == "med" ) {
      blinkSpeed = 3;
    } else if ( speedstr == "fast" ) {
      blinkSpeed = 5;
    }
    free(p);
  }



  client.publish((String(cat_name)+"/status").c_str(), "fishing");
}

void ledTimerCallback(void *pArg){
  if (ledmode == 0){
    // do nothing. nothing blinky in this mode.

    return;
  } else if (ledmode == 1){
    //leds are pulsing
    if (ledrBrightness >= 250) {ledrFadein = false;}
    if (ledrBrightness <= 5) {ledrFadein = true;}
    if (ledrFadein){ledrBrightness += blinkSpeed;}
    else {ledrBrightness -= blinkSpeed;}

    if (ledgBrightness >= 250) {ledgFadein = false;}
    if (ledgBrightness <= 5) {ledgFadein = true;}
    if (ledgFadein){ledgBrightness += blinkSpeed;}
    else {ledgBrightness -= blinkSpeed;}

    if (ledbBrightness >= 250) {ledbFadein = false;}
    if (ledbBrightness <= 5) {ledbFadein = true;}
    if (ledbFadein){ledbBrightness += blinkSpeed;}
    else {ledbBrightness -= blinkSpeed;}

    //set color based on globals.
    setEyeColor();
  } else if (ledmode ==2 ){
    //normal blinky stuff
    if (blinkCounter >= 120){
      ledrBrightness = 255;
      ledgBrightness = 255;
      ledbBrightness = 255;
    }
    blinkCounter += blinkSpeed;
    if (blinkCounter >= 240){
      ledrBrightness = 0;
      ledgBrightness = 0;
      ledbBrightness = 0;
      blinkCounter = 0;
    }
    //set color based on globals.
    setEyeColor();
  }

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    Serial.print("mqtt_username:");
    Serial.print(mqtt_username);
    Serial.print(", mqtt_password:");
    Serial.print(mqtt_password);
    Serial.print(", mqtt_server:");
    Serial.print(mqtt_server);
    Serial.print("\n Attempting MQTT connection...");

    // Attempt to connect
    if (client.connect(cat_name, mqtt_username, mqtt_password, (String(cat_name) + "/connected").c_str(), 2, true, "0")) {
      Serial.println("connected");
      eye_debug(pixels.Color(0,255,0));
      delay(100);
      eye_debug(pixels.Color(0,0,0));
      // Once connected, publish an announcement...
      client.publish((String(cat_name) + "/connected").c_str(), "1", true);
      // ... and resubscribe
      client.subscribe( ( String(cat_name) + "/paw/command").c_str() );
      client.subscribe( (String(cat_name) + "/command" ).c_str() );
      client.subscribe("winkekatze/allcats");
      client.subscribe( (String(cat_name) + "/eye/color/set").c_str());
      client.subscribe( (String(cat_name) + "/eye/mode/set").c_str());
      client.subscribe( (String(cat_name) + "/eye/speed/set").c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      eye_debug(pixels.Color(255,0,0));
      delay(100);
      eye_debug(pixels.Color(0,0,0));
      // Wait 2 seconds before retrying
      delay(1900);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
