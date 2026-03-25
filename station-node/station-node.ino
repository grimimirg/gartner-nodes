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

/* Timing and validation constants */
#define READ_INTERVAL_MS    60000UL  // 1 minute
#define ERROR_DELAY_MS      2000UL
#define MAX_RETRIES         3
#define RETRY_DELAY_MS      1000UL

/* Sensor validation ranges */
#define MIN_TEMP_C          -40.0f
#define MAX_TEMP_C          80.0f
#define MIN_HUMIDITY        0.0f
#define MAX_HUMIDITY        100.0f

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

  // Validate sensor readings
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Error reading DHT sensor!");
    delay(ERROR_DELAY_MS);
    return;
  }
  
  // Validate reasonable ranges
  if (temperature < MIN_TEMP_C || temperature > MAX_TEMP_C) {
    Serial.println("Temperature out of range!");
    delay(ERROR_DELAY_MS);
    return;
  }
  
  if (humidity < MIN_HUMIDITY || humidity > MAX_HUMIDITY) {
    Serial.println("Humidity out of range!");
    delay(ERROR_DELAY_MS);
    return;
  }

  sendLoRaPayload(temperature, humidity, lightPercent);

  // next read
  delay(READ_INTERVAL_MS);
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

  // Retry mechanism for LoRa transmission
  int retryCount = 0;
  bool success = false;
  
  while (retryCount < MAX_RETRIES && !success) {
    LoRa.beginPacket();
    LoRa.print(jsonPayload);
    success = LoRa.endPacket();
    
    if (success) {
      Serial.println("Payload sent successfully:");
      Serial.println(jsonPayload);
    } else {
      Serial.printf("Transmission failed, attempt %d/%d\n", retryCount + 1, MAX_RETRIES);
      retryCount++;
      delay(RETRY_DELAY_MS); // Wait before retry
    }
  }
  
  if (!success) {
    Serial.println("Error: Unable to send payload after all attempts");
  }
}

//----------------------------------------------------------------------------------
/* Utilities */
//----------------------------------------------------------------------------------
void setupLoRa() {
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa initialization failed");
    while (true) delay(1000);
  }

  Serial.println("LoRa ready!");
}
