// ESP32_Traffic_Controller_IntersectionA.ino
// ESP32-1: Intersection A - Full Features F01-F06 (No Sensors)
// Hardware: ESP32 + 4x Traffic Lights (RGB) + 4x TM1637 Displays

#include <TM1637Display.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ===================== WiFi Configuration =====================
const char* ssid = "Airtel_sahi_0849";     // Change to your WiFi SSID
const char* password = "air99772";         // Change to your WiFi password

// ===================== LED Pins =====================
// [road][0=Red, 1=Yellow, 2=Green]
const uint8_t ledPins[4][3] = {
  {2, 4, 5},    // Road 1: Red, Yellow, Green
  {14, 13, 12}, // Road 2
  {15, 16, 17}, // Road 3
  {18, 19, 21}  // Road 4
};

// ===================== TM1637 Display Pins =====================
// [display][0=CLK, 1=DIO]
const uint8_t displayPins[4][2] = {
  {27, 32}, // Display 1: CLK, DIO
  {25, 26}, // Display 2
  {22, 23}, // Display 3
  {33, 1}   // Display 4 (GPIO1 is UART TX - use with caution)
};

TM1637Display displays[4] = {
  TM1637Display(displayPins[0][0], displayPins[0][1]),
  TM1637Display(displayPins[1][0], displayPins[1][1]),
  TM1637Display(displayPins[2][0], displayPins[2][1]),
  TM1637Display(displayPins[3][0], displayPins[3][1])
};

// ===================== Timing Constants =====================
uint32_t RED_TIME = 20;      // Default red time in seconds
uint32_t GREEN_TIME = 20;    // Default green time in seconds
uint32_t YELLOW_TIME = 3;    // Default yellow time in seconds

// ===================== Per-Road Timing =====================
uint32_t roadRedTime[4] = {20, 20, 20, 20};
uint32_t roadGreenTime[4] = {20, 20, 20, 20};
uint32_t roadYellowTime[4] = {3, 3, 3, 3};

uint32_t CYCLE_DURATION = 0;

// ===================== Road Order Mapping =====================
const uint8_t roadOrder[4] = {0, 1, 2, 3};
const uint8_t displayForRoad[4] = {0, 1, 2, 3};
uint32_t greenStart[4];
uint32_t yellowStart[4];
uint32_t redStart[4];

// ===================== State Variables =====================
uint32_t cycleTime = 0;
uint32_t phaseStartMillis = 0;
uint8_t currentPhase = 0;
uint32_t phaseDurations[8];
uint8_t activeRoad[8];
bool isYellow[8];
int8_t lastDisplayValue[4] = {-1, -1, -1, -1};
bool systemActive = false;
bool dashboardConnected = false;
bool systemInitialized = false;
bool isPhaseSkipping = false;

// ===================== LED Burnout Tracking =====================
bool ledBurnout[4][3] = {
  {false, false, false},
  {false, false, false},
  {false, false, false},
  {false, false, false}
};

// ===================== Fault Management =====================
struct Fault {
  bool active;
  uint8_t roads[4];
  uint8_t roadCount;
  float value;
};

// All F01-F06 supported (F02 and F03 are manual only, no sensors)
Fault faults[6] = {
  {false, {0,0,0,0}, 0, 0},  // F01: LED Burnout - SUPPORTED
  {false, {0,0,0,0}, 0, 0},  // F02: Overheating - SUPPORTED (Manual)
  {false, {0,0,0,0}, 0, 0},  // F03: Water Leakage - SUPPORTED (Manual)
  {false, {0,0,0,0}, 0, 0},  // F04: Phase Skipping - SUPPORTED
  {false, {0,0,0,0}, 0, 0},  // F05: Over-Timing - SUPPORTED
  {false, {0,0,0,0}, 0, 0}   // F06: Under-Timing - SUPPORTED
};

const char* faultNames[6] = {
  "LED Burnout",
  "Overheating",
  "Water Leakage",
  "Phase Skipping",
  "Over-Timing",
  "Under-Timing"
};

// ===================== Web Server =====================
WebServer server(80);
StaticJsonDocument<8192> jsonDoc;

