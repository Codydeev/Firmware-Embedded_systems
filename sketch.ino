
#include <FastLED.h>

// ==================================================
// PINS
// ==================================================

#define BUTTON1_PIN 23
#define BUTTON2_PIN 2
#define CONTROL_PIN 1

#define LED_PIN 18

#define NUM_LEDS 5
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB


// ==================================================
// LED SETTINGS
// ==================================================

const unsigned long LED_STEP_TIME_MS = 100;


// ==================================================
// BUTTON SETTINGS
// ==================================================

const unsigned long DEBOUNCE_MS = 30;
const unsigned long COMBO_WAIT_MS = 60;

// Time allowed between first release
// and second press.
const unsigned long DOUBLE_PRESS_WINDOW_MS = 150;


// ==================================================
// WAVE SETTINGS
// ==================================================

const unsigned long WAVE_CYCLE_MS = 900;


// ==================================================
// CONFIG MODE SETTINGS
// ==================================================

const unsigned long CONFIG_BLINK_TIME_MS = 500;
const unsigned long ZERO_BRIGHTNESS_TIMEOUT_MS = 15000;


// ==================================================
// BATTERY VOLTAGE MONITOR
// ==================================================

#define BATTERY_ADC_PIN 0

const float BATTERY_R1 = 18000.0;
const float BATTERY_R2 = 33000.0;

const float BATTERY_MAX_VOLTAGE = 6.0;

const unsigned long BATTERY_CHECK_INTERVAL_MS = 60000;


// ==================================================
// BATTERY TASK
// ==================================================

TaskHandle_t batteryTaskHandle = NULL;

// Shared between the battery task and main loop.
volatile bool batteryShutdown = false;


// ==================================================
// FASTLED
// ==================================================

CRGB leds[NUM_LEDS];


// ==================================================
// BUTTON STATES
// ==================================================

bool button1Pressed = false;
bool button2Pressed = false;

bool lastButton1Raw = false;
bool lastButton2Raw = false;

unsigned long button1ChangeTime = 0;
unsigned long button2ChangeTime = 0;


// ==================================================
// MODES
// ==================================================

enum Mode {
  NORMAL_MODE,
  LIGHT_CONFIG_MODE
};

Mode currentMode = NORMAL_MODE;


// ==================================================
// NORMAL EVENT STATE
// ==================================================

enum EventState {

  // No button activity.
  READY,

  // First button press detected.
  WAITING_FOR_COMBO,

  // First press released.
  // Waiting for second press.
  WAITING_FOR_DOUBLE_PRESS,

  // Normal single press is being held.
  WAITING_FOR_RELEASE,

  // Double press detected.
  // Second press is being held.
  WAVE_ACTIVE
};

EventState eventState = READY;


// ==================================================
// NORMAL PRESS INFORMATION
// ==================================================

int firstPressButton = 0;

unsigned long comboStartTime = 0;

unsigned long firstReleaseTime = 0;


// ==================================================
// CONFIG MODE STATES
// ==================================================

// Prevents the buttons used to enter config
// from immediately changing the brightness.
bool configWaitingForEntryRelease = false;

// Prevents the buttons used to exit config
// from immediately starting a normal-mode event.
bool configExitReleaseLock = false;


// Previous button states for config edge detection.
bool previousConfigButton1 = false;
bool previousConfigButton2 = false;


// ==================================================
// NORMAL LED ANIMATION
// ==================================================

bool animationActive = false;

CRGB animationColor = CRGB::Black;

int nextLED = 0;

unsigned long lastLEDTime = 0;


// ==================================================
// SINE WAVE
// ==================================================

bool waveActive = false;

CRGB waveColor = CRGB::Black;

unsigned long waveStartTime = 0;


// ==================================================
// BRIGHTNESS SETTINGS
// ==================================================

const int brightnessLevels[] = {
  255,   // 80%
  191,   // 60%
  128,   // 40%
  64,    // 20%
  0      // 0%
};

const int brightnessPercent[] = {
  80,
  60,
  40,
  20,
  0
};

const int NUM_BRIGHTNESS_LEVELS = 5;

int brightnessIndex = 0;


// ==================================================
// CONFIG BLINK
// ==================================================

