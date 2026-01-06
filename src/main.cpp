/*
 * ESP32 Grout Pump Control System
 * 
 * Controls a hydraulic valve using 2 SSR outputs based on wireless remote inputs.
 * Supports manual control and automatic cycling mode with end-stop detection.
 */

#include <Arduino.h>

// ========== PIN DEFINITIONS ==========
// GPO Outputs - Control SSRs for hydraulic valve
const int GPO1_PIN = 25;  // SSR 1 output
const int GPO2_PIN = 26;  // SSR 2 output

// GPI Inputs - Wireless Remote Control (momentary switches)
const int INPUT_A_PIN = 32;  // Manual control for GPO1
const int INPUT_B_PIN = 33;  // Manual control for GPO2
const int INPUT_C_PIN = 34;  // Start automatic loop mode
const int INPUT_D_PIN = 35;  // Stop automatic loop mode

// GPI Inputs - End Stop Sensors
const int ENDSTOP_IN_PIN = 36;   // End stop for "in" position
const int ENDSTOP_OUT_PIN = 39;  // End stop for "out" position

// ========== CONSTANTS ==========
const unsigned long DEBOUNCE_DELAY = 50;  // Debounce time in milliseconds
const unsigned long CYCLE_DELAY = 500;    // Delay between cycle direction changes

// ========== STATE VARIABLES ==========
// System mode
enum SystemMode {
  MODE_MANUAL,
  MODE_AUTO_LOOP
};

enum CycleDirection {
  CYCLE_IN,
  CYCLE_OUT,
  CYCLE_STOPPED
};

SystemMode currentMode = MODE_MANUAL;
CycleDirection cycleDirection = CYCLE_STOPPED;

// Input state tracking for debouncing
struct ButtonState {
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  bool pressed;  // True when a valid press is detected
};

ButtonState inputA = {HIGH, HIGH, 0, false};
ButtonState inputB = {HIGH, HIGH, 0, false};
ButtonState inputC = {HIGH, HIGH, 0, false};
ButtonState inputD = {HIGH, HIGH, 0, false};

unsigned long lastCycleTime = 0;

// ========== SETUP ==========
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  Serial.println("ESP32 Grout Pump Control System Starting...");
  
  // Configure GPO pins as outputs
  pinMode(GPO1_PIN, OUTPUT);
  pinMode(GPO2_PIN, OUTPUT);
  digitalWrite(GPO1_PIN, LOW);
  digitalWrite(GPO2_PIN, LOW);
  
  // Configure GPI pins as inputs with pull-up resistors
  // Note: GPIO 32-33 support internal pull-ups
  pinMode(INPUT_A_PIN, INPUT_PULLUP);
  pinMode(INPUT_B_PIN, INPUT_PULLUP);
  
  // GPIO 34-39 are input-only and do NOT support internal pull-ups
  // External pull-up resistors (10kΩ) should be added if switches connect to ground
  pinMode(INPUT_C_PIN, INPUT);
  pinMode(INPUT_D_PIN, INPUT);
  pinMode(ENDSTOP_IN_PIN, INPUT);
  pinMode(ENDSTOP_OUT_PIN, INPUT);
  
  Serial.println("System initialized in MANUAL mode");
  Serial.println("Pin Configuration:");
  Serial.println("  GPO1 (SSR1): GPIO " + String(GPO1_PIN));
  Serial.println("  GPO2 (SSR2): GPIO " + String(GPO2_PIN));
  Serial.println("  Input A (Manual GPO1): GPIO " + String(INPUT_A_PIN));
  Serial.println("  Input B (Manual GPO2): GPIO " + String(INPUT_B_PIN));
  Serial.println("  Input C (Start Loop): GPIO " + String(INPUT_C_PIN));
  Serial.println("  Input D (Stop Loop): GPIO " + String(INPUT_D_PIN));
  Serial.println("  End Stop IN: GPIO " + String(ENDSTOP_IN_PIN));
  Serial.println("  End Stop OUT: GPIO " + String(ENDSTOP_OUT_PIN));
}

