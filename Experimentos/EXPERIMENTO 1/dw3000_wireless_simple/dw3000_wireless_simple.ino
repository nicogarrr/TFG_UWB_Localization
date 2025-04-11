#include "DW3000.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>

// ===== CONFIGURACIÓN WiFi =====
#define USE_AP_MODE true  // true = punto de acceso, false = conectar a red existente

// Configuraciones para modo AP (punto de acceso)
#define AP_SSID "UWB_TAG_AP"    // Nombre del punto de acceso
#define AP_PASS "12345678"      // Contraseña (mínimo 8 caracteres)

// Configuraciones para modo STA (conectar a red existente)
#define STA_SSID "TuRedWiFi"    // SSID de tu red WiFi
#define STA_PASS "TuPassword"   // Contraseña de tu red WiFi

// Configuraciones del servidor y UDP
#define HTTP_PORT 80             // Puerto para el servidor web
#define UDP_PORT 5555            // Puerto para transmisión UDP
WebServer server(HTTP_PORT);
WiFiUDP udp;
IPAddress broadcastIP(255, 255, 255, 255);

// ===== CONFIGURACIÓN PARA GRABACIÓN CSV =====
bool csvRecording = false;           // Indica si está grabando datos CSV
unsigned long csvInterval = 2000;    // Intervalo entre registros (ms)
unsigned long lastCsvTime = 0;       // Último registro
String startTimestamp = "";          // Timestamp de inicio de la grabación

// ===== CONFIGURACIÓN BATERÍA =====
#define BATT_PIN 34         // Pin analógico para leer voltaje de batería
#define VOLTAGE_DIVIDER 2.0  // Factor del divisor de tensión (si aplica)
float batteryVoltage = 0.0;
float batteryPercentage = 100.0;
unsigned long lastBatteryCheck = 0;
const long BATTERY_CHECK_INTERVAL = 60000;  // Revisar cada minuto

// Variables para la batería
const int BATTERY_PIN = 35;  // Pin analógico para leer la batería
const float MAX_BATTERY_VOLTAGE = 4.2;  // Voltaje máximo de la batería LiPo
const float MIN_BATTERY_VOLTAGE = 3.3;  // Voltaje mínimo de la batería LiPo

// ===== CONFIGURACIÓN DE RANGING =====
#define ROUND_DELAY 100            // Retardo en ms entre solicitudes PING (reducido de 500ms a 100ms)
static int frame_buffer = 0;        // Variable para almacenar el mensaje transmitido
static int rx_status;               // Estado actual de la operación de recepción
static int tx_status;               // Estado actual de la operación de transmisión

// Estados del ranging
// 0 - estado por defecto; inicia ranging
// 1 - ranging enviado; esperando respuesta
// 2 - respuesta recibida; enviando segundo ranging
// 3 - segundo ranging enviado; esperando respuesta final
// 4 - respuesta final recibida
static int curr_stage = 0;

static int t_roundA = 0;
static int t_replyA = 0;

static long long rx = 0;
static long long tx = 0;

static int clock_offset = 0;
static int ranging_time = 0;
static float distance = 0;

// Configuraciones para mediciones y filtrado
#define NUM_MEASUREMENTS 5         // Número de mediciones para el buffer
#define NUM_ANCHORS 4              // Número de anclajes
int ID_PONG[NUM_ANCHORS] = {10, 20, 30, 40};
float distance_buffer[NUM_ANCHORS][NUM_MEASUREMENTS] = { {0} };
int buffer_index[NUM_ANCHORS] = {0};
float anchor_distance[NUM_ANCHORS] = {0};
float anchor_avg[NUM_ANCHORS] = {0};
float pot_sig[NUM_ANCHORS] = {0};
static int fin_de_com = 0;

// Variables para timeout
unsigned long timeoutStart = 0;
bool waitingForResponse = false;
const unsigned long RESPONSE_TIMEOUT = 5000;  // 5 segundos de timeout

// Variables para gestor de estados
unsigned long lastUpdate = 0;
unsigned long updateInterval = 50;  // Frecuencia de actualizaciones en ms (reducido de 100ms a 50ms)

// Variables para modo de bajo consumo
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = 300000;  // 5 minutos sin actividad
bool lowPowerMode = false;

/* --- Variables para Filtro de Kalman --- */
// Para distancias
float kalman_dist[NUM_ANCHORS][2] = { {0} };  // [0] = estado, [1] = covarianza
float kalman_dist_q = 0.01;  // Ruido del proceso
float kalman_dist_r = 0.1;   // Ruido de la medición

// Para posición
float kalman_x = 0.0;        // Estado estimado X
float kalman_y = 0.0;        // Estado estimado Y
float kalman_p_x = 1.0;      // Covarianza X
float kalman_p_y = 1.0;      // Covarianza Y
float kalman_q = 0.01;       // Ruido del proceso para posición
float kalman_r = 0.1;        // Ruido de la medición para posición

// Variables para la posición del tag
float tagPositionX = 0.0;
float tagPositionY = 0.0;

// Estructura para definir zonas de interés
#define NUM_ZONES 3
struct Zone {
  float x;
  float y;
  float radius;
  bool tagInside;
  unsigned long entryTime;
  unsigned long minStayTime;
  bool stayTimeReached;
};

// Definición de zonas
Zone zones[NUM_ZONES] = {
  {0.5, 0.5, 0.3, false, 0, 3000, false},   // Zona 0: cerca de (0.5, 0.5) con radio 0.3m
  {2.5, 2.5, 0.4, false, 0, 5000, false},   // Zona 1: cerca de (2.5, 2.5) con radio 0.4m
  {1.5, 4.0, 0.5, false, 0, 10000, false}   // Zona 2: cerca de (1.5, 4.0) con radio 0.5m
};

