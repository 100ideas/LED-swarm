#include <Arduino.h>
#include <FastLED.h>
#include <SPI.h>
#include <NRFLite.h>
#include <EEPROM.h>


// board arduino nano
/*
  Radio    Arduino
  CE    -> 9
  CSN   -> 10 (Hardware SPI SS)
  MOSI  -> 11 (Hardware SPI MOSI)
  MISO  -> 12 (Hardware SPI MISO)
  SCK   -> 13 (Hardware SPI SCK)
  IRQ   -> No connection
  VCC   -> No more than 3.6 volts
  GND   -> GND

*/

// #define SUPERLEADER   // forces highest ID on boot
//#define SUPERFOLLOWER      // does not transmit
#define DEFAULTBRIGHTNESS 64
//#define SHOWDEBUG
//#define BATTERYTEST

// some build configurations I tested
//#define NANO150
// #define NANO60
#define NANODOT
//#define TEENSY144
// #define TEENSY150  // original from Chuck
//#define NANO100APA102
//#define STRONGMAN

#ifdef NANO100APA102
#define NANO
#define  RGBORDER BRG
#define NUM_LEDS 100                                // how long our strip is
#define DATA_PIN 7
#define CLOCK_PIN 13
#define CHIP APA102
#endif

#ifdef NANO150
#define USERADIO
#define USEACCEL
#define NANO
#define  RGBORDER RGB
#define NUM_LEDS 150                                // how long our strip is
#define DATA_PIN 7                                  // 2811 data pin
#define CHIP WS2812
#endif

#ifdef NANO60
#define NANO
#define USERADIO
// #define BATTERYTEST
#define RGBORDER BGR
#define NUM_LEDS 60                                // how long our strip is
#define DATA_PIN 5                                  // 2811 data pin
#define CHIP WS2811
#endif

#ifdef NANODOT
#define SUPERLEADER
#define NANO
#define USERADIO
#define RGBORDER BGR
#define NUM_LEDS 60                                // how long our strip is
#define DATA_PIN 3
#define CLOCK_PIN 2
#define CHIP APA102
#define PIN_PUSHBUTTON 4
#endif

#ifdef TEENSY150
#define TEENSY
#define USERADIO
#define USEACCEL
#define  RGBORDER RGB
#define NUM_LEDS 150                                // how long our strip is
#define DATA_PIN 17                                  // 2811 data pin
#define CHIP WS2812
//#define SNOOZE
#endif

#ifdef TEENSY144
#define TEENSY
#define USERADIO
#define USEACCEL
#define  RGBORDER RGB
#define NUM_LEDS 144                                // how long our strip is
#define DATA_PIN 17                                  // 2811 data pin
#define CHIP WS2812
//#define SNOOZE
#endif

#ifdef STRONGMAN
#ifndef SUPERLEADER
#define SUPERLEADER
#endif
#define USEPRESSURE
#define USERADIO
#define NANO
#define  RGBORDER RGB
#define NUM_LEDS 150                                // how long our strip is
#define DATA_PIN 7                                  // 2811 data pin
#define CHIP WS2812
#endif

#ifdef USEACCEL
#include <Wire.h>
#include <MPU6050_tockn.h>
MPU6050 mpu6050(Wire);
#endif

#ifdef USEPRESSURE
#include <Wire.h>
#include "Adafruit_MPRLS.h"
Adafruit_MPRLS mpr = Adafruit_MPRLS(-1, -1);
#endif


#define SLEEPADDRESS 0

#ifndef PIN_PUSHBUTTON
#define PIN_PUSHBUTTON 6
#endif

#ifdef SNOOZE
#include "Snoozer.h"
#endif

#include "SyncedAnims.h"
#include "dizzy.h"

// BUG - button pin is set to PULLUP so BUTTONUP state should be 1, down 0
// const char BUTTONUP = 0;
// const char BUTTONDOWN = 1;
const char BUTTONUP = 1;
const char BUTTONDOWN = 0;

const char BUTTONONE = 0;
const char BUTTONSHORT = 1;
const char BUTTONLONG = 2;