// ===================== CORS Helper =====================
void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ===================== Helper Functions =====================
void initializeSystem() {
  if (systemInitialized) return;
  
  Serial.println("\n========================================");
  Serial.println("ESP32-1 (Intersection A) Traffic Controller");
  Serial.println("Supported: F01-F06 (All Faults - Manual Mode)");
  Serial.println("========================================");
  
  Serial.println("Initializing LED pins...");
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 3; c++) {
      pinMode(ledPins[r][c], OUTPUT);
      digitalWrite(ledPins[r][c], LOW);
    }
  }
  Serial.println("✓ LED pins initialized");
  
  Serial.println("Initializing TM1637 displays...");
  for (uint8_t i = 0; i < 4; i++) {
    displays[i].setBrightness(0x0A);
    displays[i].clear();
    lastDisplayValue[i] = -1;
  }
  Serial.println("✓ Displays initialized");
  
  for (int i = 0; i < 4; i++) {
    roadRedTime[i] = RED_TIME;
    roadGreenTime[i] = GREEN_TIME;
    roadYellowTime[i] = YELLOW_TIME;
  }
  
  rebuildPhaseArrays();
  systemActive = false;
  
  for (uint8_t r = 0; r < 4; r++) {
    setRoadLEDs(r, LOW, LOW, LOW);
  }
  for (uint8_t i = 0; i < 4; i++) {
    displays[i].clear();
    lastDisplayValue[i] = -1;
  }
  
  systemInitialized = true;
  Serial.println("✓ System ready - waiting for dashboard connection");
}

void rebuildPhaseArrays() {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t road = roadOrder[i];
    phaseDurations[2*i] = roadGreenTime[road];
    phaseDurations[2*i+1] = roadYellowTime[road];
    activeRoad[2*i] = road;
    activeRoad[2*i+1] = road;
    isYellow[2*i] = false;
    isYellow[2*i+1] = true;
  }
  
  uint32_t cumulativeTime = 0;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t road = roadOrder[i];
    greenStart[road] = cumulativeTime;
    cumulativeTime += roadGreenTime[road];
    yellowStart[road] = cumulativeTime;
    cumulativeTime += roadYellowTime[road];
    redStart[road] = cumulativeTime;
  }
  CYCLE_DURATION = cumulativeTime;
}

bool isRoadAffectedByFault(uint8_t road, uint8_t faultIdx) {
  if (!faults[faultIdx].active) return false;
  if (faults[faultIdx].roadCount == 0) return true;
  for (int i = 0; i < faults[faultIdx].roadCount; i++) {
    if (faults[faultIdx].roads[i] == road) return true;
  }
  return false;
}

void setRoadLEDs(uint8_t road, uint8_t redState, uint8_t yellowState, uint8_t greenState) {
  // INDEPENDENT LED BURNOUT: Each LED checked separately
  if (ledBurnout[road][0]) redState = LOW;
  if (ledBurnout[road][1]) yellowState = LOW;
  if (ledBurnout[road][2]) greenState = LOW;
  
  digitalWrite(ledPins[road][0], redState);
  digitalWrite(ledPins[road][1], yellowState);
  digitalWrite(ledPins[road][2], greenState);
}

void updateLEDs(uint8_t phase) {
  if (!systemActive) {
    for (uint8_t r = 0; r < 4; r++) {
      setRoadLEDs(r, LOW, LOW, LOW);
    }
    return;
  }
  
  uint8_t road = activeRoad[phase];
  bool yellow = isYellow[phase];
  
  // Phase Skipping (F04)
  if (isRoadAffectedByFault(road, 3)) {
    if (!isPhaseSkipping) {
      isPhaseSkipping = true;
      currentPhase = (currentPhase + 1) % 8;
      phaseStartMillis = millis();
      cycleTime += phaseDurations[(currentPhase + 7) % 8];
      cycleTime %= CYCLE_DURATION;
      road = activeRoad[currentPhase];
      yellow = isYellow[currentPhase];
    }
  } else {
    isPhaseSkipping = false;
  }
  
  for (uint8_t r = 0; r < 4; r++) {
    setRoadLEDs(r, LOW, LOW, LOW);
  }
  
  // Overheating (F02) - reduce brightness
  if (isRoadAffectedByFault(road, 1)) {
    for (uint8_t r = 0; r < 4; r++) {
      if (isRoadAffectedByFault(r, 1)) {
        analogWrite(ledPins[r][0], 64);
        analogWrite(ledPins[r][1], 64);
        analogWrite(ledPins[r][2], 64);
      }
    }
  }
  
  if (yellow) {
    setRoadLEDs(road, LOW, HIGH, LOW);
  } else {
    setRoadLEDs(road, LOW, LOW, HIGH);
  }
  
  for (uint8_t r = 0; r < 4; r++) {
    if (r != road) {
      setRoadLEDs(r, HIGH, LOW, LOW);
    }
  }
}