// HTML para la página web integrada (versión simplificada)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Monitor de Tag UWB</title>
  <style>
    body { font-family: Arial; margin: 0; padding: 0; background: #f0f0f0; }
    .container { max-width: 800px; margin: 0 auto; padding: 20px; }
    h1 { color: #333; }
    .card { background: white; border-radius: 8px; padding: 15px; margin-bottom: 15px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    .anchor { display: flex; justify-content: space-between; margin-bottom: 10px; }
    .battery { display: flex; align-items: center; margin-bottom: 20px; }
    .battery-icon { width: 30px; height: 15px; border: 2px solid #333; border-radius: 3px; position: relative; margin-right: 10px; }
    .battery-icon:after { content: ''; width: 3px; height: 8px; background: #333; position: absolute; right: -5px; top: 3px; border-radius: 0 2px 2px 0; }
    .battery-level { height: 100%; background: #4CAF50; border-radius: 1px; }
    button { background: #4CAF50; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; margin-right: 8px; }
    button.record { background: #f44336; }
    button:disabled { background: #ccc; cursor: not-allowed; }
    .status { color: #666; font-style: italic; }
    #visualization { height: 300px; position: relative; border: 1px solid #ccc; background: #fafafa; }
    .anchor-point { position: absolute; width: 20px; height: 20px; border-radius: 50%; background: blue; color: white; display: flex; justify-content: center; align-items: center; transform: translate(-50%, -50%); }
    .distance-circle { position: absolute; border-radius: 50%; border: 1px dashed rgba(0,0,0,0.3); transform: translate(-50%, -50%); }
    .tag-point { 
      position: absolute; 
      width: 14px; 
      height: 14px; 
      border-radius: 50%; 
      background: red; 
      transform: translate(-50%, -50%); 
      box-shadow: 0 0 15px rgba(255,0,0,0.7); 
      transition: all 0.1s linear;
      animation: pulse 1.5s infinite;
    }
    .button-group { display: flex; margin-top: 10px; }
    @keyframes pulse {
      0% { box-shadow: 0 0 0 0 rgba(255,0,0,0.7); }
      70% { box-shadow: 0 0 0 10px rgba(255,0,0,0); }
      100% { box-shadow: 0 0 0 0 rgba(255,0,0,0); }
    }
    .recording-indicator {
      display: inline-block;
      width: 12px;
      height: 12px;
      background-color: #f44336;
      border-radius: 50%;
      margin-right: 5px;
      animation: blink 1s infinite;
    }
    @keyframes blink {
      0% { opacity: 1; }
      50% { opacity: 0.3; }
      100% { opacity: 1; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Monitor de Tag UWB</h1>
    
    <div class="card">
      <div class="battery">
        <div class="battery-icon">
          <div class="battery-level" id="battery-level" style="width: 50%"></div>
        </div>
        <span id="battery-percentage">50%</span>
      </div>
      <p class="status" id="status">Esperando datos...</p>
      
      <div class="card">
        <h3>Grabación de datos</h3>
        <div id="recording-status">Estado: No grabando</div>
        <div class="button-group">
          <button id="start-recording" onclick="startRecording()">Iniciar grabación</button>
          <button id="stop-recording" onclick="stopRecording()" disabled>Detener grabación</button>
        </div>
      </div>
    </div>
    
    <div class="card">
      <h2>Anclajes</h2>
      <div id="anchors-container"></div>
    </div>
    
    <div class="card">
      <h2>Visualización</h2>
      <div id="visualization"></div>
      <div style="margin-top: 10px;">
        <p>Posición estimada: <span id="tag-position">Calculando...</span></p>
      </div>
    </div>
    
    <button onclick="requestUpdate()">Actualizar datos</button>
  </div>

  <script>
    let lastUpdate = Date.now();
    let anchors = [];
    let tagPosition = { x: 150, y: 150 };
    let isRecording = false;
    
    // Gestión de grabación CSV
    function startRecording() {
      fetch('/start_csv')
        .then(response => response.text())
        .then(data => {
          console.log(data);
          isRecording = true;
          updateRecordingUI();
        })
        .catch(error => {
          console.error('Error:', error);
          alert('Error al iniciar la grabación');
        });
    }
    
    function stopRecording() {
      fetch('/stop_csv')
        .then(response => response.text())
        .then(data => {
          console.log(data);
          isRecording = false;
          updateRecordingUI();
        })
        .catch(error => {
          console.error('Error:', error);
          alert('Error al detener la grabación');
        });
    }
    
    function updateRecordingUI() {
      const statusElement = document.getElementById('recording-status');
      const startButton = document.getElementById('start-recording');
      const stopButton = document.getElementById('stop-recording');
      
      if (isRecording) {
        statusElement.innerHTML = '<span class="recording-indicator"></span> Estado: Grabando datos...';
        startButton.disabled = true;
        stopButton.disabled = false;
      } else {
        statusElement.textContent = 'Estado: No grabando';
        startButton.disabled = false;
        stopButton.disabled = true;
      }
    }
    
    // Obtener datos del ESP32
    function fetchData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          updateUI(data);
          lastUpdate = Date.now();
        })
        .catch(error => {
          console.error('Error:', error);
          document.getElementById('status').textContent = 'Error de conexión';
        });
    }
    
    // Actualizar la interfaz con los datos recibidos
    function updateUI(data) {
      // Actualizar batería
      const batteryLevel = data.battery;
      document.getElementById('battery-level').style.width = batteryLevel + '%';
      document.getElementById('battery-percentage').textContent = batteryLevel + '%';
      
      // Actualizar estado de grabación
      const recordingStatus = data.recording;
      if (recordingStatus) {
        document.getElementById('recording-status').innerHTML = '<span class="recording-indicator"></span> Estado: Grabando datos...';
        document.getElementById('start-recording').disabled = true;
        document.getElementById('stop-recording').disabled = false;
      } else {
        document.getElementById('recording-status').textContent = 'Estado: No grabando';
        document.getElementById('start-recording').disabled = false;
        document.getElementById('stop-recording').disabled = true;
      }
      
      // Actualizar anclajes
      anchors = data.anchors;
      
      // Depuración - mostrar distancias en consola
      console.log("Distancias recibidas (cm):", anchors.map(a => a.avg));
      
      const anchorsContainer = document.getElementById('anchors-container');
      anchorsContainer.innerHTML = '';
      
      anchors.forEach(anchor => {
        const anchorDiv = document.createElement('div');
        anchorDiv.className = 'anchor';
        anchorDiv.innerHTML = `
          <div>
            <strong>Anclaje ${anchor.id}</strong>
            <p>Distancia: ${anchor.dist.toFixed(1)} cm</p>
            <p>Promedio: ${anchor.avg.toFixed(1)} cm</p>
          </div>
          <div>
            <p>Señal: ${anchor.rssi.toFixed(1)} dBm</p>
          </div>
        `;
        anchorsContainer.appendChild(anchorDiv);
      });
      
      // Calcular posición del tag por trilateración
      if (anchors.length >= 4) {
        calculateTagPosition();
      }
      
      // Actualizar visualización
      renderVisualization();
      
      // Actualizar estado
      document.getElementById('status').textContent = 'Última actualización: ' + new Date().toLocaleTimeString();
    }
    
    // Cálculo de posición por trilateración
    function calculateTagPosition() {
        // Configuración del espacio físico (rectángulo de 3.45m x 5.1m)
        const areaWidth = 3.45;  // metros
        const areaHeight = 5.1;  // metros
        const scale = 80;        // 1m = 80px (reducido para que quepa mejor)
        const margin = 20;       // margen en píxeles
      
        // Ancho y alto del área de visualización en píxeles
        const vizWidth = areaWidth * scale + 2 * margin;
        const vizHeight = areaHeight * scale + 2 * margin;
      
        // Obtener distancias en metros
        const distances = anchors.map(a => a.avg / 100); // convertir cm a metros
      
        // Posiciones de los anchors (en metros)
        const anchorsPos = [
          { x: 0.0, y: 1.10 },    // Anclaje 10 (0, 1.10)
          { x: 0.0, y: 4.55 },    // Anclaje 20 (0, 4.55)
          { x: 3.45, y: 3.5 },    // Anclaje 30 (3.45, 3.5)
          { x: 3.45, y: 0.66 }    // Anclaje 40 (3.45, 0.66)
        ];
      
        const r1 = distances[0];
        const r2 = distances[1];
        const r3 = distances[2];
        const r4 = distances[3];
      
        // Si alguna distancia no es válida, no podemos calcular
        if (r1 <= 0 || r2 <= 0 || r3 <= 0 || r4 <= 0 || 
            isNaN(r1) || isNaN(r2) || isNaN(r3) || isNaN(r4)) {
          document.getElementById('tag-position').textContent = "Datos insuficientes";
          return;
        }
      
        // Verificar que las distancias son razonables (menores que la diagonal del espacio)
        const maxDistance = Math.sqrt(3.45*3.45 + 5.1*5.1) + 0.5; // Diagonal + margen
        if (r1 > maxDistance || r2 > maxDistance || r3 > maxDistance || r4 > maxDistance) {
          console.warn("Distancias demasiado grandes:", r1, r2, r3, r4);
          document.getElementById('tag-position').textContent = "Distancias fuera de rango";
          return;
        }
      
        try {
          // Depuración - mostrar distancias y posiciones de anchors
          console.log("Distancias para trilateración (m):", r1, r2, r3, r4);
          console.log("Posiciones de anchors:", anchorsPos);
      
          // Algoritmo de multilateración para 4 anclas
          // Usamos el método de mínimos cuadrados para resolver el sistema sobredeterminado
      
          // Preparamos el sistema de ecuaciones
          // Para cada par de anclas i,j formamos una ecuación lineal
          // Usaremos las primeras 3 anclas para formar 2 ecuaciones
      
          // Primera ecuación: Anclas 1 y 2
          const A1 = 2 * (anchorsPos[1].x - anchorsPos[0].x);
          const B1 = 2 * (anchorsPos[1].y - anchorsPos[0].y);
          const C1 = r1*r1 - r2*r2 - anchorsPos[0].x*anchorsPos[0].x + anchorsPos[1].x*anchorsPos[1].x - anchorsPos[0].y*anchorsPos[0].y + anchorsPos[1].y*anchorsPos[1].y;
      
          // Segunda ecuación: Anclas 2 y 3
          const A2 = 2 * (anchorsPos[2].x - anchorsPos[1].x);
          const B2 = 2 * (anchorsPos[2].y - anchorsPos[1].y);
          const C2 = r2*r2 - r3*r3 - anchorsPos[1].x*anchorsPos[1].x + anchorsPos[2].x*anchorsPos[2].x - anchorsPos[1].y*anchorsPos[1].y + anchorsPos[2].y*anchorsPos[2].y;
      
          // Tercera ecuación: Anclas 3 y 4
          const A3 = 2 * (anchorsPos[3].x - anchorsPos[2].x);
          const B3 = 2 * (anchorsPos[3].y - anchorsPos[2].y);
          const C3 = r3*r3 - r4*r4 - anchorsPos[2].x*anchorsPos[2].x + anchorsPos[3].x*anchorsPos[3].x - anchorsPos[2].y*anchorsPos[2].y + anchorsPos[3].y*anchorsPos[3].y;
      
          // Cuarta ecuación: Anclas 4 y 1
          const A4 = 2 * (anchorsPos[0].x - anchorsPos[3].x);
          const B4 = 2 * (anchorsPos[0].y - anchorsPos[3].y);
          const C4 = r4*r4 - r1*r1 - anchorsPos[3].x*anchorsPos[3].x + anchorsPos[0].x*anchorsPos[0].x - anchorsPos[3].y*anchorsPos[3].y + anchorsPos[0].y*anchorsPos[0].y;
      
          console.log("Coeficientes:", {A1, B1, C1, A2, B2, C2, A3, B3, C3, A4, B4, C4});
      
          // Resolver el sistema sobredeterminado usando mínimos cuadrados
          // Formamos las matrices para resolver [A]x = b
          // Donde A = [A1 B1; A2 B2; A3 B3; A4 B4], x = [x; y], b = [C1; C2; C3; C4]
      
          // Calculamos A^T * A
          const ATA11 = A1*A1 + A2*A2 + A3*A3 + A4*A4;
          const ATA12 = A1*B1 + A2*B2 + A3*B3 + A4*B4;
          const ATA21 = ATA12;  // Es simétrica
          const ATA22 = B1*B1 + B2*B2 + B3*B3 + B4*B4;
      
          // Calculamos A^T * b
          const ATb1 = A1*C1 + A2*C2 + A3*C3 + A4*C4;
          const ATb2 = B1*C1 + B2*C2 + B3*C3 + B4*C4;
      
          // Calculamos el determinante de A^T * A
          const detATA = ATA11 * ATA22 - ATA12 * ATA21;
      
          if (Math.abs(detATA) < 0.001) {
            throw new Error("Matriz singular, no se puede resolver");
          }
      
          // Calculamos la inversa de A^T * A
          const invATA11 = ATA22 / detATA;
          const invATA12 = -ATA12 / detATA;
          const invATA21 = -ATA21 / detATA;
          const invATA22 = ATA11 / detATA;
      
          // Finalmente, calculamos x = (A^T * A)^(-1) * A^T * b
          const x = invATA11 * ATb1 + invATA12 * ATb2;
          const y = invATA21 * ATb1 + invATA22 * ATb2;
      
          console.log("Posición calculada (m):", x, y);
      
          // Aplicar límites razonables
          const boundedX = Math.max(0, Math.min(areaWidth, x));
          const boundedY = Math.max(0, Math.min(areaHeight, y));
      
          // Convertir a coordenadas de visualización
          const pixelX = margin + boundedX * scale;
          const pixelY = vizHeight - margin - boundedY * scale;
      
          // Suavizar el movimiento: mezclamos la nueva posición con la anterior
          // Reducimos el factor de suavizado para movimiento más rápido
          tagPosition.x = tagPosition.x * 0.1 + pixelX * 0.9;
          tagPosition.y = tagPosition.y * 0.1 + pixelY * 0.9;
      
          // Actualizar texto de posición
          document.getElementById('tag-position').textContent = 
            `X: ${boundedX.toFixed(2)}m, Y: ${boundedY.toFixed(2)}m`;
        } catch (e) {
          console.error('Error en cálculo de posición:', e);
          document.getElementById('tag-position').textContent = "Error de cálculo";
        }
      }
    
    // Renderizar visualización de posicionamiento
    function renderVisualization() {
      const viz = document.getElementById('visualization');
      viz.innerHTML = '';
      
      if (anchors.length < 4) return;
      
      // Configuración del espacio físico (rectángulo de 3.45m x 5.1m)
      const areaWidth = 3.45;  // metros
      const areaHeight = 5.1;  // metros
      const scale = 80;        // 1m = 80px (reducido para que quepa mejor)
      const margin = 20;       // margen en píxeles
      
      // Ancho y alto del área de visualización en píxeles
      const vizWidth = areaWidth * scale + 2 * margin;
      const vizHeight = areaHeight * scale + 2 * margin;
      
      // Ajustar el tamaño del contenedor de visualización
      viz.style.width = vizWidth + 'px';
      viz.style.height = vizHeight + 'px';
      
      // Posiciones de los anclajes (en píxeles)
      const positions = [
        { x: margin + 0 * scale, y: vizHeight - margin - 1.10 * scale },         // Anclaje 10 (0, 1.10)
        { x: margin + 0 * scale, y: vizHeight - margin - 4.55 * scale },         // Anclaje 20 (0, 4.55)
        { x: margin + 3.45 * scale, y: vizHeight - margin - 3.5 * scale },       // Anclaje 30 (3.45, 3.5)
        { x: margin + 3.45 * scale, y: vizHeight - margin - 0.66 * scale }       // Anclaje 40 (3.45, 0.66)
      ];
      
      // Dibujar un borde para el área experimental
      const border = document.createElement('div');
      border.style.position = 'absolute';
      border.style.left = margin + 'px';
      border.style.top = margin + 'px';
      border.style.width = (areaWidth * scale) + 'px';
      border.style.height = (areaHeight * scale) + 'px';
      border.style.border = '2px solid #333';
      border.style.boxSizing = 'border-box';
      viz.appendChild(border);
      
      // Dibujar anclajes y círculos de distancia
      anchors.forEach((anchor, i) => {
        // Punto del anclaje
        const dot = document.createElement('div');
        dot.className = 'anchor-point';
        dot.textContent = (i+1);
        dot.title = `Anclaje ${anchor.id}`;
        dot.style.left = positions[i].x + 'px';
        dot.style.top = positions[i].y + 'px';
        viz.appendChild(dot);
        
        // Círculo de distancia
        const circle = document.createElement('div');
        circle.className = 'distance-circle';
        circle.style.left = positions[i].x + 'px';
        circle.style.top = positions[i].y + 'px';
        circle.style.width = (anchor.avg / 100) * scale * 2 + 'px';
        circle.style.height = (anchor.avg / 100) * scale * 2 + 'px';
        viz.appendChild(circle);
      });
      
      // Punto del tag (posición calculada por trilateración)
      const tagPoint = document.createElement('div');
      tagPoint.className = 'tag-point';
      tagPoint.style.left = tagPosition.x + 'px';
      tagPoint.style.top = tagPosition.y + 'px';
      viz.appendChild(tagPoint);
    }
    
    // Solicitar actualización de datos
    function requestUpdate() {
      fetchData();
    }
    
    // Actualizar datos periódicamente (cada 0.1 segundos para mayor fluidez)
    setInterval(fetchData, 100);
    
    // Cargar datos iniciales
    fetchData();
  </script>
</body>
</html>
)rawliteral";

// ===== FUNCIONES =====

// Comprueba el nivel de la batería
void checkBattery() {
  // Leer el valor analógico del pin de la batería
  int rawValue = analogRead(BATTERY_PIN);
  
  // Convertir a voltaje (ajustar divisor de voltaje si es necesario)
  // ESP32 ADC es de 12 bits (0-4095) y el voltaje de referencia es 3.3V
  float voltage = rawValue * (3.3 / 4095.0) * 2.0;  // Multiplicar por 2 si hay un divisor de voltaje
  
  // Filtrar el voltaje para evitar fluctuaciones
  batteryVoltage = batteryVoltage * 0.9 + voltage * 0.1;
  
  // Calcular porcentaje
  int percentage = map(batteryVoltage * 100, MIN_BATTERY_VOLTAGE * 100, MAX_BATTERY_VOLTAGE * 100, 0, 100);
  percentage = constrain(percentage, 0, 100);
  
  // Actualizar el porcentaje con un filtro simple
  batteryPercentage = batteryPercentage * 0.9 + percentage * 0.1;
  
  // Mostrar información de la batería
  Serial.print("Batería: ");
  Serial.print(batteryVoltage);
  Serial.print("V (");
  Serial.print(batteryPercentage);
  Serial.println("%)");
  
  // Comprobar si la batería está baja
  if (batteryPercentage < 15) {
    Serial.println("¡ADVERTENCIA! Batería baja");
    
    // Si la batería está muy baja, entrar en modo de bajo consumo
    if (batteryPercentage < 5) {
      Serial.println("Batería crítica. Entrando en modo de bajo consumo...");
      lowPowerMode = true;
    }
  }
}

// Filtro de Kalman para distancias
float kalmanFilterDistance(float measurement, int anchor_id) {
  kalman_dist[anchor_id][1] = kalman_dist[anchor_id][1] + kalman_dist_q;
  float k = kalman_dist[anchor_id][1] / (kalman_dist[anchor_id][1] + kalman_dist_r);
  kalman_dist[anchor_id][0] = kalman_dist[anchor_id][0] + k * (measurement - kalman_dist[anchor_id][0]);
  kalman_dist[anchor_id][1] = (1 - k) * kalman_dist[anchor_id][1];
  return kalman_dist[anchor_id][0];
}

// Filtro de Kalman para posición 2D
void kalmanFilterPosition(float measured_x, float measured_y) {
  kalman_p_x = kalman_p_x + kalman_q;
  kalman_p_y = kalman_p_y + kalman_q;
  
  float k_x = kalman_p_x / (kalman_p_x + kalman_r);
  float k_y = kalman_p_y / (kalman_p_y + kalman_r);
  
  kalman_x = kalman_x + k_x * (measured_x - kalman_x);
  kalman_y = kalman_y + k_y * (measured_y - kalman_y);
  
  kalman_p_x = (1 - k_x) * kalman_p_x;
  kalman_p_y = (1 - k_y) * kalman_p_y;
  
  tagPositionX = kalman_x;
  tagPositionY = kalman_y;
}

// Configura la conexión WiFi
void setupWiFi() {
  if (USE_AP_MODE) {
    // Modo Access Point (crea una red WiFi)
    Serial.println("Configurando modo Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP creado. Dirección IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    // Modo Station (conectar a red WiFi existente)
    Serial.println("Conectando a red WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi conectado");
      Serial.println("Dirección IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nFallo al conectar. Trabajando sin WiFi.");
    }
  }
}

// Genera un JSON con los datos actuales
String getDataJson() {
  String json = "{";
  
  // Añadir datos de la batería
  json += "\"battery\":" + String(batteryPercentage, 1) + ",";
  
  // Añadir estado de grabación CSV
  json += "\"recording\":" + String(csvRecording ? "true" : "false") + ",";
  
  // Añadir datos de los anclajes
  json += "\"anchors\":[";
  
  for (int i = 0; i < NUM_ANCHORS; i++) {
    if (i > 0) json += ",";
    
    json += "{";
    json += "\"id\":" + String(ID_PONG[i]) + ",";
    json += "\"dist\":" + String(anchor_distance[i], 1) + ",";
    json += "\"avg\":" + String(anchor_avg[i], 1) + ",";
    json += "\"rssi\":" + String(pot_sig[i], 1);
    json += "}";
  }
  
  json += "],";
  
  // Añadir posición actual del tag
  json += "\"position\":{";
  json += "\"x\":" + String(tagPositionX, 3) + ",";
  json += "\"y\":" + String(tagPositionY, 3);
  json += "}";
  
  json += "}";
  return json;
}

// Configura el servidor web
void setupWebServer() {
  // Ruta principal - página de control
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", INDEX_HTML);
  });
  
  // Ruta para obtener datos actuales en formato JSON
  server.on("/data", HTTP_GET, []() {
    server.send(200, "application/json", getDataJson());
  });
  
  // Ruta para iniciar la grabación de CSV
  server.on("/start_csv", HTTP_GET, []() {
    startCsvRecording();
    server.send(200, "text/plain", "Grabación CSV iniciada");
  });
  
  // Ruta para detener la grabación de CSV
  server.on("/stop_csv", HTTP_GET, []() {
    stopCsvRecording();
    server.send(200, "text/plain", "Grabación CSV detenida");
  });
  
  server.begin();
  Serial.print("Servidor HTTP iniciado en puerto ");
  Serial.println(HTTP_PORT);
}

// Envía datos por UDP (broadcasting)
void broadcastUDP() {
  String json = getDataJson();
  udp.beginPacket(broadcastIP, UDP_PORT);
  udp.print(json);
  udp.endPacket();
}

// Comprueba si el tag está dentro de alguna zona definida
void checkZones() {
  bool inAnyZone = false;
  
  for (int i = 0; i < NUM_ZONES; i++) {
    float dx = tagPositionX - zones[i].x;
    float dy = tagPositionY - zones[i].y;
    float distance = sqrt(dx*dx + dy*dy);
    
    if (distance <= zones[i].radius) {
      inAnyZone = true;
      
      // Si acabamos de entrar en la zona
      if (!zones[i].tagInside) {
        zones[i].tagInside = true;
        zones[i].entryTime = millis();
        
        // Notificar entrada a zona
        Serial.print("Tag entró en zona ");
        Serial.println(i);
      }
      
      // Comprobar tiempo de permanencia
      if (millis() - zones[i].entryTime >= zones[i].minStayTime && !zones[i].stayTimeReached) {
        zones[i].stayTimeReached = true;
        
        // Notificar tiempo mínimo alcanzado
        Serial.print("Tiempo mínimo alcanzado en zona ");
        Serial.println(i);
      }
    } else {
      // Si acabamos de salir de la zona
      if (zones[i].tagInside) {
        zones[i].tagInside = false;
        zones[i].stayTimeReached = false;
        
        // Notificar salida de zona
        Serial.print("Tag salió de zona ");
        Serial.println(i);
      }
    }
  }
}

// Inicia la grabación de datos CSV
void startCsvRecording() {
  // Obtener timestamp actual
  if (!csvRecording) {
    csvRecording = true;
    lastCsvTime = 0; // Forzar una grabación inmediata
    
    // Imprimir cabecera del CSV
    Serial.println("Timestamp,Anchor_ID,Distance_cm,Average_Distance_cm,Signal_Power_dBm,Position_X,Position_Y");
    
    Serial.println("Grabación CSV iniciada");
  } else {
    Serial.println("¡Ya está grabando CSV!");
  }
}

// Detiene la grabación de datos CSV
void stopCsvRecording() {
  if (csvRecording) {
    csvRecording = false;
    Serial.println("Grabación CSV detenida");
  } else {
    Serial.println("No hay grabación en curso");
  }
}

// Registra los datos en formato CSV
void logCsvData() {
  if (!csvRecording) return;
  
  // Obtener timestamp actual formateado
  unsigned long now = millis();
  
  // Para cada anchor, registrar una línea
  for (int i = 0; i < NUM_ANCHORS; i++) {
    // Formato: timestamp,anchor_id,distance_cm,avg_distance_cm,signal_power_dbm,position_x,position_y
    Serial.print(now);
    Serial.print(",");
    Serial.print(ID_PONG[i]);
    Serial.print(",");
    Serial.print(anchor_distance[i], 2);
    Serial.print(",");
    Serial.print(anchor_avg[i], 2);
    Serial.print(",");
    Serial.print(pot_sig[i], 2);
    Serial.print(",");
    Serial.print(tagPositionX, 3);
    Serial.print(",");
    Serial.println(tagPositionY, 3);
  }
}

void setup() {
  Serial.begin(115200); // Iniciar Serial con una velocidad más estándar
  Serial.println("\n=== UWB TAG con WiFi, Batería y Servidor Web (Versión Simple) ===\n");
  
  // Configuración de pines
  pinMode(BATT_PIN, INPUT);
  
  // Inicializar conexión WiFi
  setupWiFi();
  
  // Configurar servidor web
  setupWebServer();
  
  // Inicializar DW3000
  DW3000.begin();
  DW3000.hardReset();
  delay(200);
  
  while (!DW3000.checkForIDLE()) {
    Serial.println("[ERROR] IDLE1 FAILED");
    delay(100);
  }

  DW3000.softReset();
  delay(200);

  if (!DW3000.checkForIDLE()) {
    Serial.println("[ERROR] IDLE2 FAILED");
    delay(100);
  }

  DW3000.init();
  DW3000.setupGPIO();
  Serial.println("[INFO] DW3000 inicializado correctamente.");

  DW3000.configureAsTX();
  DW3000.clearSystemStatus();
  
  // Comprobar batería inicial
  checkBattery();
  Serial.print("Batería: ");
  Serial.print(batteryVoltage);
  Serial.print("V (");
  Serial.print(batteryPercentage);
  Serial.println("%)");
  
  lastActivityTime = millis();
  lastUpdate = millis();
}

void loop() {
  // Gestionar peticiones del servidor web
  server.handleClient();
  
  unsigned long currentMillis = millis();
  
  // Comprobar nivel de batería cada cierto tiempo
  if (currentMillis - lastBatteryCheck >= BATTERY_CHECK_INTERVAL) {
    lastBatteryCheck = currentMillis;
    checkBattery();
    Serial.print("Batería: ");
    Serial.print(batteryVoltage);
    Serial.print("V (");
    Serial.print(batteryPercentage);
    Serial.println("%)");
  }
  
  // Comprobar si debemos entrar en modo de ahorro de energía
  if (!lowPowerMode && (currentMillis - lastActivityTime >= SLEEP_TIMEOUT)) {
    Serial.println("Entrando en modo de bajo consumo...");
    lowPowerMode = true;
    updateInterval = 1000;  // Reducir frecuencia a 1 seg en modo ahorro
  }
  
  // Realizar mediciones solo si es tiempo de actualizar
  if (currentMillis - lastUpdate >= updateInterval) {
    lastUpdate = currentMillis;
    lastActivityTime = currentMillis;  // Resetear timer de actividad
    
    // Realizar mediciones para cada anclaje
    for (int ii = 0; ii < NUM_ANCHORS; ii++) {
      DW3000.setDestinationID(ID_PONG[ii]);
      fin_de_com = 0;
      
      while (fin_de_com == 0) {
        // Comprobar timeout
        if (waitingForResponse && ((millis() - timeoutStart) >= RESPONSE_TIMEOUT)) {
          Serial.print("Timeout para ancla ID: ");
          Serial.println(ID_PONG[ii]);
          anchor_distance[ii] = 0;  // Marcar como no disponible
          curr_stage = 0;
          ranging_time = 0;
          waitingForResponse = false;
          fin_de_com = 1;
          break;
        }
        
        switch (curr_stage) {
          case 0:  // Iniciar ranging
            t_roundA = 0;
            t_replyA = 0;
            DW3000.ds_sendFrame(1);
            tx = DW3000.readTXTimestamp();
            curr_stage = 1;
            timeoutStart = millis();
            waitingForResponse = true;
            break;
            
          case 1:  // Esperar primera respuesta
            if (rx_status = DW3000.receivedFrameSucc()) {
              DW3000.clearSystemStatus();
              if ((rx_status == 1) && (DW3000.getDestinationID() == ID_PONG[ii])) {
                if (DW3000.ds_isErrorFrame()) {
                  Serial.println("[WARNING] Error frame detected! Reverting to stage 0.");
                  curr_stage = 0;
                  waitingForResponse = false;
                } else if ((DW3000.getDestinationID() != ID_PONG[ii])) {
                  break;
                } else if (DW3000.ds_getStage() != 2) {
                  DW3000.ds_sendErrorFrame();
                  curr_stage = 0;
                  waitingForResponse = false;
                } else {
                  curr_stage = 2;
                  waitingForResponse = false;
                }
              } else {
                Serial.println("[ERROR] Receiver Error! Aborting.");
                DW3000.clearSystemStatus();
              }
            }
            break;
            
          case 2:  // Respuesta recibida. Enviar segundo ranging.
            rx = DW3000.readRXTimestamp();
            DW3000.ds_sendFrame(3);
            t_roundA = rx - tx;
            tx = DW3000.readTXTimestamp();
            t_replyA = tx - rx;
            curr_stage = 3;
            timeoutStart = millis();
            waitingForResponse = true;
            break;
            
          case 3:  // Esperar segunda respuesta.
            if (rx_status = DW3000.receivedFrameSucc()) {
              DW3000.clearSystemStatus();
              if (rx_status == 1) {
                if (DW3000.ds_isErrorFrame()) {
                  Serial.println("[WARNING] Error frame detected! Reverting to stage 0.");
                  curr_stage = 0;
                  waitingForResponse = false;
                } else {
                  waitingForResponse = false;
                  clock_offset = DW3000.getRawClockOffset();
                  curr_stage = 4;
                }
              } else {
                Serial.println("[ERROR] Receiver Error! Aborting.");
                DW3000.clearSystemStatus();
              }
            }
            break;
            
          case 4:  // Respuesta recibida. Calcular resultados.
            ranging_time = DW3000.ds_processRTInfo(t_roundA, t_replyA, DW3000.read(0x12, 0x04), DW3000.read(0x12, 0x08), clock_offset);
            distance = DW3000.convertToCM(ranging_time);
            anchor_distance[ii] = kalmanFilterDistance(distance, ii);
            
            // Guardar la medición en buffer y calcular promedio
            {
              distance_buffer[ii][buffer_index[ii]] = anchor_distance[ii];
              buffer_index[ii] = (buffer_index[ii] + 1) % NUM_MEASUREMENTS;
              
              float sum = 0;
              for (int j = 0; j < NUM_MEASUREMENTS; j++) {
                sum += distance_buffer[ii][j];
              }
              anchor_avg[ii] = sum / NUM_MEASUREMENTS;
            }
            
            // Obtener potencia de señal
            pot_sig[ii] = DW3000.getSignalStrength();
            
            curr_stage = 0;
            fin_de_com = 1;
            
            delay(50);  // Reducido de ROUND_DELAY a un valor fijo de 50ms para acelerar el proceso
            break;
            
          default:
            Serial.print("[ERROR] Estado desconocido (");
            Serial.print(curr_stage);
            Serial.println("). Volviendo a estado 0.");
            curr_stage = 0;
            break;
        }
      }
    }
    
    // Calcular posición del tag si tenemos suficientes mediciones
    if (fin_de_com >= NUM_ANCHORS) {
      // Calcular posición usando multilateración
      // Aquí usamos un algoritmo simplificado basado en las distancias a los anchors
      
      // Posiciones de los anchors (en metros)
      float anchorsPos[NUM_ANCHORS][2] = {
        {0.0, 1.10},    // Anclaje 10 (0, 1.10)
        {0.0, 4.55},    // Anclaje 20 (0, 4.55)
        {3.45, 3.5},    // Anclaje 30 (3.45, 3.5)
        {3.45, 0.66}    // Anclaje 40 (3.45, 0.66)
      };
      
      // Convertir distancias de cm a metros
      float distances[NUM_ANCHORS];
      for (int i = 0; i < NUM_ANCHORS; i++) {
        distances[i] = anchor_avg[i] / 100.0;
      }
      
      // Algoritmo de multilateración para 4 anclas usando mínimos cuadrados
      // Preparamos el sistema de ecuaciones
      float A1 = 2 * (anchorsPos[1][0] - anchorsPos[0][0]);
      float B1 = 2 * (anchorsPos[1][1] - anchorsPos[0][1]);
      float C1 = distances[0]*distances[0] - distances[1]*distances[1] - 
                 anchorsPos[0][0]*anchorsPos[0][0] + anchorsPos[1][0]*anchorsPos[1][0] - 
                 anchorsPos[0][1]*anchorsPos[0][1] + anchorsPos[1][1]*anchorsPos[1][1];
      
      float A2 = 2 * (anchorsPos[2][0] - anchorsPos[1][0]);
      float B2 = 2 * (anchorsPos[2][1] - anchorsPos[1][1]);
      float C2 = distances[1]*distances[1] - distances[2]*distances[2] - 
                 anchorsPos[1][0]*anchorsPos[1][0] + anchorsPos[2][0]*anchorsPos[2][0] - 
                 anchorsPos[1][1]*anchorsPos[1][1] + anchorsPos[2][1]*anchorsPos[2][1];
      
      float A3 = 2 * (anchorsPos[3][0] - anchorsPos[2][0]);
      float B3 = 2 * (anchorsPos[3][1] - anchorsPos[2][1]);
      float C3 = distances[2]*distances[2] - distances[3]*distances[3] - 
                 anchorsPos[2][0]*anchorsPos[2][0] + anchorsPos[3][0]*anchorsPos[3][0] - 
                 anchorsPos[2][1]*anchorsPos[2][1] + anchorsPos[3][1]*anchorsPos[3][1];
      
      float A4 = 2 * (anchorsPos[0][0] - anchorsPos[3][0]);
      float B4 = 2 * (anchorsPos[0][1] - anchorsPos[3][1]);
      float C4 = distances[3]*distances[3] - distances[0]*distances[0] - 
                 anchorsPos[3][0]*anchorsPos[3][0] + anchorsPos[0][0]*anchorsPos[0][0] - 
                 anchorsPos[3][1]*anchorsPos[3][1] + anchorsPos[0][1]*anchorsPos[0][1];
      
      // Calculamos A^T * A
      float ATA11 = A1*A1 + A2*A2 + A3*A3 + A4*A4;
      float ATA12 = A1*B1 + A2*B2 + A3*B3 + A4*B4;
      float ATA21 = ATA12;  // Es simétrica
      float ATA22 = B1*B1 + B2*B2 + B3*B3 + B4*B4;
      
      // Calculamos A^T * b
      float ATb1 = A1*C1 + A2*C2 + A3*C3 + A4*C4;
      float ATb2 = B1*C1 + B2*C2 + B3*C3 + B4*C4;
      
      // Calculamos el determinante de A^T * A
      float detATA = ATA11 * ATA22 - ATA12 * ATA21;
      
      if (abs(detATA) > 0.001) {
        // Calculamos la inversa de A^T * A
        float invATA11 = ATA22 / detATA;
        float invATA12 = -ATA12 / detATA;
        float invATA21 = -ATA21 / detATA;
        float invATA22 = ATA11 / detATA;
        
        // Finalmente, calculamos x = (A^T * A)^(-1) * A^T * b
        float x = invATA11 * ATb1 + invATA12 * ATb2;
        float y = invATA21 * ATb1 + invATA22 * ATb2;
        
        // Aplicar límites razonables
        x = max(0.0f, min(3.45f, x));
        y = max(0.0f, min(5.1f, y));
        
        // Aplicar filtro de Kalman a la posición
        kalmanFilterPosition(x, y);
      }
    }
    
    // Imprimir resultados por Serial
    for (int i = 0; i < NUM_ANCHORS; i++) {
      Serial.print("Anclaje ");
      Serial.print(ID_PONG[i]);
      Serial.print(" ");
      DW3000.printDouble(anchor_distance[i], 100, false);
      Serial.print(" cm, Promedio = ");
      Serial.print(anchor_avg[i]);
      Serial.print(", Potencia = ");
      Serial.print(pot_sig[i]);
      Serial.println("dBm");
    }
    
    // Enviar datos por UDP (broadcasting)
    broadcastUDP();
    
    // Comprobar zonas
    checkZones();
    
    // Registrar datos en formato CSV si está habilitado
    if (currentMillis - lastCsvTime >= csvInterval) {
      lastCsvTime = currentMillis;
      logCsvData();
    }
    
    // Reiniciar contador de comunicación
    fin_de_com = 0;
  }
}
