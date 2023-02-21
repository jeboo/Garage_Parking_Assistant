/* Garage Parking Sensor
 *
 * Original code by Bob Torrence -- https://create.arduino.cc/projecthub/Bcjams/garage-parking-assistant-11446b
 * Major revision by galmiklos (added eeprom storage for stop distance set by button, added sleep feature, optimized code for any # LEDs)
 * Additional optimizations/features by jeboo
 * 
 * Version history:
 * 0.5 -- Switched timer library to MillisTimer (galmiklos code appeared to use customized/unavailable library)
 *        Fixed push-button setting for stopdistance: can still specify static value; setting via button no longer requires a reboot
 *        Added option (SLEEP_POLLDELAY) to poll less often once in sleep mode
 *        Added option for independent left and right LED strip control; allows differing colors, animations, etc.
 *
 * 0.6 -- Added EEPROM dependency and switched to QuickMedianLib for compatibility with megaAVR boards (tested on Nano Every).
 * 
 * 0.7 -- Cosmetic bugfix: Changed amber LED behavior to only show when car is close to stop.
 */
 
#include <FastLED.h>
#include <MillisTimer.h>
#include <EEPROM.h>
#include <QuickMedianLib.h>

// defining the parameters/layout
#define NUM_LEDS            15      // # LEDs on each side
#define AMBER_LEDS          4       // number of amber LEDs when car is close to stop
#define DISTANCE_TOLERANCE  3       // tolerance to determine if the car is stopped (CENTIMETERS)
#define LED_TIMEOUT         60000   // in milliseconds, time before lights go out (sleep) once car stopped
#define SLEEP_POLLDELAY     1000    // in milliseconds, delay for each main loop iteration while in sleep mode

// defining the pins
#define BUTTON_PIN          6       // push-button for setting stopdistance (optional)
#define LED_PIN_L           7       // left-sided LEDs (or both sides if mirror_LEDs set to true)
#define LED_PIN_R           8       // right-sided LEDs (optional), implemented by setting mirror_LEDs below to false
#define TRIG_PIN            9
#define ECHO_PIN            10

// defined variables
int startdistance = 395;            // distance from sensor to begin scan as car pulls in (CENTIMETERS); HC-SR04 max distance is 400CM
int stopdistance = 0;               // parking position from sensor (CENTIMETERS); either specify here, or leave as zero and set dynamically with push-button
const int durationarraysz = 15;     // size of array for distance polling
bool mirror_LEDs = true;            // true = L + R LEDs controlled by pin 7 (mirrored, as in stock project)

// variables
CRGB leds_L[NUM_LEDS], leds_R[NUM_LEDS];
float duration, durationarray[durationarraysz];
int distance, previous_distance, increment, i;
uint16_t stopdistance_ee EEMEM;
MillisTimer timer = MillisTimer(LED_TIMEOUT);
bool LED_sleep;

void sleepmode_start(MillisTimer &mt)
{
  for (i = 0; i < NUM_LEDS; i++)
  {
      leds_L[i] = leds_R[i] = CRGB::Black;
  }
  FastLED.show();

  timer.reset();
  LED_sleep = true;
}

void setup()
{
  LED_sleep = false;
  if (!stopdistance)
  {
    stopdistance = eeprom_read_word(&stopdistance_ee);
  }
  increment = (startdistance - stopdistance)/NUM_LEDS;
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  FastLED.addLeds<WS2812, LED_PIN_L, GRB>(leds_L, NUM_LEDS);
  if (!mirror_LEDs) 
  {
    FastLED.addLeds<WS2812, LED_PIN_R, GRB>(leds_R, NUM_LEDS);
  }
  
  timer.setInterval(LED_TIMEOUT);
  timer.expiredHandler(sleepmode_start);
  timer.setRepeats(0);

  Serial.begin(9600);
}

void loop()
{
  timer.run();
  for (i = 0; i < durationarraysz; i++)
  {
    // Clears the trigPin
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    // Sets the trigPin on HIGH state for 10 micro seconds
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    // Reads the echoPin, returns the sound wave travel time in microseconds
    durationarray[i] = pulseIn(ECHO_PIN, HIGH);
  }
  
  duration = QuickMedian<float>::GetMedian(durationarray, durationarraysz);
  // Calculating the distance
  distance = duration*0.034/2;

  //Serial.print("stopdistance: "); Serial.println(stopdistance);
  //Serial.print("distance: "); Serial.println(distance);
  //Serial.print("previous_distance: "); Serial.println(previous_distance);

  // check if distance changed is within tolerance
  if (abs(distance - previous_distance) < DISTANCE_TOLERANCE)
  {      
    if (!timer.isRunning() && !LED_sleep)
    {
      timer.start();
    }
  }
  else // movement beyond threshold, stop timer and turn off sleep
  {
    previous_distance = distance;
    timer.reset();
    LED_sleep = false;
  }

  // check button state
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    stopdistance = distance;
    eeprom_write_word(&stopdistance_ee, stopdistance);
    increment = (startdistance - stopdistance)/NUM_LEDS;
  }

  if (!LED_sleep)
  {
    if (distance > startdistance)
    {
      for (i = 0; i < NUM_LEDS; i++)
      {
        leds_L[i] = leds_R[i] = CRGB::Black;
      }    
    }
    else if (distance <= stopdistance)
    {
      for (i = 0; i < NUM_LEDS; i++)
      {
        leds_L[i] = leds_R[i] = CRGB::Red;
      }
    }
    else
    {
      for (i = 0; i < NUM_LEDS; i++)
      {
        if (distance > (stopdistance+increment*i))
        {
          leds_L[i] = leds_R[i] = (distance < (stopdistance+(AMBER_LEDS+1)*increment)) ? CRGB::Orange : CRGB::Green;
        }
        else
        {
          leds_L[i] = leds_R[i] = CRGB::Black;
        }
      }
    }    
    FastLED.show();
    delay(50);
  }  
  else
  {
    delay(SLEEP_POLLDELAY);
  }
}