const static uint8_t RADIO_ID = 1;                  // Our radio's id.
const static uint8_t PIN_RADIO_CE = 9;              // hardware pins
const static uint8_t PIN_RADIO_CSN = 10;            // hardware pins

#define SENDPERIOD 2000000                             // broadcast period in microseconds
#define MUTEPERIOD (SENDPERIOD * 3)
#define TAGID 0x426C6973  // tag code is 'Blis'
#define LEDPERIOD (1000000 / 60)                       // how often we animate
#define BUTTONSHORTTIME 50000
#define BUTTONLONGTIME 1000000
#define MIN_BRIGHTNESS 20
#define MAX_BRIGHTNESS 255

#ifdef USERADIO
NRFLite _radio;
#endif
uint8_t current[32];                                // anim state variables
bool radioAlive = false;                            // true if radio booted up
unsigned long timeToSend;                                     // how long till next send;
unsigned long muteTimer;
int brightness;
int program = 0;                                    // which application is running in loop
unsigned long lastTime ;                            // for calculation delta time

// BUG sized int max value is 32767 but BUTTONSHORTTIME is 50000 - so use long
// int buttonDownTime;                                 // for measuring button presses
unsigned long buttonDownTime;                                 // for measuring button presses

char buttonState;                                   // is the button up or down
char buttonEvent;                                   // what the button did (short,long)
char radioEvent = 0;                                // what the radio

// BUG (potentially) will cause problems if LEDPERIOD value is ever larger than INT 
//   (unless on Due?) int is too small use long
int timeToDisplay;                                  // how long to update the display
// long timeToDisplay;                                  // how long to update the display

// How many leds in your strip?
// Define the array of leds
CRGB leds[NUM_LEDS];                                // our display

// mac
CRGB stored[NUM_LEDS]; // sort of displaybuffer

// macros for setting and getting the packet type and device IDs
#define getTag(data)  get32(data+0)
#define getID(data) get16(data+4)

#define putTag(data,value)  put32(data+0,value)
#define putID(data,value) put16(data+4,value)


void setLevel(int level)
{
  if (level < MIN_BRIGHTNESS)
    level = MIN_BRIGHTNESS;
  if (level > MAX_BRIGHTNESS)
    level = MAX_BRIGHTNESS;
  brightness = level;
  LEDS.setBrightness(brightness);
}

void fill(CRGB color)
{
  for (int i = 0; i < NUM_LEDS; i++)
  {
    leds[i] = color;
  }
}

//------------------------------------------------------------------------
void batteryTester(int delta)
{
  static unsigned long runtime = 0;
  static long ticks = 0;
  if (buttonEvent == BUTTONSHORT)
  {
    runtime = 0;
    ticks = 0;
    buttonEvent = 0;
  }
  runtime += delta;
  if ( radioEvent == 1)
  {
    radioEvent = 0;
    ticks++;
    Serial.print(runtime);
    Serial.print(",");
    Serial.println(ticks);
  }
}

//------------------------------------------------------------------------
void checkButtons(int delta)
{
  // if (digitalRead(PIN_PUSHBUTTON) == 0)
  if (digitalRead(PIN_PUSHBUTTON) == BUTTONDOWN)
  {
    if (buttonState == BUTTONUP)
    {
      buttonDownTime = 0;

    }
    buttonDownTime += delta;
    buttonState = BUTTONDOWN;
  }
  else
  {
    if (buttonState == BUTTONDOWN)
    {
      if (buttonDownTime > BUTTONSHORTTIME)
      {
        Serial.println("detected SHORT button event...");
        buttonEvent = BUTTONSHORT;
        if (buttonDownTime > BUTTONLONGTIME)
        {
          Serial.println("detected LONG button event...");
          buttonEvent = BUTTONLONG;
        }
        buttonState = BUTTONUP;
      }
    }
  }
#ifdef SNOOZE

  fill(0);
  FastLED.show();
  delay(1000);                    // wait for a second
  startSnooze(SLEEPADDRESS);
#endif
}
/*
  setLevel(brightness - 1);
  else if (digitalRead(PIN_BRIGHTNESS_UP) == 0)
  setLevel(brightness + 1);
  else
  EEPROM.update(0, brightness);   // eeprom.update only writes if the data is different
*/

