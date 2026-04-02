/*
================================================================================
AUTOMATIC PLANT WATERING SYSTEM
For Industrial Automation Course Project

Developed by:
- Marco Aland Adinanda (23/514817/TK/56524)

ARCHITECTURE OVERVIEW:
This system uses a "Finite State Machine" (FSM) approach. Instead of running 
everything at once, the system exists in one of four distinct 'states' (screens). 
The main loop constantly checks which state is active and only runs that specific 
function, ensuring the menu is snappy and the pump only runs when authorized.

================================================================================
*/

#include <Adafruit_LiquidCrystal.h>
#include <Servo.h>

// ==========================================
// 1. HARDWARE PIN DEFINITIONS
// ==========================================
const int pin_PING         = 13; // Single pin for both Trigger and Echo
const int pin_LED_red      = 12; // System ON Indicator
const int pin_LED_green    = 11; // Pump Active Indicator
const int pin_LED_blue     = 10; // Valve Active Indicator
const int pin_mosfet_pump  = 5;  // Pump MOSFET — Pin 5 fully supports analogWrite
const int pin_push_button  = 7;  // Navigation button
const int pin_pos_servo    = 6;  // Water Valve Actuator

const int pin_potentiometer = A0; // Menu scrolling / value adjustment
const int pin_moisture      = A1; // Soil moisture sensor


// ==========================================
// 2. GLOBAL SYSTEM VARIABLES
// ==========================================
int system_state = 0;
/*
  0: Home Screen  (Welcome splash)
  1: Menu Screen  (Run vs Settings)
  2: Operating    (Actively monitoring & watering)
  3: Settings     (Adjust thresholds)
*/

int setting_mode  = 0; // 0 = browsing list, 1 = editing a value
int setting_index = 0; // which settings row is highlighted

// --- System Thresholds ---
int servo_angle      = 90;  // Valve open angle (0–180 deg)
int refill_distance  = 70;  // Distance (cm) that triggers tank refill
int moist_min        = 30;  // Below this → pump full speed (%)
int moist_max        = 70;  // At or above this → pump off (%)
// [FIX #5] pump_power is now the maximum PWM ceiling applied to Zone 1 & 2.
//          Operator can tune max motor speed without touching the threshold logic.
int pump_power       = 255; // Maximum pump PWM (0–255)

// --- Actuator State ---
bool is_pumping   = false;
bool is_refilling = false;
int  pump_pwm     = 0;      // Actual PWM value sent to the motor this cycle

// [FIX #7] Debounce: track timestamp of the last valid press
unsigned long last_click_ms  = 0;
const unsigned long DEBOUNCE_MS = 50; // Ignore bounces shorter than 50 ms

// --- Button Edge Detection ---
int  button_status      = LOW;
int  last_button_status = LOW;
bool button_clicked     = false;

// --- Sensor Readings ---
float analog_value;
int   moisture_value;
float moisture_percentage;

// --- Hardware Objects ---
Adafruit_LiquidCrystal lcd(0);
Servo valveServo;


// ==========================================
// 3. SETUP
// ==========================================
void setup() {
  pinMode(pin_LED_red,     OUTPUT);
  pinMode(pin_LED_green,   OUTPUT);
  pinMode(pin_LED_blue,    OUTPUT);
  pinMode(pin_mosfet_pump, OUTPUT);
  pinMode(pin_push_button, INPUT);

  valveServo.attach(pin_pos_servo, 500, 2500);
  valveServo.write(0); // Close valve on startup

  lcd.begin(16, 2);
  lcd.setBacklight(1);

  Serial.begin(9600);
}


// ==========================================
// 4. MAIN LOOP
// ==========================================
void loop() {
  // --- [FIX #7] Debounced edge detection ---
  // A click is only registered if:
  //   (a) The button transitioned from LOW to HIGH, AND
  //   (b) At least DEBOUNCE_MS milliseconds have passed since the last click.
  // This prevents mechanical switch bounce from registering phantom presses.
  button_status = digitalRead(pin_push_button);

  if (button_status == HIGH && last_button_status == LOW) {
    unsigned long now = millis();
    if (now - last_click_ms >= DEBOUNCE_MS) {
      button_clicked = true;
      last_click_ms  = now; // Latch timestamp of this valid press
    } else {
      button_clicked = false; // Too soon — bounce, ignore
    }
  } else {
    button_clicked = false;
  }
  last_button_status = button_status;

  digitalWrite(pin_LED_red, HIGH); // System power indicator always on

  // State routing
  if      (system_state == 0) homeScreen();
  else if (system_state == 1) menuScreen();
  else if (system_state == 2) operatingScreen();
  else if (system_state == 3) settingScreen();
}


// ==========================================
// 5. SCREEN FUNCTIONS
// ==========================================

void homeScreen() {
  lcd.setCursor(0, 0); lcd.print("    WELCOME!    ");
  lcd.setCursor(0, 1); lcd.print("<PRESS TO START>");

  if (button_clicked) {
    system_state = 1;
    lcd.clear();
  }
}