bool configBlinkState = false;

unsigned long lastConfigBlinkTime = 0;


// ==================================================
// ZERO BRIGHTNESS TIMER
// ==================================================

unsigned long zeroBrightnessStartTime = 0;

bool zeroBrightnessTriggered = false;


// ==================================================
// CHECK BATTERY VOLTAGE
// ==================================================

void checkBatteryVoltage() {

  int adcValue = analogRead(BATTERY_ADC_PIN);


  // ------------------------------------------------
  // ADC VOLTAGE
  // ------------------------------------------------
  //
  // 12-bit ADC:
  //
  // 0    = 0 V
  // 4095 = approximately 3.3 V
  //
  // ------------------------------------------------

  float pinVoltage =
    ((float)adcValue / 4095.0) * 3.3;


  // ------------------------------------------------
  // RESISTOR DIVIDER
  // ------------------------------------------------
  //
  // Battery
  //    |
  //   R1 = 18k
  //    |
  //    +---- GPIO 0
  //    |
  //   R2 = 33k
  //    |
  //   GND
  //
  // Vbattery =
  // Vpin * (R1 + R2) / R2
  //
  // ------------------------------------------------

  float batteryVoltage =
    pinVoltage *
    ((BATTERY_R1 + BATTERY_R2) / BATTERY_R2);


  Serial.print("Battery voltage: ");
  Serial.print(batteryVoltage, 2);
  Serial.println(" V");


  // ------------------------------------------------
  // OVER-VOLTAGE SHUTDOWN
  // ------------------------------------------------

  if (batteryVoltage > BATTERY_MAX_VOLTAGE) {

    Serial.println("BATTERY OVER 6V - SHUTDOWN");

    batteryShutdown = true;
  }
}


// ==================================================
// BATTERY MONITOR TASK
// RUNS ON CORE 0
// ==================================================

void batteryMonitorTask(void *parameter) {

  Serial.println("Battery monitor task started");


  // ------------------------------------------------
  // CHECK IMMEDIATELY AT STARTUP
  // ------------------------------------------------

  checkBatteryVoltage();


  // ------------------------------------------------
  // CHECK ONCE PER MINUTE
  // ------------------------------------------------

  while (true) {

    vTaskDelay(
      pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS)
    );


    if (!batteryShutdown) {

      checkBatteryVoltage();
    }
  }
}


// ==================================================
// STOP NORMAL ANIMATION
// ==================================================

void stopAnimation() {

  animationActive = false;

  nextLED = 0;

  FastLED.clear();
  FastLED.show();
}


// ==================================================
// START NORMAL ANIMATION
// ==================================================

void startAnimation(CRGB color) {

  Serial.println("START NORMAL ANIMATION");

  animationColor = color;

  nextLED = 0;

  animationActive = true;


  FastLED.setBrightness(
    brightnessLevels[brightnessIndex]
  );

  FastLED.clear();


  // First LED immediately.
  leds[0] = animationColor;

  FastLED.show();


  nextLED = 1;

  lastLEDTime = millis();
}


// ==================================================
// UPDATE NORMAL ANIMATION
// ==================================================

void updateAnimation() {

  if (!animationActive)
    return;


  unsigned long now = millis();


  if ((now - lastLEDTime) < LED_STEP_TIME_MS)
    return;


  lastLEDTime = now;


  FastLED.setBrightness(
    brightnessLevels[brightnessIndex]
  );


  if (nextLED < NUM_LEDS) {

    leds[nextLED] = animationColor;

    FastLED.show();

    nextLED++;
  }


  if (nextLED >= NUM_LEDS) {

    animationActive = false;
  }
}


// ==================================================
// START WAVE
// ==================================================

void startWave(CRGB color) {

  Serial.println("START DOUBLE-PRESS WAVE");

  waveColor = color;

  waveStartTime = millis();

  waveActive = true;

  animationActive = false;


  FastLED.setBrightness(
    brightnessLevels[brightnessIndex]
  );
}


// ==================================================
// STOP WAVE
// ==================================================

void stopWave() {

  waveActive = false;

  FastLED.clear();
  FastLED.show();
}


// ==================================================
// UPDATE WAVE
// ==================================================

