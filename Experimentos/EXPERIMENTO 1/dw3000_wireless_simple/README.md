# UWB Tag Inalu00e1mbrico con Interfaz Web (Versiu00f3n Simple)

Esta es una versiu00f3n optimizada y simplificada del firmware UWB Tag que utiliza las bibliotecas estu00e1ndar de ESP32 para mayor estabilidad. Implementa un sistema de posicionamiento en interiores basado en UWB (Ultra-Wideband) utilizando un ESP32 como controlador principal y mu00f3dulos DW3000 para el ranging UWB.

## Por quu00e9 esta versiu00f3n

Esta versiu00f3n resuelve el problema de estabilidad relacionado con las bibliotecas asincru00f3nicas:

```
assert failed: tcp_alloc /IDF/components/lwip/lwip/src/core/tcp.c:1851 (Required to lock TCPIP core functionality!)
```

Utilizando las bibliotecas estu00e1ndar de ESP32 (`WebServer` en lugar de `ESPAsyncWebServer`), este firmware es mucho mu00e1s estable y tiene menos probabilidad de causar reinicios o bloqueos.

## Caracteru00edsticas

- **Ranging UWB de doble lado** para mediciones de distancia precisas
- **Interfaz Web** accesible desde cualquier dispositivo con navegador
- **Monitoreo de bateru00eda** con gru00e1ficos y alertas
- **Visualizaciu00f3n de posicionamiento** en tiempo real
- **Comunicaciu00f3n WiFi** en modo AP (crea su propia red) o STA (se conecta a una red existente)
- **Bajo consumo** con modos de ahorro de energu00eda automu00e1ticos

## Requisitos de Hardware

- ESP32 (cualquier variante con WiFi)
- Mu00f3dulo DW3000 para UWB
- Bateru00eda LiPo/18650 (opcional, para funcionamiento portu00e1til)
- Anclajes UWB (mu00ednimo 3 para posicionamiento 2D)

## Configuraciu00f3n

### Conexiones Hardware

1. Conecta el DW3000 al ESP32 segu00fan la siguiente configuraciu00f3n:
   - DW3000 MOSI -> ESP32 GPIO23
   - DW3000 MISO -> ESP32 GPIO19
   - DW3000 SCK -> ESP32 GPIO18
   - DW3000 CS -> ESP32 GPIO5
   - DW3000 RST -> ESP32 GPIO27
   - DW3000 IRQ -> ESP32 GPIO34

2. Para monitoreo de bateru00eda (opcional):
   - Conecta el pin de la bateru00eda al GPIO34 a travu00e9s de un divisor de tensiu00f3n

### Configuraciu00f3n de Software

1. Instala las bibliotecas necesarias en el Arduino IDE:
   - DW3000 (para el mu00f3dulo UWB)
   - WiFi (incluida con ESP32)
   - WebServer (incluida con ESP32)

2. Configura los paru00e1metros WiFi en el archivo `dw3000_wireless_simple.ino`:
   - Para modo AP (punto de acceso): Configura `AP_SSID` y `AP_PASS`
   - Para modo STA (cliente): Configura `STA_SSID` y `STA_PASS` y cambia `USE_AP_MODE` a `false`

3. Ajusta los IDs de los anclajes en el array `ID_PONG[]` si es necesario

## Uso

### Pasos para iniciar el sistema

1. Carga el firmware en el ESP32 utilizando el Arduino IDE
2. Enciende el ESP32 y los anclajes UWB
3. Conecta tu dispositivo (smartphone, tablet, ordenador) a la red WiFi "UWB_TAG_AP" (contraseu00f1a: 12345678)
4. Abre tu navegador y visita http://192.168.4.1
5. u00a1La interfaz web deberu00eda cargarse mostrando los datos en tiempo real!

### Diferencias con la versiu00f3n anterior

- Uso de WebServer en lugar de ESPAsyncWebServer para mayor estabilidad
- WiFiUDP en lugar de AsyncUDP
- Velocidad del puerto serial reducida a 115200 bps para mejor compatibilidad
- Optimizaciones en el manejo de memoria
