# Master Node Firmware

This repository contains the firmware for the **ESP32-based Master Node** that orchestrates distributed irrigation controllers via LoRa and Wi‑Fi. It accepts a high-level JSON DSL of rules, monitors sensor data, and dispatches irrigation programs to zone controllers.

---

## Architecture Overview

![High-Level Architecture](gartner_schema.jpg)

1. **Mobile App / Web Client** sends a DSL string (`when… then… terminate when…`) to the Master Node over HTTP.

2. **ESP32 Master Node**:

   * Exposes a `/saveProgram` HTTP endpoint to receive and store `program.json` on LittleFS.
   * Every 10 minutes checks for updates and reloads the rule set.
   * Listens on LoRa for JSON sensor payloads (`temperature`, `humidity`, `light`) and updates in-memory stats.
   * Converts the DSL into a list of `ProgramRule` objects and evaluates them against current sensor stats.
   * When a rule fires, constructs a single JSON payload including:

     * `zone` (string identifier)
     * `command` (e.g. "irrigate")
     * `duration` (milliseconds)
     * `terminationCondition` (optional nested condition tree)
   * Sends this payload via LoRa to the corresponding Zone Controller.

3. **Zone Controller** (LoRa receiver) parses the JSON payload, opens the valve for the specified duration, and autonomously stops irrigation based on the embedded stop condition.

---

## Key Features

* **DSL‑Driven**: Supports expressive rules with `when` (start), `then` (action), and `terminate when` (stop) clauses.
* **JSON Storage**: Persists configuration on LittleFS; easy to version and review.
* **Periodic Scheduling**: Checks for new programs every 10 minutes, avoiding continuous polling.
* **LoRa Integration**: One‑way sensor updates and program dispatch, minimizing RF traffic overhead.
* **Autonomous Zone Control**: Offloads timing and stop logic to individual controllers.

---

## Getting Started

1. **Configure Wi‑Fi**: Edit `WIFI_SSID` and `WIFI_PASS` in `master-node.ino`.
2. **Build & Flash**: Use the Arduino IDE or PlatformIO to compile and upload to your ESP32.
3. **Upload Rules**:

   ```bash
   curl -X POST http://<ESP32_IP>/saveProgram \
     -H 'Content-Type: application/json' \
     -d '{
           "conditions": [ ... ],
           "action": {"command":"open","zone":"zone1","duration":{"value":5,"unit":"min"}},
           "terminationCondition": { ... }
         }'
   ```
4. **Monitor Serial**: On startup, the node prints status messages (`Web server started!`, `LoRa ready!`, etc.) and logs rule executions.

---

## License

MIT License © Your Name