void updateWave() {

  if (!waveActive)
    return;


  unsigned long now = millis();

  unsigned long elapsed =
    now - waveStartTime;


  // Calculate position in the sine wave.
  uint8_t phase =
    (uint8_t)(
      ((elapsed % WAVE_CYCLE_MS) * 255UL)
      / WAVE_CYCLE_MS
    );


  for (int i = 0; i < NUM_LEDS; i++) {

    /*
       The negative offset makes the wave travel
       in the opposite direction.
    */

    uint8_t ledPhase =
      phase - (i * 51);


    uint8_t sineValue =
      sin8(ledPhase);


    // ------------------------------------------------
    // SHARP ON-PULSE
    // ------------------------------------------------

    uint8_t brightness = 0;


    if (sineValue > 70) {

      brightness =
        map(
          sineValue,
          70,
          255,
          0,
          255
        );
    }


    // Sharpen the peak.
    brightness =
      ((uint16_t)brightness * brightness) / 255;


    leds[i] = waveColor;

    leds[i].nscale8(brightness);
  }


  FastLED.setBrightness(
    brightnessLevels[brightnessIndex]
  );

  FastLED.show();
}


// ==================================================
// DRAW CONFIG DISPLAY
// ==================================================

void drawConfigDisplay() {

  int percent =
    brightnessPercent[brightnessIndex];


  int ledsToLight =
    (NUM_LEDS * percent) / 100;


  FastLED.clear();


  // ------------------------------------------------
  // BRIGHTNESS DISPLAY
  // ------------------------------------------------

  for (int i = 0; i < ledsToLight; i++) {

    leds[i] = CRGB::White;
  }


  // ------------------------------------------------
  // CONFIG INDICATOR
  // ------------------------------------------------

  if (configBlinkState) {

    leds[NUM_LEDS - 1] = CRGB::Blue;

  } else {

    leds[NUM_LEDS - 1] = CRGB::Black;
  }


  FastLED.setBrightness(255);

  FastLED.show();
}


// ==================================================
// ENTER CONFIG MODE
// ==================================================

void enterLightConfigMode() {

  Serial.println("ENTER LIGHT CONFIG");

  currentMode = LIGHT_CONFIG_MODE;


  stopAnimation();
  stopWave();


  // Both buttons used to enter config must
  // be released before config controls work.
  configWaitingForEntryRelease = true;


  previousConfigButton1 = true;
  previousConfigButton2 = true;


  configBlinkState = false;

  lastConfigBlinkTime = millis();


  zeroBrightnessStartTime = 0;

  zeroBrightnessTriggered = false;


  drawConfigDisplay();
}


// ==================================================
// EXIT CONFIG MODE
// ==================================================

void exitLightConfigMode() {

  Serial.println("SAVE CONFIG / EXIT LIGHT CONFIG");

  currentMode = NORMAL_MODE;


  /*
     Don't allow the buttons used to exit config
     to immediately trigger a normal event.
  */

  configExitReleaseLock = true;


  previousConfigButton1 = true;
  previousConfigButton2 = true;


  zeroBrightnessStartTime = 0;

  zeroBrightnessTriggered = false;


  stopAnimation();
  stopWave();


  Serial.println("WAITING FOR CONFIG BUTTON RELEASE");
}


// ==================================================
// UPDATE CONFIG BLINK
// ==================================================

void updateConfigBlink() {

  unsigned long now = millis();


  if ((now - lastConfigBlinkTime) >=
      CONFIG_BLINK_TIME_MS) {

    lastConfigBlinkTime = now;

    configBlinkState = !configBlinkState;

    drawConfigDisplay();
  }
}


// ==================================================
// INCREASE BRIGHTNESS
// BUTTON 1
// ==================================================

void increaseBrightness() {

  brightnessIndex++;


  if (brightnessIndex >= NUM_BRIGHTNESS_LEVELS) {

    brightnessIndex = 0;
  }


  int percent =
    brightnessPercent[brightnessIndex];


  Serial.print("Brightness UP: ");
  Serial.print(percent);
  Serial.println("%");


  // ------------------------------------------------
  // ZERO BRIGHTNESS TIMER
  // ------------------------------------------------

  if (percent == 0) {

    zeroBrightnessStartTime = millis();

    zeroBrightnessTriggered = false;

  } else {

    zeroBrightnessStartTime = 0;

    zeroBrightnessTriggered = false;
  }


  drawConfigDisplay();
}