uint32_t getEffectiveGreenTime(uint8_t road) {
  uint32_t greenTime = roadGreenTime[road];
  if (isRoadAffectedByFault(road, 4)) {
    greenTime = roadGreenTime[road] + 5;  // Over-Timing: +5s
  }
  if (isRoadAffectedByFault(road, 5)) {
    greenTime = max(5, (int)roadGreenTime[road] - 5);  // Under-Timing: -5s
  }
  return greenTime;
}

uint32_t getEffectiveYellowTime(uint8_t road) {
  uint32_t yellowTime = roadYellowTime[road];
  if (isRoadAffectedByFault(road, 4)) {
    yellowTime = roadYellowTime[road] + 2;  // Over-Timing: +2s
  }
  if (isRoadAffectedByFault(road, 5)) {
    yellowTime = max(1, (int)roadYellowTime[road] - 1);  // Under-Timing: -1s
  }
  return yellowTime;
}

void updateDisplay(uint8_t road, uint32_t currentTime) {
  if (!systemActive) {
    uint8_t dispIdx = displayForRoad[road];
    if (lastDisplayValue[dispIdx] != -2) {
      lastDisplayValue[dispIdx] = -2;
      displays[dispIdx].clear();
    }
    return;
  }
  
  uint32_t gStart = greenStart[road];
  uint32_t yStart = yellowStart[road];
  uint32_t effectiveGreen = getEffectiveGreenTime(road);
  uint32_t effectiveYellow = getEffectiveYellowTime(road);
  uint32_t gEnd = gStart + effectiveGreen;
  uint32_t yEnd = yStart + effectiveYellow;
  
  int32_t value = 0;
  if (currentTime >= gStart && currentTime < gEnd) {
    value = gEnd - currentTime;
  } else if (currentTime >= yStart && currentTime < yEnd) {
    value = yEnd - currentTime;
  } else {
    uint32_t nextGreen = (currentTime < gStart) ? gStart : (gStart + CYCLE_DURATION);
    value = nextGreen - currentTime;
  }
  
  if (value < 0) value = 0;
  if (value > 99) value = 99;
  
  uint8_t dispIdx = displayForRoad[road];
  if (lastDisplayValue[dispIdx] != value) {
    lastDisplayValue[dispIdx] = value;
    displays[dispIdx].showNumberDec(value, false, 2, 0);
  }
}

void advancePhase() {
  if (!systemActive) return;
  currentPhase = (currentPhase + 1) % 8;
  phaseStartMillis = millis();
  cycleTime += phaseDurations[(currentPhase + 7) % 8];
  cycleTime %= CYCLE_DURATION;
}

void forceAdvanceToRoad(uint8_t targetRoad) {
  if (!systemActive) {
    Serial.println("System not active, cannot advance");
    return;
  }
  
  for (uint8_t i = 0; i < 8; i++) {
    if (activeRoad[i] == targetRoad && !isYellow[i]) {
      currentPhase = i;
      phaseStartMillis = millis();
      cycleTime = 0;
      updateLEDs(currentPhase);
      Serial.printf("✅ Advanced to Road %d Green (Phase %d)\n", targetRoad+1, i);
      return;
    }
  }
  
  advancePhase();
  updateLEDs(currentPhase);
  Serial.printf("⚠️ Advanced one phase to Road %d\n", activeRoad[currentPhase]+1);
}