// ========== MAIN LOOP ==========
void loop() {
  // Read and debounce all inputs
  updateButtonState(&inputA, INPUT_A_PIN);
  updateButtonState(&inputB, INPUT_B_PIN);
  updateButtonState(&inputC, INPUT_C_PIN);
  updateButtonState(&inputD, INPUT_D_PIN);
  
  // Check for mode change requests
  if (inputC.pressed) {
    if (currentMode != MODE_AUTO_LOOP) {
      currentMode = MODE_AUTO_LOOP;
      cycleDirection = CYCLE_OUT;  // Start by extending
      lastCycleTime = millis();
      Serial.println("Switched to AUTO LOOP mode - starting with OUT cycle");
    }
    inputC.pressed = false;
  }
  
  if (inputD.pressed) {
    if (currentMode == MODE_AUTO_LOOP) {
      currentMode = MODE_MANUAL;
      cycleDirection = CYCLE_STOPPED;
      digitalWrite(GPO1_PIN, LOW);
      digitalWrite(GPO2_PIN, LOW);
      Serial.println("Switched to MANUAL mode - all outputs OFF");
    }
    inputD.pressed = false;
  }
  
  // Execute based on current mode
  if (currentMode == MODE_MANUAL) {
    handleManualMode();
  } else {
    handleAutoLoopMode();
  }
}

// ========== BUTTON DEBOUNCING ==========
void updateButtonState(ButtonState* btn, int pin) {
  bool reading = digitalRead(pin);
  
  // If the switch changed, due to noise or pressing
  if (reading != btn->lastState) {
    btn->lastDebounceTime = millis();
  }
  
  // Check if enough time has passed since last change
  if ((millis() - btn->lastDebounceTime) > DEBOUNCE_DELAY) {
    // If the button state has changed
    if (reading != btn->currentState) {
      btn->currentState = reading;
      
      // Detect button press (transition from HIGH to LOW for active-low)
      if (btn->currentState == LOW) {
        btn->pressed = true;
      }
    }
  }
  
  btn->lastState = reading;
}

// ========== MANUAL MODE HANDLER ==========
void handleManualMode() {
  // Input A controls GPO1 directly
  if (digitalRead(INPUT_A_PIN) == LOW) {
    digitalWrite(GPO1_PIN, HIGH);
  } else {
    digitalWrite(GPO1_PIN, LOW);
  }
  
  // Input B controls GPO2 directly
  if (digitalRead(INPUT_B_PIN) == LOW) {
    digitalWrite(GPO2_PIN, HIGH);
  } else {
    digitalWrite(GPO2_PIN, LOW);
  }
}

// ========== AUTO LOOP MODE HANDLER ==========
void handleAutoLoopMode() {
  bool endStopIn = (digitalRead(ENDSTOP_IN_PIN) == LOW);
  bool endStopOut = (digitalRead(ENDSTOP_OUT_PIN) == LOW);
  
  // Safety check: Both end stops triggered simultaneously (sensor malfunction)
  if (endStopIn && endStopOut) {
    Serial.println("ERROR: Both end stops triggered! Stopping all outputs.");
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
    cycleDirection = CYCLE_STOPPED;
    currentMode = MODE_MANUAL;  // Return to manual mode for safety
    return;
  }
  
  // Check for end stop triggers and reverse direction
  if (cycleDirection == CYCLE_IN && endStopIn) {
    Serial.println("End stop IN reached - switching to OUT cycle");
    cycleDirection = CYCLE_OUT;
    lastCycleTime = millis();
  } else if (cycleDirection == CYCLE_OUT && endStopOut) {
    Serial.println("End stop OUT reached - switching to IN cycle");
    cycleDirection = CYCLE_IN;
    lastCycleTime = millis();
  }
  
  // Apply cycle delay after direction change to prevent immediate reversal
  // This also ensures both outputs are never active simultaneously
  if (millis() - lastCycleTime < CYCLE_DELAY) {
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
    return;
  }
  
  // Control outputs based on cycle direction
  // Safety: Explicitly ensure only one output is active at a time
  if (cycleDirection == CYCLE_IN) {
    digitalWrite(GPO2_PIN, LOW);  // Turn off GPO2 first
    digitalWrite(GPO1_PIN, HIGH);  // Then turn on GPO1
  } else if (cycleDirection == CYCLE_OUT) {
    digitalWrite(GPO1_PIN, LOW);  // Turn off GPO1 first
    digitalWrite(GPO2_PIN, HIGH);  // Then turn on GPO2
  } else {
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
  }
}