// ==================================================
// DECREASE BRIGHTNESS
// BUTTON 2
// ==================================================

void decreaseBrightness() {

  brightnessIndex--;


  if (brightnessIndex < 0) {

    brightnessIndex =
      NUM_BRIGHTNESS_LEVELS - 1;
  }


  int percent =
    brightnessPercent[brightnessIndex];


  Serial.print("Brightness DOWN: ");
  Serial.print(percent);
  Serial.println("%");


  // ------------------------------------------------
  // ZERO BRIGHTNESS TIMER
  // ------------------------------------------------

  if (percent == 0) {

    zeroBrightnessStartTime = millis();

    zeroBrightnessTriggered = false;

  } else {

    zeroBrightnessStartTime = 0;

    zeroBrightnessTriggered = false;
  }


  drawConfigDisplay();
}


// ==================================================
// ZERO BRIGHTNESS TIMER
// ==================================================

void updateZeroBrightnessTimer() {

  if (brightnessPercent[brightnessIndex] != 0)
    return;


  unsigned long now = millis();


  if (!zeroBrightnessTriggered &&
      (now - zeroBrightnessStartTime >=
       ZERO_BRIGHTNESS_TIMEOUT_MS)) {

    Serial.println("0% for 15 seconds");

    digitalWrite(CONTROL_PIN, LOW);

    zeroBrightnessTriggered = true;
  }
}


// ==================================================
// UPDATE BUTTONS + DEBOUNCE
// ==================================================

void updateButtons() {

  unsigned long now = millis();


  bool button1Raw =
    (digitalRead(BUTTON1_PIN) == LOW);


  bool button2Raw =
    (digitalRead(BUTTON2_PIN) == HIGH);


  // ------------------------------------------------
  // BUTTON 1
  // ------------------------------------------------

  if (button1Raw != lastButton1Raw) {

    lastButton1Raw = button1Raw;

    button1ChangeTime = now;
  }


  if ((now - button1ChangeTime) >= DEBOUNCE_MS) {

    button1Pressed = lastButton1Raw;
  }


  // ------------------------------------------------
  // BUTTON 2
  // ------------------------------------------------

  if (button2Raw != lastButton2Raw) {

    lastButton2Raw = button2Raw;

    button2ChangeTime = now;
  }


  if ((now - button2ChangeTime) >= DEBOUNCE_MS) {

    button2Pressed = lastButton2Raw;
  }
}


// ==================================================
// NORMAL MODE
// ==================================================