// ===================== Web Server Handlers =====================
void handleRoot() {
  addCORS();
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-1 Intersection A</title>
</head>
<body>
    <h1>🚦 ESP32-1 (Intersection A)</h1>
    <p>Supported: F01-F06 (All Faults - Manual Mode)</p>
    <p>Status: <span id="status">Waiting for dashboard...</span></p>
    <script>
        fetch('/state')
            .then(res => res.json())
            .then(data => {
                document.getElementById('status').textContent = 
                    data.active ? '✅ System Active' : '⏸️ System Inactive';
            });
    </script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleState() {
  addCORS();
  jsonDoc.clear();
  
  // LED states
  JsonArray leds = jsonDoc.createNestedArray("leds");
  for (int r = 0; r < 4; r++) {
    JsonArray roadLeds = leds.createNestedArray();
    roadLeds.add(digitalRead(ledPins[r][0]));
    roadLeds.add(digitalRead(ledPins[r][1]));
    roadLeds.add(digitalRead(ledPins[r][2]));
  }
  
  // Countdowns
  JsonArray countdowns = jsonDoc.createNestedArray("countdowns");
  uint32_t now = millis();
  uint32_t elapsed = (now - phaseStartMillis) / 1000;
  uint32_t currentCycleTime = cycleTime + elapsed;
  
  for (int r = 0; r < 4; r++) {
    uint32_t effectiveGreen = getEffectiveGreenTime(r);
    uint32_t effectiveYellow = getEffectiveYellowTime(r);
    uint32_t gStart = greenStart[r];
    uint32_t yStart = yellowStart[r];
    uint32_t gEnd = gStart + effectiveGreen;
    uint32_t yEnd = yStart + effectiveYellow;
    int32_t value = 0;
    
    if (!systemActive) {
      value = 0;
    } else if (currentCycleTime >= gStart && currentCycleTime < gEnd) {
      value = gEnd - currentCycleTime;
    } else if (currentCycleTime >= yStart && currentCycleTime < yEnd) {
      value = yEnd - currentCycleTime;
    } else {
      uint32_t nextGreen = (currentCycleTime < gStart) ? gStart : (gStart + CYCLE_DURATION);
      value = nextGreen - currentCycleTime;
    }
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    countdowns.add(value);
  }
  
  // Faults - All F01-F06 supported
  JsonArray faultArray = jsonDoc.createNestedArray("faults");
  for (int f = 0; f < 6; f++) {
    JsonObject fault = faultArray.createNestedObject();
    fault["active"] = faults[f].active;
    JsonArray roads = fault.createNestedArray("roads");
    for (int r = 0; r < faults[f].roadCount; r++) {
      roads.add(faults[f].roads[r]);
    }
    fault["value"] = faults[f].value;
  }
  
  // LED burnouts
  JsonArray burnoutArray = jsonDoc.createNestedArray("burnouts");
  for (int r = 0; r < 4; r++) {
    JsonArray roadBurnout = burnoutArray.createNestedArray();
    for (int l = 0; l < 3; l++) {
      roadBurnout.add(ledBurnout[r][l]);
    }
  }
  
  // ===== SENSOR DATA - EMPTY (No Sensors) =====
  JsonObject sensors = jsonDoc.createNestedObject("sensors");
  sensors["temp"] = 0;
  sensors["rain"] = 0;
  sensors["rainDetected"] = false;
  sensors["rainAvg"] = 0;
  sensors["rainDigital"] = 0;
  
  JsonObject rainDetail = sensors.createNestedObject("rainDetail");
  rainDetail["value"] = 0;
  rainDetail["average"] = 0;
  rainDetail["detected"] = false;
  rainDetail["digital"] = 0;
  rainDetail["min"] = 0;
  rainDetail["max"] = 0;
  rainDetail["threshold"] = 2000;
  
  // Per-road timing
  JsonObject roadTiming = jsonDoc.createNestedObject("roadTiming");
  JsonArray redTimes = roadTiming.createNestedArray("red");
  JsonArray greenTimes = roadTiming.createNestedArray("green");
  JsonArray yellowTimes = roadTiming.createNestedArray("yellow");
  for (int r = 0; r < 4; r++) {
    redTimes.add(roadRedTime[r]);
    greenTimes.add(roadGreenTime[r]);
    yellowTimes.add(roadYellowTime[r]);
  }
  
  jsonDoc["active"] = systemActive;
  jsonDoc["phase"] = currentPhase;
  jsonDoc["dashboardConnected"] = dashboardConnected;
  jsonDoc["status"] = "ready";
  
  String output;
  serializeJson(jsonDoc, output);
  server.send(200, "application/json", output);
}

void handleAddFault() {
  addCORS();
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
    return;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
    return;
  }
  
  int faultIdx = doc["fault"];
  int road = doc["road"];
  int seconds = doc["seconds"] | 5;
  
  if (faultIdx < 0 || faultIdx > 5) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid fault code\"}");
    return;
  }
  
  // F01 - LED Burnout - Individual LED support
  if (faultIdx == 0) {
    if (doc.containsKey("leds")) {
      JsonArray leds = doc["leds"].as<JsonArray>();
      for (int i = 0; i < leds.size() && i < 3; i++) {
        if (leds[i].as<bool>()) {
          ledBurnout[road][i] = true;
        }
      }
    } else {
      for (int i = 0; i < 3; i++) {
        ledBurnout[road][i] = true;
      }
    }
    
    bool anyBurned = false;
    for (int i = 0; i < 3; i++) {
      if (ledBurnout[road][i]) anyBurned = true;
    }
    
    if (anyBurned) {
      if (!faults[0].active) {
        faults[0].active = true;
        faults[0].roadCount = 0;
      }
      
      bool roadExists = false;
      for (int i = 0; i < faults[0].roadCount; i++) {
        if (faults[0].roads[i] == road) roadExists = true;
      }
      if (!roadExists && faults[0].roadCount < 4) {
        faults[0].roads[faults[0].roadCount++] = road;
      }
    } else {
      for (int i = 0; i < faults[0].roadCount; i++) {
        if (faults[0].roads[i] == road) {
          for (int j = i; j < faults[0].roadCount - 1; j++) {
            faults[0].roads[j] = faults[0].roads[j+1];
          }
          faults[0].roadCount--;
          break;
        }
      }
      if (faults[0].roadCount == 0) {
        faults[0].active = false;
      }
    }
    
    String burnedLEDs = "";
    if (ledBurnout[road][0]) burnedLEDs += "R";
    if (ledBurnout[road][1]) burnedLEDs += "Y";
    if (ledBurnout[road][2]) burnedLEDs += "G";
    Serial.printf("F01 - LED Burnout on Road %d: [%s]\n", road+1, burnedLEDs.c_str());
    
    server.send(200, "application/json", "{\"success\":true,\"message\":\"LED Burnout added successfully\"}");
    return;
  }
  
  // F02 and F03 are manually controlled (no sensors)
  if (faultIdx == 1 || faultIdx == 2) {
    faults[faultIdx].active = true;
    if (road != 255 && faults[faultIdx].roadCount < 4) {
      bool exists = false;
      for (int i = 0; i < faults[faultIdx].roadCount; i++) {
        if (faults[faultIdx].roads[i] == road) exists = true;
      }
      if (!exists) {
        faults[faultIdx].roads[faults[faultIdx].roadCount++] = road;
      }
    } else if (road == 255) {
      faults[faultIdx].roadCount = 4;
      for (int i = 0; i < 4; i++) faults[faultIdx].roads[i] = i;
    }
    Serial.printf("F%02d - %s added on %s\n", faultIdx+1, faultNames[faultIdx], 
                  road == 255 ? "All Roads" : String(road+1).c_str());
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Fault added successfully\"}");
    return;
  }
  
  // F04, F05, F06
  faults[faultIdx].active = true;
  if (road != 255 && faults[faultIdx].roadCount < 4) {
    bool exists = false;
    for (int i = 0; i < faults[faultIdx].roadCount; i++) {
      if (faults[faultIdx].roads[i] == road) exists = true;
    }
    if (!exists) {
      faults[faultIdx].roads[faults[faultIdx].roadCount++] = road;
    }
  } else if (road == 255) {
    faults[faultIdx].roadCount = 4;
    for (int i = 0; i < 4; i++) faults[faultIdx].roads[i] = i;
  }
  
  if (faultIdx == 4 || faultIdx == 5) {
    faults[faultIdx].value = seconds;
    if (faultIdx == 4) {
      roadGreenTime[road] += seconds;
    } else if (faultIdx == 5) {
      roadGreenTime[road] = max(5, (int)roadGreenTime[road] - seconds);
    }
    rebuildPhaseArrays();
  }
  
  Serial.printf("F%02d - %s added on %s\n", faultIdx+1, faultNames[faultIdx], 
                road == 255 ? "All Roads" : String(road+1).c_str());
  if (faultIdx == 4 || faultIdx == 5) {
    Serial.printf("  → Timing adjusted by %d seconds\n", seconds);
  }
  
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Fault added successfully\"}");
}

