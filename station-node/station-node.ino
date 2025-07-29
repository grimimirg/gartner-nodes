#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>

/* ─────────────────────────────
   Pinout & Constants
   ───────────────────────────── */

/* DHT22 sensor config */
#define DHTPIN      2
#define DHTTYPE     DHT22
DHT tempHumidSensor(DHTPIN, DHTTYPE);

/* photoresistor config */
#define LDR_PIN     A0

/* LoRa pins */
#define LORA_SS     18
#define LORA_RST    14
#define LORA_DIO0   26
#define LORA_FREQ   868E6  // EU band

/* ─────────────────────────────
   SETUP
   ───────────────────────────── */
void setup() {
  Serial.begin(115200);
  tempHumidSensor.begin();
  setupLoRa();
}

/* ─────────────────────────────
   LOOP
   ───────────────────────────── */
void loop() {
  float temperature = tempHumidSensor.readTemperature();  // Celsius
  float humidity    = tempHumidSensor.readHumidity();     // %RH

  int lightRaw = analogRead(LDR_PIN);
  
  // Transform to light percentage
  float lightPercent = map(lightRaw, 0, 1023, 0, 100);

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Error reading DHT sensor!");
    delay(2000);
    return;
  }

  sendLoRaPayload(temperature, humidity, lightPercent);

  // next read in 1 min
  delay(60000);
}

/* ─────────────────────────────
   FUNCTIONS
   ───────────────────────────── */
void sendLoRaPayload(float temperature, float humidity, float lightPercent) {
  String jsonPayload = String("{") 
             + "\"to\":\"master-node\","
             + "\"temperature\":" + String(temperature, 2) + ","
             + "\"humidity\":"    + String(humidity,    2) + ","
             + "\"lightPercent\":" + String(lightPercent, 2)
             + "}";

  LoRa.beginPacket();
  LoRa.print(jsonPayload);
  LoRa.endPacket();

  Serial.println("Inviato payload:");
  Serial.println(jsonPayload);
}

//----------------------------------------------------------------------------------
/* Utilities */
//----------------------------------------------------------------------------------
void setupLoRa() {
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed");
    while (true) delay(1000);
  }

  Serial.println("LoRa ready!");
}