void updateNormalMode() {

  unsigned long now = millis();


  uint8_t state = 0;


  if (button1Pressed)
    state |= 0b10;


  if (button2Pressed)
    state |= 0b01;


  // ==================================================
  // BATTERY SHUTDOWN
  // ==================================================

  if (batteryShutdown) {

    animationActive = false;

    waveActive = false;

    FastLED.clear();
    FastLED.show();

    digitalWrite(CONTROL_PIN, LOW);

    return;
  }


  // ==================================================
  // CONFIG EXIT RELEASE LOCK
  // ==================================================

  if (configExitReleaseLock) {

    if (state == 0) {

      configExitReleaseLock = false;

      eventState = READY;

      firstPressButton = 0;


      Serial.println("CONFIG BUTTONS RELEASED");
      Serial.println("NORMAL READY");
    }

    else {

      stopAnimation();
      stopWave();
    }


    return;
  }


  // ==================================================
  // WAVE ACTIVE
  // ==================================================

  if (eventState == WAVE_ACTIVE) {


    // ------------------------------------------------
    // RELEASED
    // ------------------------------------------------

    if (state == 0) {

      stopWave();

      eventState = READY;

      firstPressButton = 0;


      Serial.println("WAVE RELEASED");
    }


    // ------------------------------------------------
    // STILL HOLDING SAME BUTTON
    // ------------------------------------------------

    else {

      if (
        (firstPressButton == 1 &&
         state == 0b10)

        ||

        (firstPressButton == 2 &&
         state == 0b01)
      ) {

        updateWave();
      }


      // ------------------------------------------------
      // OTHER BUTTON / BOTH BUTTONS
      // ------------------------------------------------

      else {

        stopWave();

        eventState = WAITING_FOR_RELEASE;
      }
    }


    return;
  }


  // ==================================================
  // WAITING FOR DOUBLE PRESS
  // ==================================================

  if (eventState == WAITING_FOR_DOUBLE_PRESS) {


    // ------------------------------------------------
    // SECOND PRESS - BUTTON 1
    // ------------------------------------------------

    if (
      firstPressButton == 1 &&
      state == 0b10
    ) {

      Serial.println("DOUBLE PRESS BUTTON 1");

      startWave(CRGB::Green);

      eventState = WAVE_ACTIVE;

      return;
    }


    // ------------------------------------------------
    // SECOND PRESS - BUTTON 2
    // ------------------------------------------------

    if (
      firstPressButton == 2 &&
      state == 0b01
    ) {

      Serial.println("DOUBLE PRESS BUTTON 2");

      startWave(CRGB::Red);

      eventState = WAVE_ACTIVE;

      return;
    }


    // ------------------------------------------------
    // WRONG BUTTON
    // ------------------------------------------------

    if (state != 0) {

      eventState = WAITING_FOR_RELEASE;

      return;
    }


    // ------------------------------------------------
    // DOUBLE PRESS TIMEOUT
    // ------------------------------------------------

    if (
      (now - firstReleaseTime) >=
      DOUBLE_PRESS_WINDOW_MS
    ) {

      Serial.println("SINGLE PRESS COMPLETE");

      eventState = READY;

      firstPressButton = 0;
    }


    return;
  }


  // ==================================================
  // WAITING FOR FIRST COMBO
  // ==================================================

  if (eventState == WAITING_FOR_COMBO) {


    // ------------------------------------------------
    // BOTH BUTTONS = ENTER CONFIG
    // ------------------------------------------------

    if (state == 0b11) {

      Serial.println("BOTH - ENTER CONFIG");

      enterLightConfigMode();

      return;
    }


    // ------------------------------------------------
    // SINGLE BUTTON
    // ------------------------------------------------

    if (
      (now - comboStartTime) >=
      COMBO_WAIT_MS
    ) {


      if (state == 0b10) {

        Serial.println("BUTTON 1 FIRST PRESS");

        firstPressButton = 1;

        startAnimation(CRGB::Green);

        eventState = WAITING_FOR_RELEASE;
      }


      else if (state == 0b01) {

        Serial.println("BUTTON 2 FIRST PRESS");

        firstPressButton = 2;

        startAnimation(CRGB::Red);

        eventState = WAITING_FOR_RELEASE;
      }
    }


    // Continue normal animation while held.
    if (
      state == 0b10 ||
      state == 0b01
    ) {

      updateAnimation();
    }


    return;
  }


  // ==================================================
  // NORMAL WAITING FOR RELEASE
  // ==================================================

  if (eventState == WAITING_FOR_RELEASE) {


    // ------------------------------------------------
    // BUTTON RELEASED
    // ------------------------------------------------

    if (state == 0) {

      stopAnimation();


      if (firstPressButton != 0) {

        firstReleaseTime = now;

        eventState = WAITING_FOR_DOUBLE_PRESS;


        Serial.println("WAITING FOR SECOND PRESS");
      }

      else {

        eventState = READY;
      }


      return;
    }


    // ------------------------------------------------
    // NORMAL ANIMATION WHILE HELD
    // ------------------------------------------------

    if (
      state == 0b10 ||
      state == 0b01
    ) {

      updateAnimation();
    }


    return;
  }


  // ==================================================
  // READY
  // ==================================================

  if (eventState == READY) {

    if (state != 0) {

      comboStartTime = now;

      eventState = WAITING_FOR_COMBO;
    }
  }
}


// ==================================================
// LIGHT CONFIG MODE
// ==================================================