//------------------------------------------------------------------------
// initialize the animation
void setupLED()
{
#ifdef CLOCK_PIN
  LEDS.addLeds<CHIP, DATA_PIN, CLOCK_PIN, RGBORDER >(leds, NUM_LEDS);
#else
  LEDS.addLeds<CHIP, DATA_PIN, RGBORDER >(leds, NUM_LEDS);
#endif
  brightness = EEPROM.read(0);
  brightness = DEFAULTBRIGHTNESS;//32;  //64;
  setLevel(brightness);
  syncedAnimeInit(current);
  //  pinMode(PIN_BRIGHTNESS_DOWN, INPUT_PULLUP);
  //  pinMode(PIN_BRIGHTNESS_UP, INPUT_PULLUP);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);

}
//------------------------------------------------------------------------

unsigned long crc( const byte *data, byte len ) {

  const unsigned long crc_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
  };

  unsigned long crc = ~0L;

  for ( unsigned int index = 0 ; index < len  ; ++index ) {
    crc = crc_table[( crc ^ data[index] ) & 0x0f] ^ (crc >> 4);
    crc = crc_table[( crc ^ ( data[index] >> 4 )) & 0x0f] ^ (crc >> 4);
    crc = ~crc;
  }
  return crc & 65535;
}

// initialize the radio code
void setupRadio()
{
#ifdef USERADIO
  pinMode(14, INPUT_PULLUP);
#ifdef SUPERLEADER          // super LEADER has the highest ID
  putID( current , 65535);
#else
  putID( current ,  random(65536));
#endif
  putTag( current , TAGID);
  SPI.begin();
  if (!_radio.init(RADIO_ID, PIN_RADIO_CE, PIN_RADIO_CSN))
  {
    radioAlive = false;
    //    Serial.println("radio fail");
  }
  else
  {
    radioAlive = true;
    //   Serial.println("radio ok");
  }
  timeToSend = 0;
  muteTimer = 0;       // lets listen before we start talking
#endif
}
void checkRadioReceive()
{
#ifdef USERADIO
#ifndef SUPERLEADER      // super LEADER is always deaf  
  uint8_t incoming[sizeof(current)];
  // check for incoming data
  while (_radio.hasData())
  {
    _radio.readData(&incoming);
    unsigned long sum = crc(incoming, 30);
    if (sum != ( incoming[30] + (incoming[31] * 256)))    // is the packet corrupt?
      continue;

    uint32_t tag = getTag(incoming );
    Serial.println("got");
    if (tag != TAGID)                      // is it our packet type?
      continue;

    uint16_t unitId = getID(incoming);
    uint16_t currentId = getID(current);
    
    Serial.println("currentId: "); Serial.print(currentId)
    Serial.println("from unitID: "); Serial.print(unitId)

#ifndef SUPERFOLLOWER
    if (unitId < currentId)                       // it was lower rank than me FOLLOWER, ignore it
      continue;
    if (unitId > currentId)                // it was higher rank than me, get the data
#endif
    {
      radioEvent = 1;

      // we dont copy the 6 byte header, or the 2 byte checksum
      memcpy(current + 6, incoming + 6, sizeof(current) - 8); // copy the incomming data into my data
      muteTimer = 0;              // im listening, so I keep quiet
      continue;
    }

    putID(current, random(65536));        // we had the same ID, pick a new one
  }
#endif
#endif
}

void checkRadioSend(int deltaTime)
{
#ifdef USERADIO
#ifdef SUPERFOLLOWER   // superFOLLOWER is always mute
  return;
#endif
  // check mute timer
  if (muteTimer < MUTEPERIOD)
  {
    muteTimer += deltaTime;
    return;
  }
  // run periodic timer
  timeToSend += deltaTime;
  if (timeToSend < SENDPERIOD)
    return;
  timeToSend = 0;

  unsigned long sum  = crc(current, 30);
  current[30] = sum & 255;
  current[31] = sum >> 8;

  if ( _radio.send(RADIO_ID, &current, sizeof(current), NRFLite::NO_ACK))
  {
    radioEvent = 2;                             // for display debug
  }
  else
  {
    timeToSend += getID(current) & 255;          // might have been a collision, wait a little longer, based on my ID
  }
#endif
}