void handleRemoveFault() {
  addCORS();
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
    return;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
    return;
  }
  
  int faultIdx = doc["fault"];
  int road = doc["road"];
  int ledIndex = doc["led_index"] | -1;
  
  if (faultIdx < 0 || faultIdx > 5) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid fault code\"}");
    return;
  }
  
  // F01 - LED Burnout - Individual LED resolution
  if (faultIdx == 0) {
    if (doc.containsKey("leds")) {
      JsonArray leds = doc["leds"].as<JsonArray>();
      for (int i = 0; i < leds.size() && i < 3; i++) {
        if (!leds[i].as<bool>()) {
          ledBurnout[road][i] = false;
        }
      }
    } else if (ledIndex >= 0 && ledIndex < 3) {
      ledBurnout[road][ledIndex] = false;
    } else {
      for (int i = 0; i < 3; i++) {
        ledBurnout[road][i] = false;
      }
    }
    
    bool anyBurned = false;
    for (int i = 0; i < 3; i++) {
      if (ledBurnout[road][i]) anyBurned = true;
    }
    
    if (!anyBurned) {
      for (int i = 0; i < faults[0].roadCount; i++) {
        if (faults[0].roads[i] == road) {
          for (int j = i; j < faults[0].roadCount - 1; j++) {
            faults[0].roads[j] = faults[0].roads[j+1];
          }
          faults[0].roadCount--;
          break;
        }
      }
      if (faults[0].roadCount == 0) {
        faults[0].active = false;
      }
    }
    
    String remainingLEDs = "";
    if (ledBurnout[road][0]) remainingLEDs += "R";
    if (ledBurnout[road][1]) remainingLEDs += "Y";
    if (ledBurnout[road][2]) remainingLEDs += "G";
    Serial.printf("F01 - LED Burnout on Road %d: remaining [%s]\n", road+1, remainingLEDs.c_str());
    
    server.send(200, "application/json", "{\"success\":true,\"message\":\"LED Burnout resolved successfully\"}");
    return;
  }
  
  // F02 and F03 are manually resolved
  if (faultIdx == 1 || faultIdx == 2) {
    if (road == 255) {
      faults[faultIdx].active = false;
      faults[faultIdx].roadCount = 0;
    } else {
      for (int i = 0; i < faults[faultIdx].roadCount; i++) {
        if (faults[faultIdx].roads[i] == road) {
          for (int j = i; j < faults[faultIdx].roadCount - 1; j++) {
            faults[faultIdx].roads[j] = faults[faultIdx].roads[j+1];
          }
          faults[faultIdx].roadCount--;
          break;
        }
      }
      if (faults[faultIdx].roadCount == 0) {
        faults[faultIdx].active = false;
      }
    }
    Serial.printf("F%02d - %s removed\n", faultIdx+1, faultNames[faultIdx]);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Fault resolved successfully\"}");
    return;
  }
  
  // F04, F05, F06
  if (road == 255) {
    faults[faultIdx].active = false;
    faults[faultIdx].roadCount = 0;
  } else {
    for (int i = 0; i < faults[faultIdx].roadCount; i++) {
      if (faults[faultIdx].roads[i] == road) {
        for (int j = i; j < faults[faultIdx].roadCount - 1; j++) {
          faults[faultIdx].roads[j] = faults[faultIdx].roads[j+1];
        }
        faults[faultIdx].roadCount--;
        break;
      }
    }
    if (faults[faultIdx].roadCount == 0) {
      faults[faultIdx].active = false;
    }
  }
  
  if (faultIdx == 4 || faultIdx == 5) {
    roadGreenTime[road] = GREEN_TIME;
    rebuildPhaseArrays();
    Serial.printf("  → Timing reset to default\n");
  }
  
  Serial.printf("F%02d - %s removed\n", faultIdx+1, faultNames[faultIdx]);
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Fault resolved successfully\"}");
}

