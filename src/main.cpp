/*
 * ESP32 Grout Pump Control System
 * 
 * Controls a hydraulic valve using 2 SSR outputs based on wireless remote inputs.
 * Supports manual control and automatic cycling mode with end-stop detection.
 * Features: Web interface (WebSocket+AJAX), OTA updates, WiFi credential storage, safety timeouts
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>

// ========== PIN DEFINITIONS ==========
// GPO Outputs - Control SSRs for hydraulic valve
const int GPO1_PIN = 25;  // SSR 1 output
const int GPO2_PIN = 26;  // SSR 2 output

// GPI Inputs - Wireless Remote Control (momentary switches)
const int INPUT_A_PIN = 12;  // Manual control for GPO1 (extend)
const int INPUT_B_PIN = 13;  // Manual control for GPO2 (retract)
const int INPUT_C_PIN = 14;  // Start automatic loop mode
const int INPUT_D_PIN = 15;  // Stop automatic loop mode

// GPI Inputs - End Stop Sensors
const int ENDSTOP_IN_PIN = 32;   // End stop for "in" position
const int ENDSTOP_OUT_PIN = 33;  // End stop for "out" position

// Safety Inputs
const int ESTOP_PIN = 27;        // Emergency Stop (Normally Closed Switch) -> OPEN = STOP

// ========== CONSTANTS ==========
const unsigned long DEBOUNCE_DELAY = 50;  // Debounce time in milliseconds
const unsigned long CYCLE_DELAY = 500;    // Delay between cycle direction changes
const unsigned long DEFAULT_CYCLE_TIMEOUT = 30000;  // Default 30 seconds timeout
const unsigned long STATUS_UPDATE_INTERVAL = 1000; // WebSocket broadcast interval (ms)

// ========== GLOBAL OBJECTS ==========
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
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
  unsigned long lastPressTime; // For UI visualization
};

ButtonState inputA = {HIGH, HIGH, 0, false, 0};
ButtonState inputB = {HIGH, HIGH, 0, false, 0};
ButtonState inputC = {HIGH, HIGH, 0, false, 0};
ButtonState inputD = {HIGH, HIGH, 0, false, 0};

unsigned long lastCycleTime = 0;
unsigned long cycleStartTime = 0;  // Track when cycle movement started for timeout
unsigned long lastStatusUpdate = 0; // Track last WebSocket broadcast

// Endstop state tracking for debug output
bool lastEndStopIn = HIGH;
bool lastEndStopOut = HIGH;

// Emergency Stop State
bool isEstopActive = false;

// Cycle Statistics
unsigned long cycleDurations[20];
int cycleIndex = 0;
int cycleCount = 0;
unsigned long lastDuration = 0;
unsigned long avgDuration = 0;

void updateStats(unsigned long duration) {
  // Filter out invalid durations (e.g. initial boot noise)
  if (duration < 100) return;

  cycleDurations[cycleIndex] = duration;
  cycleIndex = (cycleIndex + 1) % 20;
  if (cycleCount < 20) cycleCount++;
  
  unsigned long sum = 0;
  for (int i=0; i<cycleCount; i++) sum += cycleDurations[i];
  avgDuration = sum / cycleCount;
  lastDuration = duration;
}


// ========== FORWARD DECLARATIONS ==========
void updateButtonState(ButtonState* btn, int pin);
void handleManualMode();
void handleAutoLoopMode();
void handleSaveSettings(AsyncWebServerRequest *request);
void handleSetWiFi(AsyncWebServerRequest *request);
String getStatusJson();
void notifyClients();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
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
  pinMode(ESTOP_PIN, INPUT_PULLUP);
  
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
  Serial.println("  E-STOP (NC): GPIO " + String(ESTOP_PIN));
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
  bool stateChanged = false;

  // Handle OTA updates
  ArduinoOTA.handle();
  
  // WebSocket cleanup
  ws.cleanupClients();
  
  // Check Emergency Stop (Normal Open logic for NC switch: HIGH = Open/Triggered)
  if (digitalRead(ESTOP_PIN) == HIGH) {
    if (!isEstopActive) {
      Serial.println("!!! EMERGENCY STOP ACTIVATED !!!");
      isEstopActive = true;
      stateChanged = true;
    }
    
    // Safety: Force everything off immediately
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
    currentMode = MODE_MANUAL;
    cycleDirection = CYCLE_STOPPED;
    
    // Immediately notify on ESTOP
    if (stateChanged) notifyClients();

    // Skip remaining logic
    return;
  } else {
    // ESTOP Released
    if (isEstopActive) {
      Serial.println("Emergency Stop Released - Returning to MANUAL mode");
      isEstopActive = false;
      stateChanged = true;
    }
  }

  // Read and debounce all inputs
  updateButtonState(&inputA, INPUT_A_PIN);
  updateButtonState(&inputB, INPUT_B_PIN);
  updateButtonState(&inputC, INPUT_C_PIN);
  updateButtonState(&inputD, INPUT_D_PIN);

  // Debug Output for Endstops
  bool currentEndStopIn = digitalRead(ENDSTOP_IN_PIN);
  bool currentEndStopOut = digitalRead(ENDSTOP_OUT_PIN);

  if (currentEndStopIn != lastEndStopIn) {
    if (currentEndStopIn == HIGH) Serial.println("DEBUG: End Stop IN Triggered!");
    else Serial.println("DEBUG: End Stop IN Released.");
    lastEndStopIn = currentEndStopIn;
    stateChanged = true;
  }

  if (currentEndStopOut != lastEndStopOut) {
    if (currentEndStopOut == HIGH) Serial.println("DEBUG: End Stop OUT Triggered!");
    else Serial.println("DEBUG: End Stop OUT Released.");
    lastEndStopOut = currentEndStopOut;
    stateChanged = true;
  }
  
  // Check for mode change requests
  // Start AUTO loop
  if (inputC.pressed) {
    if (currentMode != MODE_AUTO_LOOP) {
      currentMode = MODE_AUTO_LOOP;
      // Resume from last direction, or default to OUT if unknown
      if (cycleDirection == CYCLE_STOPPED) {
        cycleDirection = CYCLE_OUT;
      }
      lastCycleTime = millis();
      cycleStartTime = millis();  // Start timeout timer
      Serial.println("Switched to AUTO LOOP mode");
      stateChanged = true;
    }
    inputC.pressed = false;
  }
  
  // Stop AUTO loop
  if (inputD.pressed || ((currentMode == MODE_AUTO_LOOP) && (inputA.pressed || inputB.pressed))) {
    if (currentMode == MODE_AUTO_LOOP) {
      currentMode = MODE_MANUAL;
      // Do NOT reset cycleDirection -> Keep it for resuming later
      digitalWrite(GPO1_PIN, LOW);
      digitalWrite(GPO2_PIN, LOW);
      Serial.println("Switched to MANUAL mode");
      stateChanged = true;
    }
    inputD.pressed = false;
  }
  
  // Capture Pre-Execution State
  int prevGPO1 = digitalRead(GPO1_PIN);
  int prevGPO2 = digitalRead(GPO2_PIN);
  CycleDirection prevCycleDir = cycleDirection;

  // Execute based on current mode
  if (currentMode == MODE_MANUAL) {
    handleManualMode();
  } else {
    handleAutoLoopMode();
  }

  // Check Post-Execution State for changes
  if (digitalRead(GPO1_PIN) != prevGPO1) stateChanged = true;
  if (digitalRead(GPO2_PIN) != prevGPO2) stateChanged = true;
  if (cycleDirection != prevCycleDir) stateChanged = true;

  // Broadcast status via WebSocket if Changed OR Timer Expired
  if (stateChanged || (millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL)) {
    notifyClients();
    lastStatusUpdate = millis();
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
        btn->lastPressTime = millis(); // Record timestamp for UI
        notifyClients();
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

  // Read endstops to update direction logic even in manual mode
  bool endStopIn = (digitalRead(ENDSTOP_IN_PIN) == HIGH);
  bool endStopOut = (digitalRead(ENDSTOP_OUT_PIN) == HIGH);

  // Update direction based on End Stops (Top Priority)
  // If we hit an end stop in manual mode, the NEXT auto-move must be the opposite way.
  if (endStopIn) {
    cycleDirection = CYCLE_OUT;
  } else if (endStopOut) {
    cycleDirection = CYCLE_IN;
  }

  // Safety: Prevent simultaneous activation of both outputs
  if (inputAPressed && inputBPressed) {
    // If both buttons pressed, turn off both outputs
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
    return;
  }
  
  // Input A controls GPO2 (Extend / OUT)
  if (inputAPressed) {
    digitalWrite(GPO1_PIN, LOW);  // Ensure other output is off

    // Only extend if end stop OUT is NOT triggered
    if (!endStopOut) {
       digitalWrite(GPO2_PIN, HIGH);
       cycleDirection = CYCLE_OUT;
    } else {
       // Safety block
       digitalWrite(GPO2_PIN, LOW);
    }
  } 
  // Input B controls GPO1 (Retract / IN)
  else if (inputBPressed) {
    digitalWrite(GPO2_PIN, LOW);  // Ensure other output is off

    // Only retract if end stop IN is NOT triggered
    if (!endStopIn) {
       digitalWrite(GPO1_PIN, HIGH);
       cycleDirection = CYCLE_IN;
    } else {
       // Safety block
       digitalWrite(GPO1_PIN, LOW);
    }
  } 
  // No input pressed
  else {
    digitalWrite(GPO1_PIN, LOW);
    digitalWrite(GPO2_PIN, LOW);
  }
}

// ========== AUTO LOOP MODE HANDLER ==========
void handleAutoLoopMode() {
  // Endstops are Normally Closed (NC): HIGH = Triggered (Open Switch), LOW = Safe (Closed Switch)
  bool endStopIn = (digitalRead(ENDSTOP_IN_PIN) == HIGH);
  bool endStopOut = (digitalRead(ENDSTOP_OUT_PIN) == HIGH);
  
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
    
    // Calculate cycle time (subtracting the delay at the start of movement)
    // Note: cycleStartTime was reset when previous end stop was hit.
    unsigned long rawDuration = millis() - cycleStartTime;
    // The previous cycle included a CYCLE_DELAY wait before moving.
    // If we want pure "stroke time", subtract CYCLE_DELAY (if duration > delay).
    if (rawDuration > CYCLE_DELAY) {
        updateStats(rawDuration - CYCLE_DELAY);
    }

    cycleDirection = CYCLE_OUT;
    lastCycleTime = millis();
    cycleStartTime = millis();  // Reset timeout timer for new cycle
  } else if (cycleDirection == CYCLE_OUT && endStopOut) {
    Serial.println("End stop OUT reached - switching to IN cycle");

    unsigned long rawDuration = millis() - cycleStartTime;
    if (rawDuration > CYCLE_DELAY) {
        updateStats(rawDuration - CYCLE_DELAY);
    }

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
  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Static files
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  
  // API endpoints
  server.on("/save", HTTP_POST, handleSaveSettings);
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getStatusJson());
  });
  server.on("/setwifi", HTTP_POST, handleSetWiFi);
  
  // Web OTA Update
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    bool shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
    if(shouldReboot) ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      int cmd = (filename == "filesystem") ? U_SPIFFS : U_FLASH;
      if(!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) Update.printError(Serial);
    }
    if(Update.write(data, len) != len) Update.printError(Serial);
    if(final){
      if(Update.end(true)) Serial.printf("Update Success: %uB\n", index+len);
      else Update.printError(Serial);
    }
  });
  
  // 404
  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not Found");
  });

  server.begin();
  Serial.println("Async Web server started");
}

void notifyClients() {
  ws.textAll(getStatusJson());
}

String getStatusJson() {
  DynamicJsonDocument doc(1024);
  
  doc["estopActive"] = isEstopActive;
  doc["mode"] = (currentMode == MODE_MANUAL ? "MANUAL" : "AUTO");
  
  if (cycleDirection == CYCLE_IN) doc["cycleDirection"] = "IN";
  else if (cycleDirection == CYCLE_OUT) doc["cycleDirection"] = "OUT";
  else doc["cycleDirection"] = "STOPPED";
  
  doc["gpo1"] = digitalRead(GPO1_PIN);
  doc["gpo2"] = digitalRead(GPO2_PIN);
  
  unsigned long now = millis();
  // Lower the threshold because we update much faster now
  doc["inputA"] = (now - inputA.lastPressTime < 1000) || (digitalRead(INPUT_A_PIN) == LOW);
  doc["inputB"] = (now - inputB.lastPressTime < 1000) || (digitalRead(INPUT_B_PIN) == LOW);
  doc["inputC"] = (now - inputC.lastPressTime < 1000) || (digitalRead(INPUT_C_PIN) == LOW);
  doc["inputD"] = (now - inputD.lastPressTime < 1000) || (digitalRead(INPUT_D_PIN) == LOW);
  
  doc["endStopIn"] = (digitalRead(ENDSTOP_IN_PIN) == HIGH);
  doc["endStopOut"] = (digitalRead(ENDSTOP_OUT_PIN) == HIGH);

  // Cycle Statistics
  doc["lastDuration"] = lastDuration;
  doc["avgDuration"] = avgDuration;
  
  JsonArray history = doc.createNestedArray("history");
  // Output history ordered (Oldest -> Newest) is ideal for graphing
  if (cycleCount > 0) {
      int idx = (cycleCount < 20) ? 0 : cycleIndex; // Start at oldest
      for (int i = 0; i < cycleCount; i++) {
         history.add(cycleDurations[(idx + i) % 20]);
      }
  }
  
  doc["cycleTimeout"] = cycleTimeout;
  doc["timeoutEnabled"] = timeoutEnabled;
  doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifiSSID"] = (WiFi.status() == WL_CONNECTED ? wifiSSID : "AP Mode");
  doc["ipAddress"] = (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  
  String json;
  serializeJson(doc, json);
  return json;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if(type == WS_EVT_CONNECT){
    client->text(getStatusJson());
  }
}

// ========== ASYNC HANDLERS ==========
void handleSaveSettings(AsyncWebServerRequest *request) {
  if (request->hasArg("timeout")) {
    unsigned long newTimeout = request->arg("timeout").toInt();
    if (newTimeout >= 1000 && newTimeout <= 300000) {
      cycleTimeout = newTimeout;
    } else {
      request->send(400, "text/html", "Invalid Timeout");
      return;
    }
  }
  
  timeoutEnabled = request->hasArg("timeoutEnabled");
  saveSettings();
  request->send(200, "text/html", "<h1>Settings Saved!</h1><meta http-equiv='refresh' content='2;url=/'>");
}

void handleSetWiFi(AsyncWebServerRequest *request) {
  if (request->hasArg("ssid")) wifiSSID = request->arg("ssid");
  if (request->hasArg("password")) wifiPassword = request->arg("password");
  saveSettings();
  request->send(200, "text/html", "<h1>WiFi Saved! Device restarting...</h1>");
  delay(1000);
  ESP.restart();
}
