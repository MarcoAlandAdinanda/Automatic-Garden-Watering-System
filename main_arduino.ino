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
// Digital pins
const int pin_PING = 13;          // Single pin for both Trigger and Echo
const int pin_LED_red = 12;       // System ON Indicator
const int pin_LED_green = 11;     // Pump Active Indicator
const int pin_LED_blue = 10;      // Valve Active Indicator
const int pin_mosfet_pump = 5;    // Pump relay/MOSFET
const int pin_push_button = 7;    // Navigation button
const int pin_pos_servo = 6;      // Water Valve Actuator

// Analog pins
const int pin_potentiometer = A0; // Menu scrolling and value adjustment
const int pin_moisture = A1;      // Soil moisture sensor

// ==========================================
// 2. GLOBAL SYSTEM VARIABLES
// ==========================================
// Main State Machine Tracker
int system_state = 0;
/*
  0: Home Screen (Welcome)
  1: Menu Screen (Select Run vs Settings)
  2: Operating Screen (System Actively Monitoring & Watering)
  3: Settings Screen (Adjust thresholds)
*/

// Sub-Menu Trackers (Used inside the Settings state)
int setting_mode = 0;     // 0 = Browsing the list, 1 = Editing a specific value
int setting_index = 0;    // Tracks which setting is currently highlighted

// System Thresholds & Settings (With Default Base Values)
int servo_angle = 90;     // Angle to open the valve (0-180 deg)
int refill_distance = 70; // Distance from top of tank to trigger refill (cm)
int moist_min = 30;       // Lower limit: Trigger pump when soil drops below this (%)
int moist_max = 70;       // Upper limit: Stop pump when soil reaches this (%)
int pump_power = 255;     // (Legacy) Pump speed placeholder

// Hardware Tracking
bool is_pumping = false;  // Tracks if the pump is currently running
bool is_refilling = false;// Tracks if the valve is currently open

// Edge Detection Variables (For crisp button presses)
int button_status = LOW;
int last_button_status = LOW;
bool button_clicked = false;

// Sensor Storage Variables
float analog_value;
int moisture_value;
float moisture_percentage;

// Initialize Components
Adafruit_LiquidCrystal lcd(0);
Servo valveServo;


// ==========================================
// 3. SETUP FUNCTION
// Runs once on startup to initialize pins and hardware
// ==========================================
void setup(){
  pinMode(pin_LED_red, OUTPUT);
  pinMode(pin_LED_green, OUTPUT);
  pinMode(pin_LED_blue, OUTPUT);
  pinMode(pin_mosfet_pump, OUTPUT); 
  pinMode(pin_push_button, INPUT); 
  
  // Attach servo with explicit minimum and maximum pulse widths (500us to 2500us).
  // This is required for many standard micro-servos to properly hit 0 and 180 degrees.
  valveServo.attach(pin_pos_servo, 500, 2500); 
  valveServo.write(0); // Ensure valve starts in the closed position
  
  lcd.begin(16, 2);
  lcd.setBacklight(1);
  
  Serial.begin(9600);
}


// ==========================================
// 4. MAIN LOOP (THE STATE MACHINE CONTROLLER)
// Runs continuously, directing traffic to the correct screen function
// ==========================================
void loop(){
  // --- EDGE DETECTION BLOCK ---
  // This logic ensures holding the button down doesn't rapidly skip through menus.
  // It only registers 'true' on the exact millisecond the button transitions from LOW to HIGH.
  button_status = digitalRead(pin_push_button);
  if (button_status == HIGH && last_button_status == LOW) {
    button_clicked = true;  
  } else {
    button_clicked = false; 
  }
  last_button_status = button_status; // Save current state for the next loop

  // Turn on System Power Indicator
  digitalWrite(pin_LED_red, HIGH); 
  
  // --- STATE MACHINE ROUTING ---
  // 'else if' is used so only ONE state can be evaluated per loop cycle
  if(system_state == 0){
    homeScreen();
  }
  else if(system_state == 1){
    menuScreen();
  }
  else if(system_state == 2){
    operatingScreen();
  }
  else if(system_state == 3){
    settingScreen();
  }
}


// ==========================================
// 5. SCREEN FUNCTIONS
// ==========================================