void handleGlobalTiming() {
  addCORS();
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
    return;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
    return;
  }
  
  int red = doc["red"];
  int green = doc["green"];
  int yellow = doc["yellow"];
  
  for (int i = 0; i < 4; i++) {
    if (red >= 5 && red <= 60) roadRedTime[i] = red;
    if (green >= 5 && green <= 60) roadGreenTime[i] = green;
    if (yellow >= 1 && yellow <= 10) roadYellowTime[i] = yellow;
  }
  
  RED_TIME = red;
  GREEN_TIME = green;
  YELLOW_TIME = yellow;
  
  rebuildPhaseArrays();
  
  Serial.printf("Global timing updated: Red=%d, Green=%d, Yellow=%d\n", red, green, yellow);
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Global timing updated\"}");
}

void handleRoadTimingUpdate() {
  addCORS();
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
    return;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
    return;
  }
  
  int road = doc["road"];
  int red = doc["red"];
  int green = doc["green"];
  int yellow = doc["yellow"];
  
  if (road < 0 || road > 3) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid road index\"}");
    return;
  }
  
  if (red >= 5 && red <= 60) roadRedTime[road] = red;
  if (green >= 5 && green <= 60) roadGreenTime[road] = green;
  if (yellow >= 1 && yellow <= 10) roadYellowTime[road] = yellow;
  
  rebuildPhaseArrays();
  
  Serial.printf("Road %d timing updated: Red=%d, Green=%d, Yellow=%d\n", 
                road+1, roadRedTime[road], roadGreenTime[road], roadYellowTime[road]);
  
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Road timing updated in real-time\"}");
}

