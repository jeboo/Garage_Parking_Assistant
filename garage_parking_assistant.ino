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
 * 
 * 0.8 -- Changed polling code to NewPing library. This library fixes blocking that may occur on some HC-SR04s when the distance
 *        is beyond 400cm. NewPing also has built-in iterative polling and median calculation, simplifying the code greatly.
 *
 * 0.9 -- Fixed amber-to-green transition bug.
  *
 * 0.10 - Fixed lights not showing in mirrored mode; added version code upon power cycle. LEDs will show White = major, Red = minor version for 3s.
 */
 
#include <FastLED.h>
#include <MillisTimer.h>
#include <EEPROM.h>
#include <NewPing.h>

// defining the parameters/layout
#define NUM_LEDS                30      // # LEDs on each side
#define AMBER_LEDS              5       // number of amber LEDs when car is close to stop
#define DISTANCE_TOLERANCE      5       // tolerance to determine if the car is stopped (CENTIMETERS)
#define LED_TIMEOUT             60000   // in milliseconds, time before lights go out (sleep) once car stopped
#define SLEEP_POLLDELAY         1000    // in milliseconds, delay for each main loop iteration while in sleep mode

// defining the hardware
#define BUTTON_PIN              6       // push-button for setting stopdistance (optional)
#define LED_PIN_L               7       // left-sided LEDs (or both sides if mirror_LEDs set to true)
#define LED_PIN_R               8       // right-sided LEDs (optional), implemented by setting mirror_LEDs below to false
#define TRIG_PIN                9
#define ECHO_PIN                10
#define MAX_DISTANCE            400     // HC-SR04 max distance is 400CM

// version
#define MAJOR_VER               1
#define MINOR_VER               10

// defined variables
const int startdistance =       400;    // distance from sensor to begin scan as car pulls in (CENTIMETERS)
int stopdistance =              10;      // parking position from sensor (CENTIMETERS); either specify here, or leave as zero and set dynamically with push-button
const int durationarraysz =     20;     // number of measurements to take per cycle
const bool mirror_LEDs =        true;   // true = L + R LEDs controlled by LED_PIN_L (mirrored, as in stock project)

// variables
CRGB leds_L[NUM_LEDS], leds_R[NUM_LEDS];
int distance, previous_distance, increment, i;
unsigned int uS;
uint16_t stopdistance_ee EEMEM;
MillisTimer timer = MillisTimer(LED_TIMEOUT);
bool LED_sleep;

NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE); // NewPing setup of pins and maximum distance.

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
  CRGB temp_color;
  LED_sleep = false;

  if (!stopdistance)
  {
    stopdistance = eeprom_read_word(&stopdistance_ee);
  }
  increment = (startdistance - stopdistance)/NUM_LEDS;
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  FastLED.addLeds<WS2812, LED_PIN_L, GRB>(leds_L, NUM_LEDS);
  FastLED.addLeds<WS2812, mirror_LEDs ? LED_PIN_L : LED_PIN_R, GRB>(leds_R, NUM_LEDS);
  
 // flash major/minor version for 3s
  for(i = 0; i < NUM_LEDS; i++)
  {
      temp_color = CRGB::Black;

      if (i < (MAJOR_VER)) { temp_color = CRGB::White; }
      else if ((i > (MAJOR_VER) && (i < (MAJOR_VER + 1 + MINOR_VER)))) { temp_color = CRGB::Red; }

      leds_L[i] = leds_R[i] = temp_color;
  }

  FastLED.show();

  delay(3000);

  // clear LEDs  
  for(i = 0; i < NUM_LEDS; i++)
        leds_L[i] = leds_R[i] = CRGB::Black;
  
  FastLED.show();

  timer.setInterval(LED_TIMEOUT);
  timer.expiredHandler(sleepmode_start);
  timer.setRepeats(0);

  Serial.begin(9600);
}

void loop()
{
  timer.run();

  uS = sonar.ping_median(durationarraysz, MAX_DISTANCE);
  distance = sonar.convert_cm(uS);
  
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
    // turn off lights when beyond startdistance
    if (distance > startdistance)
    {
      for (i = 0; i < NUM_LEDS; i++)
      {
        leds_L[i] = leds_R[i] = CRGB::Black;
      }    
    }
    else if (distance <= stopdistance) // either past stopdistance or limit of sensor (reported as distance=0)
    {
      for (i = 0; i < NUM_LEDS; i++)
      {
        leds_L[i] = leds_R[i] = distance ? CRGB::Red : CRGB::Black;
      }
    }
    else
    {
      for (i = 0; i < NUM_LEDS; i++) // fill entire LED array with orange/green/black
      {
        if (distance > (stopdistance+increment*i))
        {
          leds_L[i] = leds_R[i] = (distance <= (stopdistance+(AMBER_LEDS)*increment)) ? CRGB::Orange : CRGB::Green;
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
    //Serial.println("Sleep");
  }
}