void menuScreen() {
  analog_value = analogRead(pin_potentiometer);

  if (analog_value <= (1023.0 / 2)) {
    lcd.setCursor(0, 0); lcd.print("*RUN THE SYSTEM*");
    lcd.setCursor(0, 1); lcd.print(" SETTINGS       ");

    if (button_clicked) {
      is_pumping   = false;
      is_refilling = false;
      system_state = 2;
      lcd.clear();
    }
  } else {
    lcd.setCursor(0, 0); lcd.print(" RUN THE SYSTEM ");
    lcd.setCursor(0, 1); lcd.print("*SETTINGS*      ");

    if (button_clicked) {
      system_state = 3;
      setting_mode = 0;
      lcd.clear();
    }
  }
}

void settingScreen() {
  analog_value = analogRead(pin_potentiometer);

  // --- SUB-STATE 0: BROWSING ---
  if (setting_mode == 0) {
    setting_index = map(analog_value, 0, 1023, 0, 5);

    lcd.setCursor(0, 0); lcd.print("--- SETTINGS ---");
    lcd.setCursor(0, 1);
    switch (setting_index) {
      case 0: lcd.print("< BACK         >"); break;
      case 1: lcd.print("< VALVE ANGLE  >"); break;
      case 2: lcd.print("< REFILL DIST. >"); break;
      case 3: lcd.print("< MOISTURE MIN >"); break;
      case 4: lcd.print("< MOISTURE MAX >"); break;
      // [FIX #5] Renamed label to clarify it now controls max speed cap
      case 5: lcd.print("< MAX PWM CAP  >"); break;
    }

    if (button_clicked) {
      if (setting_index == 0) system_state = 1;
      else                    setting_mode = 1;
      lcd.clear();
    }
  }

  // --- SUB-STATE 1: EDITING ---
  else if (setting_mode == 1) {
    lcd.setCursor(0, 0);

    switch (setting_index) {
      case 1:
        servo_angle = map(analog_value, 0, 1023, 0, 180);
        lcd.print("SET VALVE ANGLE ");
        lcd.setCursor(0, 1);
        lcd.print(servo_angle); lcd.print(" deg        ");
        break;

      case 2:
        refill_distance = map(analog_value, 0, 1023, 0, 100);
        lcd.print("SET REFILL DIST.");
        lcd.setCursor(0, 1);
        lcd.print(refill_distance); lcd.print(" cm         ");
        break;

      case 3:
        // [FIX #2] Clamp moist_min to always be at least 1 below moist_max.
        // This prevents the inverted-settings bug and the division-by-zero bug
        // at the source — before the bad value is ever stored.
        moist_min = map(analog_value, 0, 1023, 0, 100);
        moist_min = constrain(moist_min, 0, moist_max - 1);
        lcd.print("SET MOIST. MIN  ");
        lcd.setCursor(0, 1);
        lcd.print(moist_min); lcd.print(" %  <"); lcd.print(moist_max); lcd.print("% ");
        break;

      case 4:
        // [FIX #2] Clamp moist_max to always be at least 1 above moist_min.
        moist_max = map(analog_value, 0, 1023, 0, 100);
        moist_max = constrain(moist_max, moist_min + 1, 100);
        lcd.print("SET MOIST. MAX  ");
        lcd.setCursor(0, 1);
        lcd.print(moist_max); lcd.print(" %  >"); lcd.print(moist_min); lcd.print("% ");
        break;

      case 5:
        // [FIX #5] pump_power is now used as a real max PWM cap in operatingScreen.
        pump_power = map(analog_value, 0, 1023, 0, 255);
        lcd.print("SET MAX PWM CAP ");
        lcd.setCursor(0, 1);
        lcd.print(pump_power); lcd.print(" PWM        ");
        break;
    }

    if (button_clicked) {
      setting_mode = 0;
      lcd.clear();
    }
  }
}