void handleToggle() {
  addCORS();
  systemActive = !systemActive;
  
  if (systemActive) {
    phaseStartMillis = millis();
    currentPhase = 0;
    cycleTime = 0;
    updateLEDs(currentPhase);
    Serial.println("System started by dashboard");
  } else {
    for (uint8_t r = 0; r < 4; r++) {
      setRoadLEDs(r, LOW, LOW, LOW);
    }
    for (uint8_t i = 0; i < 4; i++) {
      displays[i].clear();
      lastDisplayValue[i] = -1;
    }
    Serial.println("System stopped by dashboard");
  }
  
  server.send(200, "application/json", "{\"success\":true,\"message\":\"System " + String(systemActive ? "started" : "stopped") + "\"}");
}

void handleAdvance() {
  addCORS();
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
    return;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
    return;
  }
  
  int targetRoad = doc["targetRoad"];
  
  if (targetRoad < 0 || targetRoad > 3) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid road index\"}");
    return;
  }
  
  if (!systemActive) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"System not active\"}");
    return;
  }
  
  forceAdvanceToRoad(targetRoad);
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Advanced to Road " + String(targetRoad+1) + " Green\"}");
}

void handleDashboardConnect() {
  addCORS();
  dashboardConnected = true;
  Serial.println("\n✅ Dashboard connected!");
  if (!systemInitialized) initializeSystem();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Dashboard connected\"}");
}