void homeScreen(){
  // Simple splash screen waiting for a user click to begin
  lcd.setCursor(0, 0);
  lcd.print("    WELCOME!    ");
  lcd.setCursor(0, 1);
  lcd.print("<PRESS TO START>");
  
  if(button_clicked){
    system_state = 1; // Transition to Menu
    lcd.clear();
  }
}

void menuScreen(){
  analog_value = analogRead(pin_potentiometer);

  // Divide potentiometer into two halves (Left = Run, Right = Settings)
  if(analog_value <= (1023.0 / 2)){
    lcd.setCursor(0, 0);
    lcd.print("*RUN THE SYSTEM*");
    lcd.setCursor(0, 1);
    lcd.print(" SETTINGS       "); 
    
    if(button_clicked) {
      // Safety Reset: Ensure actuators are declared OFF before starting
      is_pumping = false;
      is_refilling = false;
      system_state = 2; // Transition to Operating Mode
      lcd.clear();
    }
  }
  else{
    lcd.setCursor(0, 0);
    lcd.print(" RUN THE SYSTEM "); 
    lcd.setCursor(0, 1);
    lcd.print("*SETTINGS*      "); 
    
    if(button_clicked) {
      system_state = 3; // Transition to Settings Mode
      setting_mode = 0; // Ensure Settings starts in "Browse" mode
      lcd.clear();
    }
  }
}

void settingScreen(){
  analog_value = analogRead(pin_potentiometer);

  // SUB-STATE 0: BROWSING SETTINGS
  if (setting_mode == 0) {
    // Map the 0-1023 analog value into 6 distinct menu items (0 through 5)
    setting_index = map(analog_value, 0, 1023, 0, 5); 
    
    lcd.setCursor(0, 0);
    lcd.print("--- SETTINGS ---");
    lcd.setCursor(0, 1);
    
    switch(setting_index) {
      case 0: lcd.print("< BACK         >"); break;
      case 1: lcd.print("< VALVE ANGLE  >"); break;
      case 2: lcd.print("< REFILL DIST. >"); break;
      case 3: lcd.print("< MOISTURE MIN >"); break;
      case 4: lcd.print("< MOISTURE MAX >"); break;
      case 5: lcd.print("< PUMP POWER   >"); break;
    }

    if (button_clicked) {
      if (setting_index == 0) {
        system_state = 1; // Return to Main Menu
      } else {
        setting_mode = 1; // Enter "Edit" mode for the selected variable
      }
      lcd.clear();
    }
  } 
  
  // SUB-STATE 1: EDITING A SETTING
  else if (setting_mode == 1) {
    lcd.setCursor(0, 0);
    
    // Dynamically adjust the chosen variable based on the potentiometer
    switch(setting_index) {
      case 1: 
        servo_angle = map(analog_value, 0, 1023, 0, 180);
        lcd.print("SET VALVE ANGLE ");
        lcd.setCursor(0, 1);
        lcd.print(servo_angle); lcd.print(" deg        "); // Padded spaces erase ghost characters
        break;
      case 2: 
        refill_distance = map(analog_value, 0, 1023, 0, 100);
        lcd.print("SET REFILL DIST.");
        lcd.setCursor(0, 1);
        lcd.print(refill_distance); lcd.print(" cm         ");
        break;
      case 3: 
        moist_min = map(analog_value, 0, 1023, 0, 100);
        lcd.print("SET MOIST. MIN  ");
        lcd.setCursor(0, 1);
        lcd.print(moist_min); lcd.print(" %          ");
        break;
      case 4: 
        moist_max = map(analog_value, 0, 1023, 0, 100);
        lcd.print("SET MOIST. MAX  ");
        lcd.setCursor(0, 1);
        lcd.print(moist_max); lcd.print(" %          ");
        break;
      case 5: 
        pump_power = map(analog_value, 0, 1023, 0, 255);
        lcd.print("SET PUMP POWER  ");
        lcd.setCursor(0, 1);
        lcd.print(pump_power); lcd.print(" PWM        ");
        break;
    }

    // Save the new value and return to Browse mode
    if (button_clicked) {
      setting_mode = 0; 
      lcd.clear();
    }
  }
}


