# 🚽 Smart Toilet Hygiene Monitor: Temporal Physics Edition

An advanced IoT hygiene monitoring system built on the **ESP32**. This project uses a dual-core **FreeRTOS** implementation to separate real-time sensor processing from web server handling, utilizing a custom physics-based model to calculate a real-time "Hygiene Score."



## 🚀 Key Features
* **Physics-Based Degradation Model:** Instead of simple thresholds, the system uses a temporal model where the hygiene score degrades based on occupancy time, gas levels, and thermal catalysts.
* **Dual-Core Execution:** * **Core 0:** Manages the WiFi Access Point and Web Server.
    * **Core 1:** Handles sensor fusion (MQ-135, DHT11, PIR) and mathematical modeling.
* **Dynamic Web Interface:** A modern, responsive dashboard stored in `PROGMEM` featuring live-syncing data and dynamic color-coding based on the toilet's status.
* **Automated Actuators:** Intelligent logic for Fan (Relay), Deodorizer (LED), and UV Sanitization.

---

## 🛠 Hardware Requirements
| Component | Purpose |
| :--- | :--- |
| **ESP32** | Main Microcontroller |
| **MQ-135** | Gas/Air Quality Sensor (Odour detection) |
| **DHT11/22** | Ambient Temperature & Humidity |
| **PIR Sensor** | Occupancy Detection |
| **Relay Module** | Exhaust Fan Control |
| **LEDs** | Status indicators for UV and Deodorizer |

---

## 🧠 The "Temporal Physics" Model
The Hygiene Score ($H$) is not just a static reading. It follows a differential logic:

1.  **Degradation:** When the stall is occupied or gas levels rise, the score drops. This is accelerated by a **Thermal Catalyst Factor** (Heat).
    $$Drop = T_{factor} \times (4.0 \times (G_{norm} + H_{norm} + Occupied))$$
2.  **Recovery:** When vacant and clean, the score recovers using an inertia-driven exponential curve.
    $$Recovery = 0.2 + 3.0 \times (\frac{H_{current}}{100})^2$$

<img width="1398" height="597" alt="image" src="https://github.com/user-attachments/assets/2cb01dc0-3216-4d5b-9095-10fb1f145adb" />

## 📸 Dashboard Preview
The UI changes dynamically based on the hygiene state:
* **Green (Clean):** Optimal conditions.
* **Yellow (Warning):** Moderate odour or high humidity detected.
* **Red (Unclean):** Immediate cleaning required or high gas levels.
* **Blue (Occupied):** Privacy mode active; UV sanitization disabled for safety.
