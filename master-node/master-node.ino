#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServerule.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

using std::vector;

/* ─────────────────────────────
   Pinout & Constants
   ───────────────────────────── */
#define LORA_SS    18
#define LORA_RST   14
#define LORA_DIO0  26
#define LORA_FREQ  868E6          // EU band

const char* WIFI_SSID = "[change_it]";
const char* WIFI_PASS = "[change_it]";

/* HTTP Helpers */
enum class HttpStatus : int {
  OK           = 200,
  BAD_REQUEST  = 400,
  NOT_FOUND    = 404
};

const char CONTENT_JSON[] = "application/json";
const char INVALID_API[]  = "{\"error\":\"Invalid API\"}";
const char INVALID_JSON[] = "{\"error\":\"Invalid JSON\"}";
const char MISSING_BODY[] = "{\"error\":\"Missing body\"}";

/* scheduler */
unsigned long lastCheck = 0;
const unsigned long checkInterval = 10UL * 60UL * 1000UL;  // 10 minutes

/* ─────────────────────────────
   Models
   ───────────────────────────── */
struct ProgramRule {
  String   sensor;
  String   zone;
  String   op;
  float    value;
  String   command;
  String   actionZone;
  uint32_t durationMs;
  String   terminationCondition;
};

struct SensorStats {
  float temperature = 0;
  float humidity    = 0;
  float light       = 0;
  unsigned long timestamp = 0;
};

/* ─────────────────────────────
   Globals
   ───────────────────────────── */
SensorStats currentStats;
WebServer server(80);
vector<ProgramRule> ruleList;

//----------------------------------------------------------------------------------

/* ─────────────────────────────
   SETUP
   ───────────────────────────── */
void setup() {
  Serial.begin(115200);

  prepareFS();
  setupLoRa();
  setLoRaReceiveMode();
  connectToWifi();

  ruleList.clear();
  // initially mark as executed
  markProgramUpdated(false);

  server.on("/saveProgram", HTTP_POST, saveProgram);
  server.onNotFound([]() {
    server.send(int(HttpStatus::NOT_FOUND), CONTENT_JSON, INVALID_API);
  });
  server.begin();
  Serial.println("Web server started!");
}

/* ─────────────────────────────
   LOOP
   ───────────────────────────── */
void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastCheck >= checkInterval) {
    lastCheck = now;
    if (isProgramUpdated()) {
      executeProgram();
    }
  }
}

//----------------------------------------------------------------------------------

/* ─────────────────────────────
   FUNCTIONS
   ───────────────────────────── */
void onSensorPacketReceived(int packetSize) {
  if (packetSize == 0) return;
  String payload;
  while (LoRa.available()) {
    payload += (char)LoRa.read();
  }

  Serial.print("Received LoRa: ");
  Serial.println(payload);
  onSensorJson(payload);
  LoRa.receive();  // re-enter receive mode
}

void onSensorJson(const String& json) {
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, json)) {
    Serial.println("Sensor JSON parse error!");
    return;
  }

  currentStats.timestamp   = millis();
  currentStats.temperature = doc["temperature"] | currentStats.temperature;
  currentStats.humidity    = doc["humidity"]    | currentStats.humidity;
  currentStats.light       = doc["light"]       | currentStats.light;
}

