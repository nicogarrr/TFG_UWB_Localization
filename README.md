# UWB Indoor Localization for Futsal (B.Sc. Thesis)

> Language: **English** | [Español](README.es.md)

Final degree project (TFG) in Data Science & Engineering, University of Oviedo. Graded 10/10.

**What it is:** a real-time indoor positioning system built on UWB (Ultra-Wideband) with ESP32, designed for futsal and other sports in indoor spaces where GPS does not work.

**Problem it solves:** GPS is unusable indoors. UWB measures distances with centimetre-level accuracy, which makes it suitable for tracking players inside a court.

## How it works

```text
Anchors (ESP32 + DW3000), fixed at known positions
        |  DS-TWR distance measurements
        v
Tag (ESP32 + DW3000), carried by the player
        |  MQTT (wireless)
        v
Laptop running the Python pipeline: positioning, visualisation, stats
```

The system uses the DS-TWR (Double-Sided Two-Way Ranging) protocol to compute accurate distances between the tag and the anchors. This protocol compensates clock-drift errors by measuring in both directions.

## Repository structure

- **Experimentos**: the different experiments built during the project.
  - **Experimento 1**: initial system where the tag must stay connected to the computer.
  - **ESP32 MQTT Experimento 2**: improved system where the anchors send data over MQTT, so the tag is fully mobile.

- **Documentación**: reports, diagrams and technical documentation.

- **Librerias**: libraries required by the system.

- **Utilidades**: helper scripts and tools.

## Test area

The experiments use a controlled area 3.45 m wide and 5.1 m long, with anchors placed at these coordinates:

- Anchor 10 (A): (0.0, 1.10)
- Anchor 20 (B): (0.0, 4.55)
- Anchor 30 (C): (3.45, 3.5)
- Anchor 40 (D): (3.45, 0.66)

## Tech stack

- **Hardware**:
  - ESP32 (tags and anchors)
  - DW3000 UWB modules

- **Software**:
  - Arduino IDE
  - DW3000 library for ESP32
  - MQTT for wireless communication
  - Python for data processing and visualisation

## DW3000 library configuration

The DW3000 library requires explicit configuration through method calls:

```cpp
DW3000.setChannel(5);           // Channel 5 (~6.5 GHz)
DW3000.setPreambleLength(64);   // Preamble length
DW3000.setPreambleCode(9);      // Preamble code 9
DW3000.setDatarate(0);          // Data rate 110 kbps
DW3000.writeSysConfig();        // Apply configuration
```

## Project evolution

1. **Experiment 1**: initial system where the tag must be connected to the computer to process and visualise the data.

2. **Experiment 2**: improved system where the anchors send data over MQTT, making the tag fully mobile and autonomous.

Next experiments aim to improve accuracy, reduce latency and adapt the system for real futsal environments.

## Applications

- Real-time player tracking
- Sports performance analysis
- Movement and positioning statistics
- Trajectory visualisation and heat maps

## Follow-up work

This thesis is continued by **Lazarillo** (master's thesis, in progress), which fuses UWB ranging with WiFi CSI sensing on the same ESP32 setup for smart-home zoning, elderly-care fall detection and indoor sports tracking.
