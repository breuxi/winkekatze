#include <FS.h>   // SPIFFS Filesystem
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <PubSubClient.h>
#include <stdlib.h>
#include "FastLED.h"
FASTLED_USING_NAMESPACE

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
#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define DATA_PIN    14
//#define CLK_PIN   4
#define LED_TYPE    WS2811
#define COLOR_ORDER RGB
#define NUM_LEDS    2
CRGB leds[NUM_LEDS];
#define BRIGHTNESS          96
#define FRAMES_PER_SECOND  120
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
//Trigger pin for forced Setup
const int TRIGGER_PIN = 4; //Wemos D1 Mini: "D2"

//Timer, for changing color etc.
os_timer_t ledTimer;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
//for whatever reasons auto typedef didnt work...
void juggle();
void bpm();
void sinelon();
void confetti();
void addGlitter( fract8 chanceOfGlitter);
void rainbowWithGlitter();
void rainbow();


typedef void (*SimplePatternList[])();
SimplePatternList gPatterns = { rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm };

uint8_t gCurrentPatternNumber = 0; // Index number of which pattern is current
uint8_t gHue = 0; // rotating "base color" used by many of the patterns
bool gColorcycle = 0;

void setup_wifi();
void callback(char* topic, byte* payload, unsigned int length);
void ledTimerCallback(void *pArg);

void eye_debug(struct CRGB pixel_color) {
  Serial.println("Running Eye Debug");
  for(int dot = 0; dot < NUM_LEDS; dot++) {
            leds[dot] = pixel_color;
            FastLED.show();
        }
  Serial.println("finishing Eye Debug");
};

//flag for saving data
bool shouldSaveConfig = false;

void configModeCallback(WiFiManager *myWiFiManager) {
  eye_debug(CRGB::Cyan);
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

  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  Serial.println("before 1st eye debug");
  eye_debug(CRGB::Green);
  Serial.println("after brightness control");
  //turn wink mechanism off
  pinMode(servoPin, OUTPUT);
  digitalWrite(servoPin, LOW);
Serial.println("after servoPin setting");
  //check if Setup is enforced (user pressed Button while booting...)
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    Serial.println("erasing filesystem");
    SPIFFS.format();
    ESP.eraseConfig();
  };
Serial.println("before readConfig");
  readConfig();
  Serial.println("will setup wifi now");
  setup_wifi();

  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(callback);


  eye_debug(CRGB::HotPink);
  delay(100);

  eye_debug(CRGB::Black);
  //arm Timer
  os_timer_setfn(&ledTimer, ledTimerCallback, NULL);
  os_timer_arm(&ledTimer, 10, true);
  Serial.println("end of setup");
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

  Serial.println("in setup wifi now");
  eye_debug(CRGB::Blue);
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
    eye_debug(CRGB::Red);
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
  } else if ( String(topic) == String(cat_name)+"/eye/hue/set" ) {
    char* p = (char*)malloc(length + 1);
    p[length] = 0;

    // Copy the payload to the new buffer
    memcpy(p, payload, length);

    String colstr(p);
//  if ( colstr == "pink" ) {
    Serial.print("Color Payload: ");
    Serial.print(colstr);
    Serial.print("\n");
    gHue = colstr.toInt() % 256 ;
    free(p);
  } else if ( String(topic) == String(cat_name)+"/eye/colorcycle/set" ) {
    char* p = (char*)malloc(length + 1);
    p[length] = 0;

    // Copy the payload to the new buffer
    memcpy(p, payload, length);

    String cyclestr(p);
//  if ( colstr == "pink" ) {
    Serial.print("colorcycle Payload: ");
    Serial.print(cyclestr);
    Serial.print("\n");
    gColorcycle = cyclestr.toInt() % 2;
    free(p);
  } else if ( String(topic) == String(cat_name)+"/eye/brightness/set" ) {
    char* p = (char*)malloc(length + 1);
    p[length] = 0;

    // Copy the payload to the new buffer
    memcpy(p, payload, length);

    String brtstr(p);
    Serial.print("Brightness Payload: ");
    Serial.print(brtstr);
    Serial.print("\n");
    FastLED.setBrightness(brtstr.toInt() % 256);
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
    int mode = modestr.toInt();
    gCurrentPatternNumber = mode % ARRAY_SIZE(gPatterns);
  }
  client.publish((String(cat_name)+"/status").c_str(), "fishing");
}

void ledTimerCallback(void *pArg){
    // Call the current pattern function once, updating the 'leds' array
  gPatterns[gCurrentPatternNumber]();

  // send the 'leds' array out to the actual LED strip
  FastLED.show();

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
      eye_debug(CRGB::Green);
      delay(100);
      eye_debug(CRGB::Black);
      // Once connected, publish an announcement...
      client.publish((String(cat_name) + "/connected").c_str(), "1", true);
      // ... and resubscribe
      client.subscribe( ( String(cat_name) + "/paw/command").c_str() );
      client.subscribe( (String(cat_name) + "/command" ).c_str() );
      client.subscribe("winkekatze/allcats");
      client.subscribe( (String(cat_name) + "/eye/hue/set").c_str());
      client.subscribe( (String(cat_name) + "/eye/brightness/set").c_str());
      client.subscribe( (String(cat_name) + "/eye/mode/set").c_str());
      client.subscribe( (String(cat_name) + "/eye/speed/set").c_str());
      client.subscribe( (String(cat_name) + "/eye/colorcycle/set").c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      eye_debug(CRGB::Red);
      delay(100);
      eye_debug(CRGB::Black);
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
  //if colorcycle is on, rotate colors
  EVERY_N_MILLISECONDS( 20 ) { 
    if(gColorcycle){
      gHue++; 
    } 
  }
}

void rainbow()
{
  // FastLED's built-in rainbow generator
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
}

void rainbowWithGlitter()
{
  // built-in FastLED rainbow, plus some random sparkly glitter
  rainbow();
  addGlitter(80);
}

void addGlitter( fract8 chanceOfGlitter)
{
  if( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB::White;
  }
}

void confetti()
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy( leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV( gHue + random8(64), 200, 255);
}

void sinelon()
{
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy( leds, NUM_LEDS, 20);
  int pos = beatsin16( 13, 0, NUM_LEDS-1 );
  leds[pos] += CHSV( gHue, 255, 192);
}

void bpm()
{
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);
  for( int i = 0; i < NUM_LEDS; i++) { //9948
    leds[i] = ColorFromPalette(palette, gHue+(i*2), beat-gHue+(i*10));
  }
}

void juggle() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy( leds, NUM_LEDS, 20);
  byte dothue = 0;
  for( int i = 0; i < 8; i++) {
    leds[beatsin16( i+7, 0, NUM_LEDS-1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}
