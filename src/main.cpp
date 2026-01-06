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
void handleRoot();
void handleSettings();
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
  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
  server.on("/save", HTTP_POST, handleSaveSettings);
  server.on("/status", handleStatus);
  server.on("/setwifi", HTTP_POST, handleSetWiFi);
  
  server.begin();
  Serial.println("Web server started");
}

// ========== WEB SERVER HANDLERS ==========
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 600px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "h1 { color: #333; }";
  html += ".status { padding: 15px; margin: 10px 0; border-radius: 5px; }";
  html += ".status.manual { background: #e3f2fd; border-left: 4px solid #2196F3; }";
  html += ".status.auto { background: #fff3e0; border-left: 4px solid #ff9800; }";
  html += ".btn { display: inline-block; padding: 10px 20px; margin: 5px; background: #2196F3; color: white; text-decoration: none; border-radius: 5px; }";
  html += ".btn:hover { background: #1976D2; }";
  html += ".info { background: #e8f5e9; padding: 10px; margin: 10px 0; border-radius: 5px; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>🚰 Grout Pump Control</h1>";
  
  // Status display
  html += "<div class='status " + String(currentMode == MODE_MANUAL ? "manual" : "auto") + "'>";
  html += "<h2>Current Status</h2>";
  html += "<p><strong>Mode:</strong> " + String(currentMode == MODE_MANUAL ? "MANUAL" : "AUTO LOOP") + "</p>";
  if (currentMode == MODE_AUTO_LOOP) {
    html += "<p><strong>Cycle Direction:</strong> ";
    if (cycleDirection == CYCLE_IN) html += "IN (Retracting)";
    else if (cycleDirection == CYCLE_OUT) html += "OUT (Extending)";
    else html += "STOPPED";
    html += "</p>";
  }
  html += "<p><strong>GPO1 (SSR1):</strong> " + String(digitalRead(GPO1_PIN) ? "ON" : "OFF") + "</p>";
  html += "<p><strong>GPO2 (SSR2):</strong> " + String(digitalRead(GPO2_PIN) ? "ON" : "OFF") + "</p>";
  html += "</div>";
  
  // Network info
  html += "<div class='info'>";
  html += "<h3>Network Information</h3>";
  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    html += "<p><strong>WiFi:</strong> Connected to " + wifiSSID + "</p>";
    html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
    html += "<p><strong>Access via:</strong> http://groutpump.local</p>";
  } else {
    html += "<p><strong>WiFi:</strong> AP Mode (Setup)</p>";
    html += "<p><strong>AP IP:</strong> " + WiFi.softAPIP().toString() + "</p>";
  }
  html += "</div>";
  
  // Navigation
  html += "<div style='margin-top: 20px;'>";
  html += "<a href='/settings' class='btn'>⚙️ Settings</a>";
  html += "<a href='/status' class='btn'>📊 Status JSON</a>";
  html += "<a href='/' class='btn'>🔄 Refresh</a>";
  html += "</div>";
  
  html += "<p style='margin-top: 20px; color: #666; font-size: 12px;'>Grout Pump Control v1.0</p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSettings() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 600px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "h1, h2 { color: #333; }";
  html += "form { margin: 20px 0; }";
  html += "label { display: block; margin: 10px 0 5px; font-weight: bold; }";
  html += "input[type='text'], input[type='password'], input[type='number'] { width: 100%; padding: 8px; margin: 5px 0; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; }";
  html += "input[type='checkbox'] { margin-right: 10px; }";
  html += "input[type='submit'] { background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin-top: 10px; }";
  html += "input[type='submit']:hover { background: #45a049; }";
  html += ".btn { display: inline-block; padding: 10px 20px; margin: 5px; background: #2196F3; color: white; text-decoration: none; border-radius: 5px; }";
  html += ".section { background: #f9f9f9; padding: 15px; margin: 15px 0; border-radius: 5px; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>⚙️ Settings</h1>";
  
  // WiFi Settings
  html += "<div class='section'>";
  html += "<h2>WiFi Configuration</h2>";
  html += "<form action='/setwifi' method='POST'>";
  html += "<label>SSID:</label>";
  html += "<input type='text' name='ssid' value='" + wifiSSID + "' required>";
  html += "<label>Password:</label>";
  html += "<input type='password' name='password' value='" + wifiPassword + "'>";
  html += "<input type='submit' value='💾 Save WiFi Settings'>";
  html += "<p style='color: #666; font-size: 12px;'>Note: Device will restart after saving WiFi settings</p>";
  html += "</form>";
  html += "</div>";
  
  // Timing Settings
  html += "<div class='section'>";
  html += "<h2>Timing Configuration</h2>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Cycle Timeout (milliseconds):</label>";
  html += "<input type='number' name='timeout' value='" + String(cycleTimeout) + "' min='1000' max='300000' required>";
  html += "<p style='color: #666; font-size: 12px;'>Time allowed for hydraulic valve to reach end-stop (1000ms = 1 second)</p>";
  html += "<label><input type='checkbox' name='timeoutEnabled' " + String(timeoutEnabled ? "checked" : "") + ">Enable Timeout Protection</label>";
  html += "<input type='submit' value='💾 Save Timing Settings'>";
  html += "</form>";
  html += "</div>";
  
  // OTA Info
  html += "<div class='section'>";
  html += "<h2>OTA Updates</h2>";
  html += "<p><strong>Status:</strong> Enabled</p>";
  html += "<p><strong>Hostname:</strong> groutpump</p>";
  html += "<p><strong>Password:</strong> groutpump123</p>";
  html += "<p style='color: #666; font-size: 12px;'>Use Arduino IDE or PlatformIO to upload firmware over WiFi</p>";
  html += "</div>";
  
  html += "<div style='margin-top: 20px;'>";
  html += "<a href='/' class='btn'>🏠 Home</a>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

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
  json += "\"ipAddress\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}
