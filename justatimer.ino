// A timer running on ESP8266 connected to 4 8x8 matrixes controlled by a MAX7219.
// Two buttons for input.
// One piezo for notification.

#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <SPI.h>
#include "InputDebounce.h"
#include <PubSubClient.h>

#include "kFont_Data.h"
#include "2kFont_Data.h"
#include "pitches.h"

// WiFi
WiFiManager wm;
WiFiManagerParameter display_brightness("brightness", "Display brightness (0-15)", "2", 2);


// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org");
#define TIME_OFFSET 2
short timeOffset = TIME_OFFSET;

// MQTT
const char* mqtt_server = "10.0.0.3";
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;
int power = -1;
float price = -1;
float consumption = -1;

// Display
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D8 // or SS

MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

#define CHAR_SPACING  1 // Pixels between characters
#define USE_LOCAL_FONT 1 // Use font from Font_Data.h

#define BUF_SIZE  10
char message[BUF_SIZE];
char prefix[3];

int brightness = 2;

// Input buttons
#define buttonAction D1
#define buttonIncrease D2

int buttonActionState = false;
static InputDebounce buttonTestAction;
int buttonIncreaseState = false;
static InputDebounce buttonTestIncrease;

#define BUTTON_DEBOUNCE_DELAY   20   // [ms]
#define STEP 30 // Increase adds this many seconds

// Notificaiton
#define MELODY_PIN D6

// State
#define INITIAL_VALUE 0
int timer = INITIAL_VALUE;

unsigned long previousMillis = 0;
unsigned long interval = 1000;

unsigned long previousTimeMillis = 0;
unsigned long timeInterval = 360000;

char timerDisplay[8];

bool active = false;

enum mode {
  None = 0,
  Timer = 1,
  StopWatch = 2,
  Power = 3,
  Price = 4,
  Consumption = 5,
  TimerPlus = 6
};

mode state = None;

// Input Functions
void buttonTest_pressedCallback(uint8_t pinIn)
{
  if (pinIn == buttonAction) {
    if (timer == 0 && !active) {
      state = StopWatch;
      active = true;
    } else {
      active = !active;
    }
    buttonActionState = true;
  } else if (pinIn == buttonIncrease) {
    buttonIncreaseState = true;
  }
}

void buttonTest_releasedCallback(uint8_t pinIn)
{
  if (pinIn == buttonAction) {
    buttonActionState = false;
  } else if (pinIn == buttonIncrease) {
    buttonIncreaseState = false;
  }
}

unsigned long increaseMillis = 0;
int increaseInterval = 250;

bool portalRunning = false;

void buttonTest_pressedDurationCallback(uint8_t pinIn, unsigned long duration)
{ 
  unsigned long now = millis();
  interval = 1000;
  Serial.print("Button");
  Serial.println(pinIn, duration);

  if (buttonActionState && buttonIncreaseState) {
      if (now - increaseMillis >= increaseInterval) {
          increaseMillis = now;
      } else {
        return;
      }
      
    if (duration >= 3000) {
      if (portalRunning){
        wm.process();
      }
      
      Serial.println("Activate configuration portal");
      wm.setConfigPortalTimeout(120);
      wm.setParamsPage(true);

      if (!portalRunning){
        Serial.println("Button Pressed, Starting Portal");
        wm.startWebPortal();
        portalRunning = true;
      }
      
      myDisplay.setFont(nullptr);
      myDisplay.print("CONFIG");
      return;
    }
    Serial.println("RESET");
    myDisplay.setFont(nullptr);
    myDisplay.print("RESET");
    active = false;
    state = None;
    timer = 0;
    return;
  }
  
  if ((pinIn == buttonIncrease) && (state != StopWatch)) {
    if (now - increaseMillis >= increaseInterval) {
      state = Timer;
      increaseMillis = now;
      timer += STEP;
    }

    // Increase speed when holding the button longer
    if (duration > 3000) {
      increaseInterval = 25;
    } else if (duration > 1000) {
      increaseInterval = 100;
    } 
  }
}

void buttonTest_releasedDurationCallback(uint8_t pinIn, unsigned long duration)
{
  // Reset increase interval
  increaseInterval = 250;
}

void saveParamsCallback () {
  Serial.println("Get Params:");
  Serial.print(display_brightness.getID());
  Serial.print(" : ");
  Serial.println(display_brightness.getValue());

  brightness = String(display_brightness.getValue()).toInt();
  myDisplay.setIntensity(brightness);
}

// MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  // Serial.print("Message arrived [");
  // Serial.print(topic);
  // Serial.print("] ");
  payload[length] = '\0';

  // Serial.print("Topic: Value: ");
  // Serial.println(atoi((char *)payload));

  if (strcmp("tibber/power", topic) == 0) {
    power = atoi((char *)payload);
  } else if (strcmp("tibber/price", topic) == 0) {
    price = atof((char *)payload);
  } else if (strcmp("tibber/consumption-today", topic) == 0) {
    consumption = atof((char *)payload);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("tibber/+");
      Serial.println("Subscribed");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  
  // Intialize the object:
  myDisplay.begin();
  myDisplay.setFont(numeric7Seg);
  // Set the intensity (brightness) of the display (0-15):
  brightness = String(display_brightness.getValue()).toInt();
  myDisplay.setIntensity(brightness);
  // Clear the display:
  myDisplay.displayClear();
  myDisplay.setTextAlignment(PA_CENTER);
  
  // register callback functions (shared, used by all buttons)
  buttonTestAction.registerCallbacks(buttonTest_pressedCallback, buttonTest_releasedCallback, buttonTest_pressedDurationCallback, buttonTest_releasedDurationCallback);
  buttonTestIncrease.registerCallbacks(buttonTest_pressedCallback, buttonTest_releasedCallback, buttonTest_pressedDurationCallback, buttonTest_releasedDurationCallback);

  // setup input buttons (debounced)
  buttonTestAction.setup(buttonAction, BUTTON_DEBOUNCE_DELAY, InputDebounce::PIM_INT_PULL_UP_RES, 300);
  buttonTestIncrease.setup(buttonIncrease, BUTTON_DEBOUNCE_DELAY, InputDebounce::PIM_INT_PULL_UP_RES);

  // Notificaiton
  pinMode(MELODY_PIN, OUTPUT);

  Serial.println("");
  Serial.println("Just a Timer");

  // WiFi & Config
  WiFi.mode(WIFI_STA);
  //wm.resetSettings();
  wm.addParameter(&display_brightness);
  wm.setConfigPortalBlocking(false);
  wm.setSaveParamsCallback(saveParamsCallback);
  
  // invert theme, dark
  wm.setDarkMode(true);
  wm.setParamsPage(true); // move params to seperate page, not wifi, do not combine with setmenu!
  // set Hostname
  wm.setHostname("JUSTATIMER");
  wm.setBreakAfterConfig(true);

  if (wm.autoConnect("Just a Timer")) {
      Serial.println("Connected to WiFi");
  }
  else {
      Serial.println("Configuration portal running");
  }

  // MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.setKeepAlive(60);
  client.setSocketTimeout(60);
  
  // NTP
  timeClient.begin();
  timeClient.setTimeOffset(timeOffset * 3600);
  timeClient.update();
}

void playMelody() {
  int size = sizeof(melody) / sizeof(int);

  delay(1000);
  
  // iterate over the notes of the melody:
  for (int thisNote = 0; thisNote < size; thisNote++) {

    // to calculate the note duration, take one second divided by the note type.
    //e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
    int noteDuration = 1000 / tempo[thisNote];
    tone(MELODY_PIN, melody[thisNote], noteDuration);

    // to distinguish the notes, set a minimum time between them.
    // the note's duration + 30% seems to work well:
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    // stop the tone playing:
    noTone(MELODY_PIN);
  }
}

void progressState() {
  if (state == None)
    state = Power;
  else if (state == Power)
    state = Price;
  else if (state == Price)
    state = Consumption;
  else if (state == Consumption)
    state = None;
}

void loop() {
  // MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  unsigned long now = millis();
  // poll button state
  buttonTestAction.process(now); // callbacks called in context of this function
  buttonTestIncrease.process(now);

  // WiFi manager
  wm.process();

  // Update time
  if (now - previousTimeMillis >= timeInterval) {
    previousTimeMillis = now;
    timeClient.update();    
  }

  if (now - previousMillis >= interval) {
    previousMillis = now;

    if (active) {
      if (state == Timer)
        timer--;
      else
        timer++;

      if (timer < 0) {
        //active = false;
        timer = 0;
        myDisplay.setFont(nullptr);
        myDisplay.print("DONE");
        state = TimerPlus;
        playMelody();
      }
    } else if (timer == 0) {
      if (state == None) {
        sprintf(timerDisplay, "%02d:%02d\0", timeClient.getHours(), timeClient.getMinutes());
        interval = 10000;
        myDisplay.setFont(numeric7Seg);
      } else if (state == Power) {
        sprintf(timerDisplay, "%02d w\0", power);
        interval = 3000;
        myDisplay.setFont(numeric7Seg2);
      } else if (state == Price) {
        sprintf(timerDisplay, "%0.2f k\0", price);
        interval = 3000;
        myDisplay.setFont(numeric7Seg2);
      }  else if (state == Consumption) {
        if (consumption >= 10) {
          sprintf(timerDisplay, "%0.1f W\0", consumption);
        } else {
          sprintf(timerDisplay, "%0.2f W\0", consumption);
        }
        interval = 3000;
        myDisplay.setFont(numeric7Seg2);
      } else {
        myDisplay.setFont(nullptr);
        sprintf(timerDisplay, "hmm %d\0", state);
        interval = 1000;
      }

      progressState();
    }

    if (timer > 0) {
      Serial.println(state);
      if (state == TimerPlus) {
        strcpy(prefix, "+\n");
      } else {
        strcpy(prefix, "\n");
      }
      myDisplay.setFont(numeric7Seg);
      
      if (timer >= 3600) {
        sprintf(timerDisplay, "%s%02d:%02d\0", prefix, timer / 3600, timer % 3600 - timer % 60);
      } else if (timer >= 600) {
        sprintf(timerDisplay, "%s%02d:%02d\0", prefix, timer / 60, timer % 60);
      } else if (timer >= 60) {
        sprintf(timerDisplay, "%s%d:%02d\0", prefix, timer / 60, timer % 60);
      } else {
        sprintf(timerDisplay, "%s%d\0", prefix, timer);
      }

      
    }
    
    memcpy(message, timerDisplay, BUF_SIZE);
    myDisplay.print(message);  
  }

  // if ((timer > 0) || (active)) {
    
  // }
}
