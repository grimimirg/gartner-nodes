#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

/* ─────────────────────────────
   Pinout & Constants
   ───────────────────────────── */
#define LORA_SS    18
#define LORA_RST   14
#define LORA_DIO0  26
#define LORA_FREQ  868E6          // EU band, change if needed

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

/* ─────────────────────────────
   ProgramRule Model
   ───────────────────────────── */
struct ProgramRule {
  String sensor;
  String zone;
  String op;
  float  value;
  String command;
  String actionZone;
  uint16_t duration;
  String durationUnit;
};

/* Web server */
WebServer server(80);

/* ─────────────────────────────
   SETUP
   ───────────────────────────── */
void setup() {
  Serial.begin(115200);

  setupLoRa();
  connectToWifi();
  prepareFS();

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
}

/* ─────────────────────────────
   FUNCTIONS
   ───────────────────────────── */
void setupLoRa() {
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed");
    while (true) delay(1000);
  }

  Serial.println("LoRa ready!");
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

void prepareFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  } else {
    Serial.println("LittleFS mounted!");
  }
}

void saveProgram() {
  if (!server.hasArg("plain")) {
    server.send(int(HttpStatus::BAD_REQUEST), CONTENT_JSON, MISSING_BODY);
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(int(HttpStatus::BAD_REQUEST), CONTENT_JSON, INVALID_JSON);
    return;
  }

  persistProgram(body);
  server.send(int(HttpStatus::OK), CONTENT_JSON, "{\"status\":\"ok\"}");
}

void persistProgram(const String& jsonProgram) {
  File programFile = LittleFS.open("/program.json", FILE_APPEND);

  if (!programFile) {
    Serial.println("Failed opening '/program.json'");
    return;
  }

  programFile.println(jsonProgram);
  programFile.close();

  Serial.println("Program saved!");
}

void executeProgram() {
  File programFile = LittleFS.open("/program.json", "r");
  if (!programFile) {
    Serial.println("Failed to open '/program.json'");
    return;
  }

  DynamicJsonDocument programDoc(1024);
  DeserializationError parseError = deserializeJson(programDoc, programFile);

  programFile.close();

  if (parseError) {
    Serial.print("JSON parse error: ");
    Serial.println(parseError.c_str());
    return;
  }

  std::vector<ProgramRule> ruleList = buildRuleList(programDoc);
  for (const ProgramRule& rule : ruleList) {
    Serial.printf("IF %s %s %s %.1f ⇒ %s %s for %u %s\n",
                  rule.sensor.c_str(), rule.zone.c_str(),
                  rule.op.c_str(), rule.value,
                  rule.command.c_str(), rule.actionZone.c_str(),
                  rule.duration, rule.durationUnit.c_str());
  }
}

std::vector<ProgramRule> buildRuleList(const JsonDocument& programDoc) {
  std::vector<ProgramRule> rules;

  JsonArray  conditionsArray = programDoc["conditions"].as<JsonArray>();
  JsonObject actionObject    = programDoc["action"];

  String   command        = actionObject["command"].as<String>();
  String   targetZone     = actionObject["zone"].as<String>();
  uint16_t actionDuration = actionObject["duration"]["value"];
  String   durationUnit   = actionObject["duration"]["unit"].as<String>();

  for (JsonObject conditionObj : conditionsArray) {
    ProgramRule rule;

    rule.sensor       = conditionObj["sensor"].as<String>();
    rule.zone         = conditionObj["zone"].as<String>();
    rule.op           = conditionObj["operator"].as<String>();
    rule.value        = conditionObj["value"];

    rule.command      = command;
    rule.actionZone   = targetZone;
    rule.duration     = actionDuration;
    rule.durationUnit = durationUnit;

    rules.push_back(std::move(rule));
  }
  
  return rules;
}

void sendLoRaCommand(uint8_t nodeId, const String& action) {
  String msg = String(nodeId) + ':' + action;
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();
  Serial.printf("Sent \"%s\"\n", msg.c_str());
}