void saveProgram() {
  if (!server.hasArg("plain")) {
    server.send(int(HttpStatus::BAD_REQUEST), CONTENT_JSON, MISSING_BODY);
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<512> doc;

  auto err = deserializeJson(doc, body);
  if (err) {
    server.send(int(HttpStatus::BAD_REQUEST), CONTENT_JSON, INVALID_JSON);
    return;
  }

  persistProgram(body);
  server.send(int(HttpStatus::OK), CONTENT_JSON, "{\"status\":\"ok\"}");
}

void persistProgram(const String& jsonProgram) {
  File programFile = LittleFS.open("/program.json", FILE_WRITE);
  if (!programFile) {
    Serial.println("Failed opening '/program.json'");
    return;
  }

  programFile.print(jsonProgram);
  programFile.close();
  markProgramUpdated(true);
  Serial.println("Program saved!");
}

void executeProgram() {
  File programFile = LittleFS.open("/program.json", "r");
  if (!programFile) {
    Serial.println("Failed to open '/program.json'");
    return;
  }

  DynamicJsonDocument doc(1024);
  auto err = deserializeJson(doc, programFile);
  programFile.close();

  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  ruleList = buildRuleList(doc);
  checkRules(ruleList);

  // after execution, reset flag
  markProgramUpdated(false);
}

void checkRules(const vector<ProgramRule>& rules) {
  for (const auto& rule : rules) {
    float actual = 0;
    if      (rule.sensor == "temperature") actual = currentStats.temperature;
    else if (rule.sensor == "humidity")    actual = currentStats.humidity;
    else if (rule.sensor == "light")       actual = currentStats.light;
    
    bool match = false;
    if      (rule.op == ">")  match = actual >  rule.value;
    else if (rule.op == "<")  match = actual <  rule.value;
    else if (rule.op == ">=") match = actual >= rule.value;
    else if (rule.op == "<=") match = actual <= rule.value;
    else if (rule.op == "==") match = actual == rule.value;
    else if (rule.op == "!=") match = actual != rule.value;
    
    if (match) {
      // Build full program JSON payload
      StaticJsonDocument<256> payloadDoc;

      payloadDoc["zone"]     = rule.actionZone;
      payloadDoc["command"]  = rule.command;
      payloadDoc["duration"] = rule.durationMs;

      if (rule.terminationCondition.length()) {
        deserializeJson(payloadDoc["terminationCondition"], rule.terminationCondition);
      }

      String loRaPayload;

      serializeJson(payloadDoc, loRaPayload);
      sendLoRaCommand(rule.actionZone, loRaPayload);
      Serial.printf("Rule sent: %s %s %s %.1f\n",
                    rule.sensorule.c_str(), rule.op.c_str(), rule.actionZone.c_str(), rule.value);
    }
  }
}

void sendLoRaCommand(const String& nodeId, const String& payload) {
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  Serial.printf("Successfully sent payload to %s: %s\n", nodeId.c_str(), payload.c_str());
}

/* Utilities */
//----------------------------------------------------------------------------------
void prepareFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    while (true) delay(1000);
  }
  Serial.println("LittleFS mounted!");
}

void setupLoRa() {
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed");
    while (true) delay(1000);
  }
  Serial.println("LoRa ready!");
}

void setLoRaReceiveMode() {
  LoRa.onReceive(onSensorPacketReceived);
  LoRa.receive();
}

void connectToWifi() {
  Serial.print("Connecting to Wi‑Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.print("\nWi‑Fi connected ‑ IP: ");
  Serial.println(WiFi.localIP());
}

void markProgramUpdated(bool flag) {
  File f = LittleFS.open("/program-updated", FILE_WRITE);
  if (!f) {
    Serial.println("Error opening '/program-updated'");
    return;
  }
  f.print(flag ? '1' : '0');
  f.close();
}

bool isProgramUpdated() {
  File f = LittleFS.open("/program-updated", "r");
  if (!f) {
    Serial.println("Error opening '/program-updated'");
    return false;
  }
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  return (s == "1");
}

vector<ProgramRule> buildRuleList(const JsonDocument& doc) {
  vector<ProgramRule> rules;
  // Extract termination condition JSON once
  String termJson;
  if (doc.containsKey("terminationCondition")) {
    serializeJson(doc["terminationCondition"], termJson);
  }

  JsonArray  conds = doc["conditions"].as<JsonArray>();
  JsonObject act   = doc["action"];
  uint32_t   durMs = act["duration"]["value"].as<uint32_t>() * 1000UL;
  for (JsonObject c : conds) {
    ProgramRule rule;
    rule.sensor                = c["sensor"].as<String>();
    rule.zone                  = c["zone"].as<String>();
    rule.op                    = c["operator"].as<String>();
    rule.value                 = c["value"].as<float>();
    rule.command               = act["command"].as<String>();
    rule.actionZone            = act["zone"].as<String>();
    rule.durationMs            = durMs;
    rule.terminationCondition  = termJson;
    rules.push_back(std::move(rule));
  }
  return rules;
}