// ==========================================
// 6. OPERATING LOGIC
// ==========================================
void operatingScreen() {
  // Step A: Read sensors
  float current_moist = calculateMoisturePercentage();
  long  current_dist  = readDistancePING();

  // ============================================================
  // Step B: Proportional Pump Speed (all 3 bug fixes applied)
  //
  //  Zone 1 | moisture < moist_min
  //          → DRY: pump at max cap (pump_power)       [FIX #5]
  //
  //  Zone 2 | moist_min <= moisture < moist_max
  //          → PARTIAL: linearly scale pump_power → 0   [FIX #5]
  //          → Boundary changed to strict < moist_max   [FIX #3]
  //
  //  Zone 3 | moisture >= moist_max
  //          → WET: pump off
  //
  //  Guard   | moist_min >= moist_max (should never happen
  //            after FIX #2, but kept as a runtime failsafe) [FIX #1]
  // ============================================================

  // [FIX #1] Runtime safety guard — if thresholds are equal or inverted,
  // skip the map() call entirely to avoid division by zero.
  if (moist_min >= moist_max) {
    pump_pwm  = 0;
    is_pumping = false;
  }
  else if (current_moist < moist_min) {
    // Zone 1: soil is DRY → full speed up to the PWM cap
    pump_pwm  = pump_power;  // [FIX #5] respects operator-set ceiling
    is_pumping = true;
  }
  else if (current_moist < moist_max) {
    // Zone 2: soil is PARTIAL → proportional speed, mapped through PWM cap
    // [FIX #3] Strict < moist_max so at moist_max the pump is cleanly off.
    pump_pwm = map((int)current_moist, moist_min, moist_max, pump_power, 0);
    pump_pwm = constrain(pump_pwm, 0, pump_power);
    // [FIX #3] Derive is_pumping from actual PWM, not from the zone alone.
    //          Prevents the LED lying when PWM rounds down to 0 near moist_max.
    is_pumping = (pump_pwm > 0);
  }
  else {
    // Zone 3: soil is WET → pump off
    pump_pwm  = 0;
    is_pumping = false;
  }

  analogWrite(pin_mosfet_pump, pump_pwm);
  digitalWrite(pin_LED_green, is_pumping ? HIGH : LOW);

  // Step C: Valve — tank water level
  // [FIX #4] readDistancePING() now returns -1 on sensor timeout.
  //          Treat -1 as "sensor error" → leave valve in its current state
  //          and warn on the LCD rather than silently closing it.
  if (current_dist == -1) {
    // Sensor error: do not change is_refilling. The valve holds its last state.
    // (A deliberate safe-hold rather than a silent snap to "full tank")
  } else if (current_dist >= refill_distance) {
    is_refilling = true;
  } else {
    is_refilling = false;
  }

  if (is_refilling) {
    valveServo.write(servo_angle);
    digitalWrite(pin_LED_blue, HIGH);
  } else {
    valveServo.write(0);
    digitalWrite(pin_LED_blue, LOW);
  }

  // Step D: Update LCD
  // [FIX #6] Layout redesigned to show all four pieces of information:
  //          moisture, pump PWM, distance, and valve state — simultaneously.
  //
  //  Row 0: "M:XX%  P:XXX   "  (moisture left, pump PWM right)
  //  Row 1: "D:XXcm  VLV:ON "  (distance left, valve state right)
  //  On PING error, row 1 reads "D:ERR   VLV:HLD"
  //
  // Fixed cursor positions prevent digit-count changes from shifting text.

  lcd.setCursor(0, 0);
  lcd.print("M:"); lcd.print((int)current_moist); lcd.print("%      ");
  lcd.setCursor(7, 0);
  lcd.print("P:"); lcd.print(pump_pwm); lcd.print("   ");

  lcd.setCursor(0, 1);
  if (current_dist == -1) {
    lcd.print("D:ERR   "); // [FIX #4] Visible sensor error instead of silent D:0
  } else {
    lcd.print("D:"); lcd.print(current_dist); lcd.print("cm    ");
  }
  lcd.setCursor(8, 1);
  if (current_dist == -1) {
    lcd.print("VLV:HLD "); // Hold — neither confirmed open nor closed
  } else if (is_refilling) {
    lcd.print("VLV:ON  ");
  } else {
    lcd.print("VLV:OFF ");
  }

  // Step E: Exit & safety shutdown
  if (button_clicked) {
    analogWrite(pin_mosfet_pump, 0);
    digitalWrite(pin_LED_green, LOW);
    valveServo.write(0);
    digitalWrite(pin_LED_blue, LOW);
    is_pumping    = false;
    is_refilling  = false;
    pump_pwm      = 0;
    system_state  = 1;
    lcd.clear();
  }
}


// ==========================================
// 7. SENSOR FUNCTIONS
// ==========================================

float calculateMoisturePercentage() {
  moisture_value      = analogRead(pin_moisture);
  moisture_percentage = (moisture_value / 876.00) * 100.0;
  return constrain(moisture_percentage, 0.0, 100.0);
}

// [FIX #4] Returns distance in cm, or -1 if no echo was received within
// the sensor's reliable range (~400 cm → ~23 200 µs round-trip).
// The previous version returned 0 on timeout, which silently read as
// "distance = 0 cm" → "tank is full" → valve stays closed indefinitely.
long readDistancePING() {
  // Trigger pulse
  pinMode(pin_PING, OUTPUT);
  digitalWrite(pin_PING, LOW);
  delayMicroseconds(2);
  digitalWrite(pin_PING, HIGH);
  delayMicroseconds(5);
  digitalWrite(pin_PING, LOW);

  // Read echo
  pinMode(pin_PING, INPUT);
  // Timeout set to 23 200 µs (400 cm round-trip at speed of sound).
  // pulseIn() returns 0 if no pulse arrives within the timeout.
  long duration = pulseIn(pin_PING, HIGH, 23200);

  if (duration == 0) return -1; // Timeout: sensor error or out of range

  return duration / 29 / 2;
}
