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

#include "Font_Data.h"
#include "pitches.h"

// WiFi
WiFiManager wm;
WiFiManagerParameter display_brightness("brightness", "Display brightness (0-15)", "5", 2);

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org");

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

int brightness = 3;

// Input buttons
#define buttonAction D1
#define buttonIncrease D2

int buttonActionState = false;
static InputDebounce buttonTestAction;
int buttonIncreaseState = false;
static InputDebounce buttonTestIncrease;

#define BUTTON_DEBOUNCE_DELAY   20   // [ms]
#define STEP 10 // Increase adds this many seconds

// Notificaiton
#define MELODY_PIN D6

// State
#define INITIAL_VALUE 0
int timer = INITIAL_VALUE;

unsigned long previousMillis = 0;
const long interval = 1000;

char timerDisplay[8];

bool active = false;

enum mode {
  None = 0,
  Timer = 1,
  StopWatch = 2
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
      
      return;
    }
    Serial.println("RESET");
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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  
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
  
  // NTP
  timeClient.begin();
  timeClient.setTimeOffset(3600);
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

void loop() {
  wm.process();
  
  unsigned long now = millis();
  if (now - previousMillis >= interval) {
    previousMillis = now;

    if (active) {
      if (state == Timer)
        timer--;
      else
        timer++;

      if (timer < 0) {
        active = false;
        timer = 0;
        myDisplay.setFont(nullptr);
        myDisplay.print("DONE");
        state = None;
        playMelody();
      }
    } else if (timer == 0) {
      sprintf(timerDisplay, "%02d:%02d\0", timeClient.getHours(), timeClient.getMinutes());
      memcpy(message, timerDisplay, BUF_SIZE);
  
      myDisplay.setFont(numeric7Seg);
      myDisplay.print(message);
    }
  }
  
  // poll button state
  buttonTestAction.process(now); // callbacks called in context of this function
  buttonTestIncrease.process(now);

  if ((timer > 0) || (active)) {
    if (timer >= 600) {
      sprintf(timerDisplay, "%02d:%02d\0", timer / 60, timer % 60);
    } else if (timer >= 60) {
      sprintf(timerDisplay, "%d:%02d\0", timer / 60, timer % 60);
    } else {
      sprintf(timerDisplay, "%d\0", timer);
    }
  
    memcpy(message, timerDisplay, BUF_SIZE);
  
    myDisplay.setFont(numeric7Seg);
    myDisplay.print(message);
  }
}
