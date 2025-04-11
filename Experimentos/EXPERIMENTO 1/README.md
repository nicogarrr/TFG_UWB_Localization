# Experimento 1: Sistema de Localización UWB con Tag Conectado

## Descripción

Este experimento implementa un sistema de localización en tiempo real utilizando tecnología UWB (Ultra-Wideband) con ESP32. En esta primera versión, el tag debe estar conectado al ordenador mediante USB para procesar y visualizar los datos de posicionamiento.

## Componentes del Sistema

### Hardware

- **Tag**: ESP32 con módulo UWB DW3000, conectado al ordenador por USB
- **Anchors**: 4 dispositivos ESP32 con módulos UWB DW3000 ubicados en posiciones fijas
  - Anchor 10 (A): Posición (0.0, 1.10)
  - Anchor 20 (B): Posición (0.0, 4.55)
  - Anchor 30 (C): Posición (3.45, 3.5)
  - Anchor 40 (D): Posición (3.45, 0.66)

### Software

#### Código para Anchors

Cada anchor ejecuta el firmware `dw3000_doublesided_ranging_pong_pru11_IDXX.ino` (donde XX es el ID del anchor). Este código:

- Configura el dispositivo para funcionar como un anchor fijo
- Implementa el protocolo DS-TWR (Double-Sided Two-Way Ranging)
- Responde a las solicitudes de ranging del tag
- Envía información de timestamps para el cálculo de distancias

#### Código para Tag

El tag ejecuta un firmware que:

- Inicia comunicación secuencial con cada anchor
- Implementa el protocolo DS-TWR para obtener mediciones precisas de distancia
- Envía los datos de ranging a través del puerto serie al ordenador

#### Software de Procesamiento (Python)

1. **csv_logger.py**
   - Captura datos del puerto serie conectado al tag
   - Procesa y formatea los datos recibidos
   - Guarda los datos en archivos CSV para su posterior análisis
   - Permite controlar la captura de datos mediante comandos

2. **tag_replay_4anchors.py**
   - Visualiza los datos de posicionamiento en tiempo real
   - Implementa algoritmos de trilateración para calcular la posición del tag
   - Genera mapas de calor y estadísticas de movimiento
   - Permite reproducir sesiones grabadas anteriormente

## Protocolo DS-TWR

El sistema utiliza el protocolo DS-TWR (Double-Sided Two-Way Ranging) basado en la nota de aplicación APS011 de Decawave ("SOURCES OF ERROR IN DW1000 BASED TWO-WAY RANGING (TWR) SCHEMES"). Este enfoque:

- Minimiza el error causado por el desfase de reloj entre dispositivos
- Realiza mediciones bidireccionales para mayor precisión
- Calcula distancias con precisión de centímetros

## Configuración de la Librería DW3000

La librería DW3000 requiere configuración explícita mediante llamadas a métodos:

```cpp
DW3000.setChannel(5);           // Canal 5 (frecuencia ~6.5GHz)
DW3000.setPreambleLength(64);   // Longitud de preámbulo 
DW3000.setPreambleCode(9);      // Código de preámbulo 9
DW3000.setDatarate(0);          // Tasa de datos 110kbps
DW3000.writeSysConfig();        // Aplicar la configuración
```

## Uso del Sistema

### Configuración Inicial

1. Programar cada anchor con su firmware específico (ID10, ID20, ID30, ID40)
2. Colocar los anchors en las posiciones definidas
3. Programar el tag con su firmware
4. Conectar el tag al ordenador mediante USB

### Captura de Datos

1. Ejecutar el script `csv_logger.py`:
   ```
   python csv_logger.py
   ```

2. Comandos disponibles:
   - `start`: Iniciar grabación de datos
   - `stop`: Detener grabación
   - `status`: Ver estado actual
   - `debug`: Activar/desactivar modo depuración
   - `exit`: Salir del programa

### Visualización

1. Ejecutar el script `tag_replay_4anchors.py`:
   ```
   python tag_replay_4anchors.py
   ```

2. El script permite:
   - Visualizar la posición del tag en tiempo real
   - Generar mapas de calor
   - Calcular estadísticas de movimiento
   - Reproducir sesiones grabadas anteriormente

## Limitaciones

- El tag debe estar conectado al ordenador mediante USB
- No es adecuado para aplicaciones donde se requiere movilidad completa
- Procesamiento centralizado en el ordenador

## Próximos Pasos

Este experimento sirve como base para el Experimento 2, donde se implementa un sistema más avanzado utilizando MQTT para comunicación inalámbrica, permitiendo que el tag sea completamente móvil y autónomo.