//------------------------------------------------------------------------

void setup() {
  pinMode(PIN_PUSHBUTTON, INPUT_PULLUP);
#ifdef SNOOZE
  waitSnooze(SLEEPADDRESS, PIN_PUSHBUTTON);
#endif
  Serial.begin(115200);
  Serial.println("boot");
#ifdef USEACCEL
  Wire.begin();
  mpu6050.begin();
#endif
#ifdef USEPRESSURE
  if (mpr.begin())
    Serial.println("MPRLS sensor ok");
#endif
  //  Serial.println("resetting");
  //  pinMode(4, INPUT_PULLUP);
  //  pinMode(5, INPUT_PULLUP);
  randomSeed(analogRead(0));
  setupLED();
  setupRadio();
  program = 0;
  buttonDownTime = 0;
  buttonState = BUTTONUP;
  buttonEvent = 0;
  timeToDisplay = 0;
  lastTime = micros();
}

// utility to display radio info on first 5 LEDs
static bool gotone = false;
void ShowRadio(  CRGB *display)
{
  static CRGB faded = CRGB(0, 0, 0);
  if ( radioEvent == 1)
  {
    Serial.println("  received signal OK");
    faded = CRGB(50, 0, 0);
    gotone = true;
  }
  if ( radioEvent == 2)
  {
    // Serial.println("  ..))) radioEvent 2");
    faded = CRGB(0, 50, 0);
  }
  radioEvent = 0;
  for (int i = 0; i < 5; i++)
    display[i] &= faded ;
    // display[i] = faded;
  faded.nscale8(240);

}

#ifdef USEPRESSURE
int lastpressuretime = 0;
int strongmantimout = 0;
void strongmanUpdate(int delta)
{
  lastpressuretime += delta;
  if (lastpressuretime > 10000)       // time to read the sensor
  {
    lastpressuretime = 0;
    if (strongmantimout > 0)          // run down the active timer
    {
      strongmantimout--;
    }
    float pressure_hPa = mpr.readPressure();    // read the pressure
    pressure_hPa -= 1000;             // adjust for expected range
    if (pressure_hPa < 0)             // rectify
      pressure_hPa = 0;
    if (pressure_hPa > 10)            // was there some activity?
    {
      strongmantimout = 100;          // reset the timeout
    }
    if (strongmantimout > 0)          // are we active?
    {
      putEffect(current, STRONGMANEFFECT);              // force strongman effect
      putCounter(current, 0);               // dont let this effect timeout
      putData(current, 0, pressure_hPa);    // save the level
      timeToSend = SENDPERIOD - 10000;       // we send again soon
      muteTimer = MUTEPERIOD;
    }
    else
      muteTimer = 0;                  // we are mute if we arent active

  }
}
#endif

#ifdef SHOWDEBUG
void showDebug()
{
  // showing transmit/receive events
  ShowRadio(leds);
  // looking for LEADER/FOLLOWER mode
  for (int i = 5; i < 10; i++)
    leds[i] = (muteTimer > 0) ? CRGB(255, 255, 0) : CRGB(0, 0, 255);

  // looking for reboot
  for (int i = 10; i < 15; i++)
    leds[i] = gotone ? CRGB(0, 255, 255) : CRGB(255, 0, 0);
}
#endif

char strobing = 0;
// unsigned long strobe_duration = 50000;
unsigned long strobe_duration = 25000;
unsigned long strobe_end_moment;
CRGB strobe_led = CRGB(250, 250, 250);