void updateLightConfigMode() {


  // ==================================================
  // WAIT FOR ORIGINAL BOTH PRESS TO RELEASE
  // ==================================================

  if (configWaitingForEntryRelease) {

    if (
      !button1Pressed &&
      !button2Pressed
    ) {

      configWaitingForEntryRelease = false;

      previousConfigButton1 = false;
      previousConfigButton2 = false;


      Serial.println("CONFIG READY");
    }


    updateConfigBlink();

    updateZeroBrightnessTimer();

    return;
  }


  // ==================================================
  // BOTH BUTTONS = SAVE / EXIT
  // ==================================================

  if (
    button1Pressed &&
    button2Pressed
  ) {

    exitLightConfigMode();

    return;
  }


  // ==================================================
  // BUTTON 1 = BRIGHTNESS UP
  // ==================================================

  if (
    button1Pressed &&
    !previousConfigButton1
  ) {

    increaseBrightness();
  }


  previousConfigButton1 = button1Pressed;


  // ==================================================
  // BUTTON 2 = BRIGHTNESS DOWN
  // ==================================================

  if (
    button2Pressed &&
    !previousConfigButton2
  ) {

    decreaseBrightness();
  }


  previousConfigButton2 = button2Pressed;


  // ==================================================
  // CONFIG BLINK
  // ==================================================

  updateConfigBlink();


  // ==================================================
  // ZERO BRIGHTNESS TIMER
  // ==================================================

  updateZeroBrightnessTimer();
}


// ==================================================
// START BATTERY MONITOR
// ==================================================

void startBatteryMonitor() {

  xTaskCreatePinnedToCore(
    batteryMonitorTask,
    "BatteryMonitor",
    4096,
    NULL,
    1,
    &batteryTaskHandle,
    0
  );

  Serial.println("BATTERY MONITOR STARTED");
}


// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);


  // --------------------------------------------------
  // BUTTON 1
  // HIGH = idle
  // LOW  = pressed
  // --------------------------------------------------

  pinMode(
    BUTTON1_PIN,
    INPUT_PULLUP
  );


  // --------------------------------------------------
  // BUTTON 2
  // LOW  = idle
  // HIGH = pressed
  // --------------------------------------------------

  pinMode(
    BUTTON2_PIN,
    INPUT_PULLDOWN
  );


  // --------------------------------------------------
  // CONTROL PIN
  // --------------------------------------------------

  pinMode(
    CONTROL_PIN,
    OUTPUT
  );

  digitalWrite(
    CONTROL_PIN,
    HIGH
  );


  // --------------------------------------------------
  // BATTERY ADC
  // --------------------------------------------------

  pinMode(
    BATTERY_ADC_PIN,
    INPUT
  );

  analogReadResolution(12);


  // --------------------------------------------------
  // LED STRIP
  // --------------------------------------------------

  FastLED.addLeds<
    LED_TYPE,
    LED_PIN,
    COLOR_ORDER
  >(
    leds,
    NUM_LEDS
  );


  FastLED.setBrightness(
    brightnessLevels[brightnessIndex]
  );


  FastLED.clear();
  FastLED.show();


  // --------------------------------------------------
  // INITIAL BUTTON STATES
  // --------------------------------------------------

  lastButton1Raw =
    (digitalRead(BUTTON1_PIN) == LOW);


  lastButton2Raw =
    (digitalRead(BUTTON2_PIN) == HIGH);


  button1Pressed =
    lastButton1Raw;


  button2Pressed =
    lastButton2Raw;


  // --------------------------------------------------
  // START BATTERY MONITOR
  // --------------------------------------------------

  startBatteryMonitor();


  Serial.println("SYSTEM STARTED");
}


// ==================================================
// MAIN LOOP
// ==================================================

void loop() {

  updateButtons();


  // ==================================================
  // BATTERY SHUTDOWN
  // ==================================================

  if (batteryShutdown) {

    animationActive = false;

    waveActive = false;


    FastLED.clear();
    FastLED.show();


    digitalWrite(
      CONTROL_PIN,
      LOW
    );


    // Stay shut down.
    delay(100);

    return;
  }


  // ==================================================
  // MAIN MODE
  // ==================================================

  if (currentMode == NORMAL_MODE) {

    updateNormalMode();
  }


  else if (
    currentMode == LIGHT_CONFIG_MODE
  ) {

    updateLightConfigMode();
  }
}