// ==========================================
// 6. OPERATING LOGIC (CORE AUTOMATION)
// Monitors sensors, evaluates thresholds, and triggers actuators
// ==========================================
void operatingScreen(){
  // Step A: Read Sensors
  float current_moist = calculateMoisturePercentage();
  long current_dist = readDistancePING();

  // Step B: Evaluate Pump (Soil Moisture)
  // Trigger pump if soil is dry (below min). Stop pump if soil is wet enough (above max).
  if (current_moist <= moist_min) {
    is_pumping = true; 
  } else if (current_moist >= moist_max) {
    is_pumping = false; 
  }

  // Actuate Pump
  // IMPORTANT HARDWARE NOTE: The <Servo.h> library disables PWM (analogWrite) on Pins 9 & 10.
  // Because the pump is on Pin 9, we MUST use digitalWrite to safely turn it on and off.
  if (is_pumping) {
    digitalWrite(pin_mosfet_pump, HIGH);
    digitalWrite(pin_LED_green, HIGH);
  } else {
    digitalWrite(pin_mosfet_pump, LOW);
    digitalWrite(pin_LED_green, LOW);
  }

  // Step C: Evaluate Valve (Water Tank Refill)
  // Trigger valve if water level is far down (distance is great). Stop when distance is short.
  if (current_dist >= refill_distance) {
    is_refilling = true; 
  } else if (current_dist < refill_distance) {
    is_refilling = false; 
  }

  // Actuate Valve
  if (is_refilling) {
    valveServo.write(servo_angle); // Open valve to custom angle
    digitalWrite(pin_LED_blue, HIGH);
  } else {
    valveServo.write(0);           // Close valve (0 degrees)
    digitalWrite(pin_LED_blue, LOW);
  }

  // Step D: Update Display
  // Static Coordinates are used (e.g., setCursor(9, 0)) to prevent text from shifting
  // left or right when the sensor values change from 1 digit to 2 digits to 3 digits.
  lcd.setCursor(0, 0);
  lcd.print("M:"); lcd.print((int)current_moist); lcd.print("%   "); 
  lcd.setCursor(9, 0); 
  if(is_pumping) lcd.print("PMP:ON "); else lcd.print("PMP:OFF");
  
  lcd.setCursor(0, 1);
  lcd.print("D:"); lcd.print(current_dist); lcd.print("cm  "); 
  lcd.setCursor(9, 1); 
  if(is_refilling) lcd.print("VLV:ON "); else lcd.print("VLV:OFF");

  // Step E: Safety Shutdown & Exit Condition
  // If the user clicks to exit to the menu, we MUST force all actuators off immediately
  // so the pump doesn't keep running while we are navigating settings.
  if (button_clicked) {
    digitalWrite(pin_mosfet_pump, LOW);
    digitalWrite(pin_LED_green, LOW);
    valveServo.write(0);
    digitalWrite(pin_LED_blue, LOW);
    is_pumping = false;
    is_refilling = false;

    system_state = 1; // Return to Menu
    lcd.clear();
  }
}


// ==========================================
// 7. SENSOR PROCESSING FUNCTIONS
// ==========================================

// Reads the analog soil moisture sensor and maps it to a percentage
float calculateMoisturePercentage(){
  moisture_value = analogRead(pin_moisture);
  moisture_percentage = (moisture_value / 876.00) * 100;
  // Use constrain() to prevent readings below 0% or above 100% in extreme conditions
  return constrain(moisture_percentage, 0, 100); 
}

// Triggers the ultrasonic sensor and converts the echo time into distance (cm)
long readDistancePING() {
  // Pulse the trigger
  pinMode(pin_PING, OUTPUT);
  digitalWrite(pin_PING, LOW);
  delayMicroseconds(2);
  digitalWrite(pin_PING, HIGH);
  delayMicroseconds(5);
  digitalWrite(pin_PING, LOW);

  // Read the echo length
  pinMode(pin_PING, INPUT);
  long duration = pulseIn(pin_PING, HIGH);
  
  // Convert time to distance (Speed of sound is ~29 microseconds per cm)
  // Divide by 2 because the sound wave travels out to the object AND back.
  return duration / 29 / 2; 
}