void loop()
{
  unsigned long now = micros();
  unsigned long delta = now - lastTime;
  lastTime = now;
  
  // mac
  // if (program == 1) {
    // if (strobing == 0) {
      // Serial.println("start strobing...");
      // strobing = 1;
      // strobe_end_moment = now + (strobe_duration_ms * 1000);
      // for (int i = 0; i < NUM_LEDS; i++)
        // stored[i] = leds[i];
  //   }
  // }
    

  checkButtons(delta);
#ifdef USEPRESSURE
  strongmanUpdate(delta);
#endif
  if (radioAlive)
  {
    checkRadioReceive();
    checkRadioSend(delta);
  }
  //    serviceMotion(leds, NUM_LEDS);
  timeToDisplay += delta;
  while (timeToDisplay > LEDPERIOD)
  {
    timeToDisplay -= LEDPERIOD;
// #ifdef BATTERYTEST
//     batteryTester(delta);        // include to run radion battery test
// #else
    if (buttonEvent == BUTTONSHORT)
    {
      buttonEvent = BUTTONONE;
      // program++;
      // strobe_end_moment = now + (strobe_duration_ms * 30000);
      strobe_end_moment = now + strobe_duration;
      program = 1;
      Serial.println("buttonevent- strobe_end_moment, now, diff: ");
      Serial.println(strobe_end_moment);
      // Serial.print(' : ');
      Serial.println(now);
      Serial.println(now - strobe_end_moment);
      Serial.println(' ');
    }
// #ifdef USEACCEL
//     mpu6050.update();       // update the accelerometer
// #endif
    switch (program)
    {
      case 0:
        SyncedAnims( current, leds, NUM_LEDS);
// #ifdef SHOWDEBUG
//         showDebug();    // show some debug info on the display
// #endif
        break;
      case 1:
        if (now > strobe_end_moment) {
          // for (int i = 0; i < NUM_LEDS; i++)
            // leds[i] = stored[i];
          Serial.println("...ending strobe");
          program = 0;
          strobing = 0;
        } else {
          // putEffect(current, 4);
          Serial.println("...strobing...");
          // SyncedAnims( current, leds, NUM_LEDS);
          for (int i = 0; i < NUM_LEDS; i++)
            leds[i] = strobe_led;
        }
        break;
      case 2:
        // SyncedAnims( current, leds, NUM_LEDS);
        ShowRadio(  leds);
        break;
// #ifdef USEACCEL
//       case 2:
//         Serial.println("start program 2");
//         DizzyGame( current, leds, NUM_LEDS);
//         break;
// #endif
      default:
        Serial.println("start default program");
        program++;
        if (program >= 3)  /// maybe should be >= 2?
          program = 0;
        break;

    }
// #endif
    FastLED.show();
  }
}

/*
    Serial.println("=======================================================");
    Serial.print("temp : ");Serial.println(mpu6050.getTemp());
    Serial.print("accX : ");Serial.print(mpu6050.getAccX());
    Serial.print("\taccY : ");Serial.print(mpu6050.getAccY());
    Serial.print("\taccZ : ");Serial.println(mpu6050.getAccZ());

    Serial.print("gyroX : ");Serial.print(mpu6050.getGyroX());
    Serial.print("\tgyroY : ");Serial.print(mpu6050.getGyroY());
    Serial.print("\tgyroZ : ");Serial.println(mpu6050.getGyroZ());

    Serial.print("accAngleX : ");Serial.print(mpu6050.getAccAngleX());
    Serial.print("\taccAngleY : ");Serial.println(mpu6050.getAccAngleY());

    Serial.print("gyroAngleX : ");Serial.print(mpu6050.getGyroAngleX());
    Serial.print("\tgyroAngleY : ");Serial.print(mpu6050.getGyroAngleY());
    Serial.print("\tgyroAngleZ : ");Serial.println(mpu6050.getGyroAngleZ());

    Serial.print("angleX : ");Serial.print(mpu6050.getAngleX());
    Serial.print("\tangleY : ");Serial.print(mpu6050.getAngleY());
    Serial.print("\tangleZ : ");Serial.println(mpu6050.getAngleZ());
    Serial.println("=======================================================\n");
*/

