# Experimento 2: Sistema de Localización UWB con MQTT

Este experimento implementa un sistema de localización en tiempo real utilizando tecnología UWB (Ultra-Wideband) con ESP32 y comunicación MQTT.

## Descripción

En este experimento, el tag UWB se comunica secuencialmente con 4 anchors fijos. Cada anchor calcula la distancia al tag utilizando el protocolo DS-TWR (Double-Sided Two-Way Ranging) y envía esta información a través de MQTT a un servidor central para su procesamiento.

A diferencia del Experimento 1, donde el tag debía estar conectado al ordenador, en este experimento los anchors son los que envían los datos a través de WiFi y MQTT, permitiendo que el tag sea completamente móvil y autónomo.

## Componentes del Sistema

### Tag (tag1.ino)
- ESP32 con módulo UWB DW3000
- Funciona de forma autónoma sin conexión a ordenador
- Cicla secuencialmente entre los 4 anchors
- Implementa el protocolo DS-TWR modificado

### Anchors (anchor_XX_Y.ino)
- ESP32 con módulo UWB DW3000 y conexión WiFi
- Cada anchor tiene una posición fija conocida:
  - Anchor A (ID: 10): (0.0, 1.10)
  - Anchor B (ID: 20): (0.0, 4.55)
  - Anchor C (ID: 30): (3.45, 3.5)
  - Anchor D (ID: 40): (3.45, 0.66)
- Calculan la distancia al tag mediante DS-TWR
- Envían los datos vía MQTT al servidor central

### Servidor MQTT
- Recibe los datos de distancia de los anchors
- Procesa la información para calcular la posición del tag
- Visualiza la posición en tiempo real

## Protocolo DS-TWR

El sistema utiliza el protocolo DS-TWR (Double-Sided Two-Way Ranging) para calcular distancias precisas entre el tag y los anchors. Este protocolo compensa los errores de deriva del reloj al realizar mediciones en ambas direcciones.

## Formato de Datos MQTT

Los anchors publican datos en formato JSON con la siguiente estructura:

```json
{
  "ts": 12345678,         // Timestamp en milisegundos
  "tag": 1,              // ID del tag
  "anchor_id": 10,       // ID numérico del anchor
  "anchor_label": "A",   // Etiqueta del anchor
  "dist": 2.345,         // Distancia calculada en metros
  "anchor_x": 0.0,       // Coordenada X del anchor
  "anchor_y": 1.10,      // Coordenada Y del anchor
  "anchor_z": 1.5        // Coordenada Z del anchor (altura)
}
```

## Configuración

Antes de utilizar el sistema, es necesario configurar:

1. En los anchors:
   - Credenciales WiFi
   - Dirección IP del servidor MQTT
   - ID y coordenadas específicas de cada anchor

2. En el tag:
   - Lista de IDs de los anchors con los que se comunicará

## Aplicaciones

Este sistema puede utilizarse para localización en tiempo real en espacios interiores como campos de fútbol sala, donde los sistemas GPS tradicionales no funcionan correctamente.