void handleOptions() {
  addCORS();
  server.send(204);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/fault/add", HTTP_POST, handleAddFault);
  server.on("/fault/remove", HTTP_POST, handleRemoveFault);
  server.on("/global/timing", HTTP_POST, handleGlobalTiming);
  server.on("/road/timing/update", HTTP_POST, handleRoadTimingUpdate);
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/advance", HTTP_POST, handleAdvance);
  server.on("/connect", HTTP_POST, handleDashboardConnect);
  
  server.on("/state", HTTP_OPTIONS, handleOptions);
  server.on("/fault/add", HTTP_OPTIONS, handleOptions);
  server.on("/fault/remove", HTTP_OPTIONS, handleOptions);
  server.on("/global/timing", HTTP_OPTIONS, handleOptions);
  server.on("/road/timing/update", HTTP_OPTIONS, handleOptions);
  server.on("/toggle", HTTP_OPTIONS, handleOptions);
  server.on("/advance", HTTP_OPTIONS, handleOptions);
  server.on("/connect", HTTP_OPTIONS, handleOptions);
  
  server.onNotFound([]() {
    addCORS();
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
  });
  
  server.begin();
  Serial.println("✅ Web server started");
}

// ===================== Setup =====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n----------------------------------------");
  Serial.println("🔌 Connecting to WiFi...");
  Serial.print("  SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  Serial.print("  Progress: ");
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected successfully!");
    Serial.println("----------------------------------------");
    Serial.println("📡 NETWORK INFORMATION:");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("  Subnet Mask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("  MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("  Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.println("----------------------------------------");
  } else {
    Serial.println("\n❌ WiFi connection failed!");
    Serial.println("  Please check your SSID and password");
  }
  
  setupWebServer();
  
  Serial.println("\n========================================");
  Serial.println("🚦 ESP32-1 (Intersection A)");
  Serial.println("========================================");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("⏳ Waiting for Dashboard Connection...");
    Serial.println("----------------------------------------");
    Serial.print("   Connect to: http://");
    Serial.println(WiFi.localIP());
    Serial.println("----------------------------------------");
    Serial.print("   ▶ ");
    Serial.println(WiFi.localIP());
    Serial.println("\n   📌 Supported Faults:");
    Serial.println("   - F01: LED Burnout (Individual LED control)");
    Serial.println("   - F02: Overheating (Manual)");
    Serial.println("   - F03: Water Leakage (Manual)");
    Serial.println("   - F04: Phase Skipping");
    Serial.println("   - F05: Over-Timing");
    Serial.println("   - F06: Under-Timing");
    Serial.println("\n   📌 Sensors:");
    Serial.println("   - No sensors installed on this device");
  } else {
    Serial.println("⚠️ WiFi not connected");
    Serial.println("   Check WiFi credentials and restart");
  }
  Serial.println("========================================");
  Serial.println("📌 System will initialize when dashboard connects");
  Serial.println("========================================\n");
}

// ===================== Main Loop =====================
void loop() {
  server.handleClient();
  
  if (systemActive) {
    uint32_t now = millis();
    uint32_t elapsedSeconds = (now - phaseStartMillis) / 1000;
    
    if (elapsedSeconds >= phaseDurations[currentPhase]) {
      uint8_t currentRoad = activeRoad[currentPhase];
      if (isRoadAffectedByFault(currentRoad, 3)) {
        currentPhase = (currentPhase + 2) % 8;
        phaseStartMillis = millis();
        uint32_t skippedTime = phaseDurations[(currentPhase + 7) % 8] + phaseDurations[(currentPhase + 6) % 8];
        cycleTime += skippedTime;
        cycleTime %= CYCLE_DURATION;
        updateLEDs(currentPhase);
        Serial.printf("⏭️ Phase Skip: Advanced from Road %d to Road %d\n", 
                      currentRoad+1, activeRoad[currentPhase]+1);
      } else {
        advancePhase();
        updateLEDs(currentPhase);
      }
      elapsedSeconds = 0;
    }
    
    uint32_t currentCycleTime = cycleTime + elapsedSeconds;
    for (uint8_t road = 0; road < 4; road++) {
      updateDisplay(road, currentCycleTime);
    }
  }
  
  delay(50);
}
