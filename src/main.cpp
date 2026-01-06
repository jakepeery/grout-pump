/*
 * ESP32 Grout Pump Control System
 * 
 * Controls a hydraulic valve using 2 SSR outputs based on wireless remote inputs.
 * Supports manual control and automatic cycling mode with end-stop detection.
 * Features: Web interface, OTA updates, WiFi credential storage, safety timeouts
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <LittleFS.h>

// ========== PIN DEFINITIONS ==========
// GPO Outputs - Control SSRs for hydraulic valve
const int GPO1_PIN = 25;  // SSR 1 output
const int GPO2_PIN = 26;  // SSR 2 output

// GPI Inputs - Wireless Remote Control (momentary switches)
// All pins now support internal pull-ups - no external resistors needed!
const int INPUT_A_PIN = 32;  // Manual control for GPO1 (extend)
const int INPUT_B_PIN = 33;  // Manual control for GPO2 (retract)
const int INPUT_C_PIN = 12;  // Start automatic loop mode
const int INPUT_D_PIN = 13;  // Stop automatic loop mode

// GPI Inputs - End Stop Sensors
const int ENDSTOP_IN_PIN = 14;   // End stop for "in" position
const int ENDSTOP_OUT_PIN = 15;  // End stop for "out" position

// ========== CONSTANTS ==========
const unsigned long DEBOUNCE_DELAY = 50;  // Debounce time in milliseconds
const unsigned long CYCLE_DELAY = 500;    // Delay between cycle direction changes
const unsigned long DEFAULT_CYCLE_TIMEOUT = 30000;  // Default 30 seconds timeout

// ========== GLOBAL OBJECTS ==========
WebServer server(80);
Preferences preferences;

// ========== CONFIGURATION VARIABLES ==========
String wifiSSID = "";
String wifiPassword = "";
unsigned long cycleTimeout = DEFAULT_CYCLE_TIMEOUT;  // Configurable via web interface
bool timeoutEnabled = true;

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
unsigned long cycleStartTime = 0;  // Track when cycle movement started for timeout

// ========== FORWARD DECLARATIONS ==========
void handleSaveSettings();
void handleStatus();
void handleSetWiFi();
void loadSettings();
void saveSettings();
void setupWiFi();
void setupOTA();
void setupWebServer();

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
  
  // Configure GPI pins as inputs with internal pull-up resistors
  // All pins now support internal pull-ups - no external resistors needed!
  pinMode(INPUT_A_PIN, INPUT_PULLUP);
  pinMode(INPUT_B_PIN, INPUT_PULLUP);
  pinMode(INPUT_C_PIN, INPUT_PULLUP);
  pinMode(INPUT_D_PIN, INPUT_PULLUP);
  pinMode(ENDSTOP_IN_PIN, INPUT_PULLUP);
  pinMode(ENDSTOP_OUT_PIN, INPUT_PULLUP);
  
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
  Serial.println("  All inputs use internal pull-ups - no external resistors needed!");
  
  // Initialize LittleFS for web files
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed! Web interface may not work.");
    Serial.println("Upload filesystem files using: pio run --target uploadfs");
  } else {
    Serial.println("LittleFS mounted successfully");
  }
  
  // Load settings from flash
  loadSettings();
  
  // Setup WiFi connection
  setupWiFi();
  
  // Setup OTA updates
  setupOTA();
  
  // Setup web server
  setupWebServer();
  
  Serial.println("Setup complete!");
}

// ========== MAIN LOOP ==========
void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();
  
  // Handle web server requests
  server.handleClient();
  
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
      cycleStartTime = millis();  // Start timeout timer
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
      
      // Detect button press event (transition from HIGH to LOW for active-low)
      // This is edge-triggered - the flag is set on press and must be cleared by handler
      if (btn->currentState == LOW) {
        btn->pressed = true;
      } else {
        // Clear pressed flag when button is released
        btn->pressed = false;
      }
    }
  }
  
  btn->lastState = reading;
}

// ========== MANUAL MODE HANDLER ==========
void handleManualMode() {
  bool inputAPressed = (digitalRead(INPUT_A_PIN) == LOW);
  bool inputBPressed = (digitalRead(INPUT_B_PIN) == LOW);
  
  // Safety: Prevent simultaneous activation of both outputs
  if (inputAPressed && inputBPressed) {
    // If both buttons pressed, turn off both outputs
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
    return;
  }
  
  // Input A controls GPO1 (extend)
  if (inputAPressed) {
    digitalWrite(GPO1_PIN, HIGH);
    digitalWrite(GPO2_PIN, LOW);  // Ensure other output is off
  } 
  // Input B controls GPO2 (retract)
  else if (inputBPressed) {
    digitalWrite(GPO2_PIN, HIGH);
    digitalWrite(GPO1_PIN, LOW);  // Ensure other output is off
  } 
  // No input pressed
  else {
    digitalWrite(GPO1_PIN, LOW);
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
    inputC.pressed = false;
    inputD.pressed = false;
    return;
  }
  
  // Safety check: Timeout if end-stop not reached within expected time
  if (timeoutEnabled && (millis() - cycleStartTime > cycleTimeout)) {
    Serial.println("ERROR: Cycle timeout! End-stop not reached within " + String(cycleTimeout) + "ms");
    Serial.println("Stopping all outputs and returning to manual mode.");
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
    cycleDirection = CYCLE_STOPPED;
    currentMode = MODE_MANUAL;  // Return to manual mode for safety
    inputC.pressed = false;
    inputD.pressed = false;
    return;
  }
  
  // Check for end stop triggers and reverse direction
  if (cycleDirection == CYCLE_IN && endStopIn) {
    Serial.println("End stop IN reached - switching to OUT cycle");
    cycleDirection = CYCLE_OUT;
    lastCycleTime = millis();
    cycleStartTime = millis();  // Reset timeout timer for new cycle
  } else if (cycleDirection == CYCLE_OUT && endStopOut) {
    Serial.println("End stop OUT reached - switching to IN cycle");
    cycleDirection = CYCLE_IN;
    lastCycleTime = millis();
    cycleStartTime = millis();  // Reset timeout timer for new cycle
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

// ========== SETTINGS MANAGEMENT ==========
void loadSettings() {
  preferences.begin("groutpump", false);
  
  // Load WiFi credentials
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  
  // Load timing settings
  cycleTimeout = preferences.getULong("cycleTimeout", DEFAULT_CYCLE_TIMEOUT);
  timeoutEnabled = preferences.getBool("timeoutEnabled", true);
  
  preferences.end();
  
  Serial.println("Settings loaded from flash");
  Serial.println("  SSID: " + (wifiSSID.length() > 0 ? wifiSSID : "Not configured"));
  Serial.println("  Cycle Timeout: " + String(cycleTimeout) + " ms");
  Serial.println("  Timeout Enabled: " + String(timeoutEnabled ? "Yes" : "No"));
}

void saveSettings() {
  preferences.begin("groutpump", false);
  
  preferences.putString("ssid", wifiSSID);
  preferences.putString("password", wifiPassword);
  preferences.putULong("cycleTimeout", cycleTimeout);
  preferences.putBool("timeoutEnabled", timeoutEnabled);
  
  preferences.end();
  
  Serial.println("Settings saved to flash");
}

// ========== WIFI SETUP ==========
void setupWiFi() {
  if (wifiSSID.length() == 0) {
    Serial.println("WiFi not configured. Starting in AP mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("GroutPump-Setup", "12345678");
    Serial.println("AP Mode started");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Connect to 'GroutPump-Setup' (password: 12345678)");
    Serial.println("Then navigate to http://192.168.4.1 to configure WiFi");
    return;
  }
  
  Serial.println("Connecting to WiFi: " + wifiSSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Setup mDNS
    if (MDNS.begin("groutpump")) {
      Serial.println("mDNS responder started: http://groutpump.local");
    }
  } else {
    Serial.println("\nWiFi connection failed. Starting in AP mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("GroutPump-Setup", "12345678");
    Serial.println("AP Mode started");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
  }
}

// ========== OTA SETUP ==========
void setupOTA() {
  ArduinoOTA.setHostname("groutpump");
  ArduinoOTA.setPassword("groutpump123");  // Change this for security
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
    
    // Stop all outputs during OTA update
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update complete!");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA update service started");
  Serial.println("OTA Password: groutpump123");
}

// ========== WEB SERVER SETUP ==========
void setupWebServer() {
  // Serve static files from LittleFS
  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/index.html", LittleFS, "/index.html");
  server.serveStatic("/settings.html", LittleFS, "/settings.html");
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/script.js", LittleFS, "/script.js");
  
  // API endpoints
  server.on("/save", HTTP_POST, handleSaveSettings);
  server.on("/status", handleStatus);
  server.on("/setwifi", HTTP_POST, handleSetWiFi);
  
  // 404 handler
  server.onNotFound([]() {
    server.send(404, "text/plain", "404: File not found");
  });
  
  server.begin();
  Serial.println("Web server started");
  Serial.println("Web files served from LittleFS filesystem");
}

// ========== WEB SERVER HANDLERS ==========
void handleSaveSettings() {
  if (server.hasArg("timeout")) {
    unsigned long newTimeout = server.arg("timeout").toInt();
    // Validate timeout value (1 second to 5 minutes)
    if (newTimeout >= 1000 && newTimeout <= 300000) {
      cycleTimeout = newTimeout;
    } else {
      // Invalid value, send error response
      String html = "<!DOCTYPE html><html><head>";
      html += "<meta http-equiv='refresh' content='3;url=/settings'>";
      html += "<style>body { font-family: Arial; text-align: center; padding: 50px; }</style>";
      html += "</head><body>";
      html += "<h1>❌ Invalid Timeout Value</h1>";
      html += "<p>Timeout must be between 1000ms and 300000ms</p>";
      html += "<p>Redirecting back to settings...</p>";
      html += "</body></html>";
      server.send(400, "text/html", html);
      return;
    }
  }
  
  timeoutEnabled = server.hasArg("timeoutEnabled");
  
  saveSettings();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial; text-align: center; padding: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>✅ Settings Saved!</h1>";
  html += "<p>Redirecting to home page...</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleSetWiFi() {
  if (server.hasArg("ssid")) {
    wifiSSID = server.arg("ssid");
  }
  if (server.hasArg("password")) {
    wifiPassword = server.arg("password");
  }
  
  saveSettings();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<style>body { font-family: Arial; text-align: center; padding: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>✅ WiFi Settings Saved!</h1>";
  html += "<p>Device will restart momentarily...</p>";
  html += "<p>Please reconnect to your WiFi network and access the device at:</p>";
  html += "<p><strong>http://groutpump.local</strong></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
  
  // Small delay to ensure response is sent before restart
  delay(1000);
  ESP.restart();
}

void handleStatus() {
  String json = "{";
  json += "\"mode\":\"" + String(currentMode == MODE_MANUAL ? "MANUAL" : "AUTO") + "\",";
  json += "\"cycleDirection\":\"";
  if (cycleDirection == CYCLE_IN) json += "IN";
  else if (cycleDirection == CYCLE_OUT) json += "OUT";
  else json += "STOPPED";
  json += "\",";
  json += "\"gpo1\":" + String(digitalRead(GPO1_PIN)) + ",";
  json += "\"gpo2\":" + String(digitalRead(GPO2_PIN)) + ",";
  json += "\"endStopIn\":" + String(digitalRead(ENDSTOP_IN_PIN) == LOW ? "true" : "false") + ",";
  json += "\"endStopOut\":" + String(digitalRead(ENDSTOP_OUT_PIN) == LOW ? "true" : "false") + ",";
  json += "\"cycleTimeout\":" + String(cycleTimeout) + ",";
  json += "\"timeoutEnabled\":" + String(timeoutEnabled ? "true" : "false") + ",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"wifiSSID\":\"" + (WiFi.status() == WL_CONNECTED ? wifiSSID : "AP Mode") + "\",";
  json += "\"ipAddress\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}
