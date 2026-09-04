# 🚦 Traffic Light Control System

<div align="center">

![Version](https://img.shields.io/badge/version-2.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32-green)
![License](https://img.shields.io/badge/license-MIT-orange)
![Python](https://img.shields.io/badge/python-3.8+-yellow)
![Flask](https://img.shields.io/badge/flask-2.3.3-red)

**A complete traffic light control system with fault injection and resolution capabilities**

[Features](#-features) • [Hardware](#-hardware-requirements) • [Installation](#-installation) • [Usage](#-usage) • [API Documentation](#-api-documentation)

</div>

---

## 📸 Preview

<div align="center">

### Fault Creation Dashboard
![Fault Creation Dashboard](docs/images/dashboard_creation.png)

### Fault Resolution Dashboard
![Fault Resolution Dashboard](docs/images/dashboard_resolution.png)

</div>

---

## ✨ Features

### 🔧 Fault Management
| Fault Code | Name | Description | Detection |
|------------|------|-------------|-----------|
| **F01** | LED Burnout | Individual LED burnout simulation | Manual |
| **F02** | Overheating | Temperature threshold exceeded | Auto (DHT11) |
| **F03** | Water Leakage | Rain/water detected | Auto (Rain Sensor) |
| **F04** | Phase Skipping | Skips traffic light phase | Manual |
| **F05** | Over-Timing | Extended green/yellow time | Manual |
| **F06** | Under-Timing | Reduced green/yellow time | Manual |

### 🎮 Dashboard Features
- ✅ **Per-Road Controls** - Independent control for each road
- ✅ **Individual LED Burnout** - Burn specific LEDs (Red, Yellow, Green)
- ✅ **Seconds Adjustment** - +/- buttons for timing faults
- ✅ **Real-time Monitoring** - Live traffic light status
- ✅ **Fault History** - Complete log of all events
- ✅ **Signal Reset** - Reset all faults and timers to defaults
- ✅ **Sensor Data Display** - Temperature and rain sensor readings

### 🔌 Hardware Support
- **ESP32-1 (Intersection A)** - Full F01-F06 (No Sensors)
- **ESP32-2 (Intersection B)** - Full F01-F06 with DHT11 & Rain Sensor

---

## 🛠️ Hardware Requirements

### Common Components (Both Intersections)
| Component | Quantity | Description |
|-----------|----------|-------------|
| ESP32 Dev Board | 2 | WiFi + Bluetooth microcontroller |
| TM1637 Display | 8 | 4-digit 7-segment displays |
| Traffic Light LEDs | 8 sets | RGB LEDs for each road |
| Resistors (220Ω) | 24 | For current limiting |
| Jumper Wires | - | For connections |
| Breadboard | 2 | For prototyping |

### ESP32-2 Only (Intersection B)
| Component | Quantity | Description |
|-----------|----------|-------------|
| DHT11 Sensor | 1 | Temperature and humidity sensor |
| Rain Sensor Module | 1 | Analog + Digital output |

### Wiring Diagram

#### ESP32-1 (Intersection A) - No Sensors
