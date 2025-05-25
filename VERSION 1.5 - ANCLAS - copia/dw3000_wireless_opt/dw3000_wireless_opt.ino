#include <SPI.h>
#include "DW3000.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#define MQTT_MAX_PACKET_SIZE 1024 // Increase buffer size
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ===== IDENTIFICACIÓN DEL TAG =====
#define TAG_ID 2 // <---- CAMBIAR A 2 PARA EL SEGUNDO TAG

// ===== CONFIGURACIÓN WiFi =====
#define USE_AP_MODE false
#define AP_SSID "UWB_TAG_AP"
#define AP_PASS "12345678"
#define STA_SSID "iPhone de Nicolas"
#define STA_PASS "12345678"

// Configuraciones del servidor y UDP
#define HTTP_PORT 80
#define UDP_PORT 5555
WebServer server(HTTP_PORT);
WiFiUDP udp;
IPAddress broadcastIP(255, 255, 255, 255);

// MQTT Configuration
const char* mqtt_server = "172.20.10.3"; // Broker IP (Your PC)
const int mqtt_port = 1883;
const char* log_topic = "uwb/tag/logs";       // Topic for detailed CSV logs
char status_topic[30];                      // Topic for simple status (constructed in setup)
WiFiClient espClient;
PubSubClient client(espClient);

// ===== Configuration for WiFi Logging =====
const char* logServerIp = "172.20.10.3"; // <<< CHANGE THIS to your computer's IP on the hotspot network
const int logServerPort = 5000;             // Port your Python receiver will listen on

// ===== TDMA Configuration =====
const unsigned long TDMA_CYCLE_MS = 1000; // Total cycle length (1 second)
const unsigned long TDMA_SLOT_DURATION_MS = 500; // Slot duration (0.5 seconds per tag)

// ===== CONFIGURACIÓN DE RANGING =====
#define ROUND_DELAY 100
static int frame_buffer = 0;
static int rx_status;
static int tx_status;

// Estados del ranging
static int curr_stage = 0;

static int t_roundA = 0;
static int t_replyA = 0;

static long long rx = 0;
static long long tx = 0;

static int clock_offset = 0;
static int ranging_time = 0;
static float distance = 0;

// Configuraciones para mediciones y filtrado
#define NUM_MEASUREMENTS 5
#define NUM_ANCHORS 4
int ID_PONG[NUM_ANCHORS] = {10, 20, 30, 40};
float distance_buffer[NUM_ANCHORS][NUM_MEASUREMENTS] = { {0} };
int buffer_index[NUM_ANCHORS] = {0};
float anchor_distance[NUM_ANCHORS] = {0};
float anchor_avg[NUM_ANCHORS] = {0};
float pot_sig[NUM_ANCHORS] = {0};
static int fin_de_com = 0;
bool anchor_responded[NUM_ANCHORS] = {false}; // Added: Array to track anchor responses

// Variables para timeout
unsigned long timeoutStart = 0;
bool waitingForResponse = false;
const unsigned long RESPONSE_TIMEOUT = 200; // Time to wait for anchor response (was 5000)

// Variables para gestor de estados
unsigned long lastUpdate = 0;
unsigned long updateInterval = 50;

// Variables para modo de bajo consumo
unsigned long lastActivityTime = 0;
const unsigned long SLEEP_TIMEOUT = 300000;
bool lowPowerMode = false;

// Variables para Filtro de Kalman
float kalman_dist[NUM_ANCHORS][2] = { {0} };
float kalman_dist_q = 0.05; // Increased Q for more responsiveness
float kalman_dist_r = 0.1;

// Variables para posición
float kalman_x = 0.0;
float kalman_y = 0.0;
float kalman_p_x = 1.0;
float kalman_p_y = 1.0;
float kalman_q = 0.01;
float kalman_r = 0.1;

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
  {0.5, 0.5, 0.3, false, 0, 3000, false},
  {2.5, 2.5, 0.4, false, 0, 5000, false},
  {1.5, 4.0, 0.5, false, 0, 10000, false}
};

// Variables para MQTT y Estado
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastStatusUpdate = 0; // Para enviar estado MQTT periódicamente
const long statusUpdateInterval = 5000; // Enviar estado MQTT cada 5 segundos
String last_anchor_id = "N/A"; // Variable para almacenar el ID del último ancla vista

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
    button { background: #4CAF50; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; }
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
    @keyframes pulse {
      0% { box-shadow: 0 0 0 0 rgba(255,0,0,0.7); }
      70% { box-shadow: 0 0 0 10px rgba(255,0,0,0); }
      100% { box-shadow: 0 0 0 0 rgba(255,0,0,0); }
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
    let tagPosition = { x: 150, y: 150 }; // Initial guess
    let visualizationInitialized = false;
    let vizElements = { // Store references to visualization DOM elements
        container: null,
        border: null,
        anchorPoints: {},
        distanceCircles: {},
        tagPoint: null
    };
    let anchorListItems = {}; // Store references to anchor list DOM elements

    // Obtener datos del ESP32
    function fetchData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          window.currentTagPositionFromESP = data.position; // Almacenar posición del ESP
          window.currentAnchorsData = data.anchors; // Almacenar datos de anclas por si renderVisualization los necesita directamente
          updateUI(data); // Pasar todos los datos a updateUI
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

      // Actualizar anclajes data (usaremos window.currentAnchorsData o data.anchors)
      anchors = data.anchors; // Mantenemos esto por si renderVisualization lo usa directamente

      // Depuración - mostrar distancias en consola
      // console.log("Distancias recibidas (cm):", anchors.map(a => a.dist));

      // --- Optimización: Actualizar lista de anclas sin recrear todo ---
      const anchorsContainer = document.getElementById('anchors-container');
      let anchorsChanged = anchorsContainer.children.length !== anchors.length;

      anchors.forEach((anchor, i) => {
        let anchorDiv = anchorListItems[anchor.id];
        if (!anchorDiv) {
          // Crear el div del ancla si no existe
          anchorDiv = document.createElement('div');
          anchorDiv.className = 'anchor';
          anchorDiv.id = `anchor-list-item-${anchor.id}`;
          anchorDiv.innerHTML = `
            <div>
              <strong>Anclaje ${anchor.id}</strong>
              <p>Distancia: <span class="anchor-dist">${anchor.dist.toFixed(1)}</span> cm</p>
            </div>
            <div>
              <p>Señal: <span class="anchor-rssi">${anchor.rssi.toFixed(1)}</span> dBm</p>
            </div>
          `;
          anchorsContainer.appendChild(anchorDiv);
          anchorListItems[anchor.id] = anchorDiv; // Guardar referencia
          anchorsChanged = true;
        } else {
          // Actualizar datos si ya existe
          anchorDiv.querySelector('.anchor-dist').textContent = anchor.dist.toFixed(1);
          anchorDiv.querySelector('.anchor-rssi').textContent = anchor.rssi.toFixed(1);
        }
      });
      // Podríamos añadir lógica para eliminar anclas si desaparecen, pero asumimos que son fijas
      // --- Fin Optimización Lista Anclas ---

      // Calcular posición del tag por trilateración (ahora usa datos del ESP)
      // Ya no necesitamos la condición de anchors.length >=4 aquí si confiamos en data.position
      calculateTagPosition(); // Esta función ahora usará window.currentTagPositionFromESP
      
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

        // Obtener la posición X, Y calculada por el ESP32 (que ya debería estar filtrada por Kalman si está activo en C++)
        // Estos valores vienen de data.position.x y data.position.y en updateUI
        // Necesitamos acceder a la variable 'data' globalmente o pasarla.
        // Por simplicidad, asumiremos que 'currentData' es una variable global actualizada en fetchData.
        // Sería mejor pasar 'data.position' como argumento a calculateTagPosition.
        
        // Para este ejemplo, vamos a necesitar que 'fetchData' almacene 'data.position' en una variable accesible.
        // Modificaremos fetchData y updateUI ligeramente.

        if (!window.currentTagPositionFromESP) {
            document.getElementById('tag-position').textContent = "Esperando datos del ESP...";
            return;
        }

        let esp_x = window.currentTagPositionFromESP.x;
        let esp_y = window.currentTagPositionFromESP.y;

        // Depuración: mostrar la posición recibida del ESP32
        console.log("Posición RECIBIDA del ESP32 (metros):", esp_x, esp_y);
              
        try {
          // Aplicar límites razonables (el ESP32 ya debería haber hecho algo similar)
          const boundedX = Math.max(0, Math.min(areaWidth, esp_x));
          const boundedY = Math.max(0, Math.min(areaHeight, esp_y));
      
          // Convertir a coordenadas de visualización
          const pixelX = margin + boundedX * scale;
          const pixelY = vizHeight - margin - boundedY * scale;
      
          // Suavizar el movimiento: mezclamos la nueva posición con la anterior
          tagPosition.x = tagPosition.x * 0.3 + pixelX * 0.7; 
          tagPosition.y = tagPosition.y * 0.3 + pixelY * 0.7; 
      
          // Actualizar texto de posición
          document.getElementById('tag-position').textContent = 
            `X: ${boundedX.toFixed(2)}m, Y: ${boundedY.toFixed(2)}m (ESP)`;
        } catch (e) {
          console.error('Error en conversión de posición ESP:', e);
          document.getElementById('tag-position').textContent = "Error de visualización";
        }
      }
    
    // --- Optimización: Renderizar visualización actualizando elementos existentes ---
    function renderVisualization() {
      const viz = document.getElementById('visualization');
      if (!viz) return; 

      if (!visualizationInitialized) {
        vizElements.container = viz;
        viz.innerHTML = ''; 

        const areaWidth = 3.45;  
        const areaHeight = 5.1;  
        const scale = 80;        
        const margin = 20;       
        const vizWidth = areaWidth * scale + 2 * margin;
        const vizHeight = areaHeight * scale + 2 * margin;

        viz.style.width = vizWidth + 'px';
        viz.style.height = vizHeight + 'px';
        viz.style.position = 'relative'; 

        const currentAnchorsToRender = window.currentAnchorsData || anchors; // Usar datos actualizados

        // Posiciones de los anclajes (en píxeles)
        // Asegurarse de que las anclas se dibujen correctamente según las posiciones físicas
        const anchorsPosMetros = [
          { id: 10, x: 0.0, y: 1.10 }, { id: 20, x: 0.0, y: 4.55 },
          { id: 30, x: 3.45, y: 3.5 }, { id: 40, x: 3.45, y: 0.66 }
        ];

        const border = document.createElement('div');
        border.style.position = 'absolute';
        border.style.left = margin + 'px';
        border.style.top = margin + 'px';
        border.style.width = (areaWidth * scale) + 'px';
        border.style.height = (areaHeight * scale) + 'px';
        border.style.border = '2px solid #333';
        border.style.boxSizing = 'border-box';
        viz.appendChild(border);
        vizElements.border = border;

        // Crear puntos de anclajes y círculos de distancia solo si hay datos de anclas
        if (currentAnchorsToRender && currentAnchorsToRender.length > 0) {
            currentAnchorsToRender.forEach((anchorData, i) => {
                const anchorCfg = anchorsPosMetros.find(a => a.id === anchorData.id);
                if (!anchorCfg) return; // Si no hay configuración para este ID de ancla

                const anchorPixelX = margin + anchorCfg.x * scale;
                const anchorPixelY = vizHeight - margin - anchorCfg.y * scale;

                const dot = document.createElement('div');
                dot.className = 'anchor-point';
                dot.textContent = anchorData.id; // Usar ID del ancla
                dot.title = `Anclaje ${anchorData.id}`;
                dot.style.position = 'absolute'; 
                dot.style.left = anchorPixelX + 'px';
                dot.style.top = anchorPixelY + 'px';
                dot.style.transform = 'translate(-50%, -50%)'; 
                viz.appendChild(dot);
                vizElements.anchorPoints[anchorData.id] = dot; 

                const circle = document.createElement('div');
                circle.className = 'distance-circle';
                circle.style.position = 'absolute'; 
                circle.style.left = anchorPixelX + 'px';
                circle.style.top = anchorPixelY + 'px';
                const radius = (anchorData.dist / 100) * scale;
                circle.style.width = radius * 2 + 'px';
                circle.style.height = radius * 2 + 'px';
                circle.style.transform = 'translate(-50%, -50%)'; 
                viz.appendChild(circle);
                vizElements.distanceCircles[anchorData.id] = circle; 
            });
        }

        const tagPoint = document.createElement('div');
        tagPoint.className = 'tag-point';
        tagPoint.style.position = 'absolute'; 
        tagPoint.style.left = tagPosition.x + 'px';
        tagPoint.style.top = tagPosition.y + 'px';
        tagPoint.style.transform = 'translate(-50%, -50%)'; 
        viz.appendChild(tagPoint);
        vizElements.tagPoint = tagPoint; 

        visualizationInitialized = true;
      } else {
        const scale = 80; 
        const currentAnchorsToRender = window.currentAnchorsData || anchors;

        // Actualizar círculos de distancia solo si hay datos de anclas
        if (currentAnchorsToRender && currentAnchorsToRender.length > 0) {
            currentAnchorsToRender.forEach((anchorData) => {
                const circle = vizElements.distanceCircles[anchorData.id];
                if (circle) {
                    const radius = (anchorData.dist / 100) * scale;
                    const currentWidth = parseFloat(circle.style.width) || 0;
                    if (radius >= 0 && Math.abs(radius * 2 - currentWidth) > 0.1) { 
                        circle.style.width = radius * 2 + 'px';
                        circle.style.height = radius * 2 + 'px';
                    }
                }
            });
        }

        // Actualizar posición del tag (debe hacerse siempre si tagPoint existe)
        const tagPoint = vizElements.tagPoint;
        if (tagPoint && typeof tagPosition.x === 'number' && typeof tagPosition.y === 'number' && !isNaN(tagPosition.x) && !isNaN(tagPosition.y)) {
           console.log("Dibujando TAG en (píxeles):", tagPosition.x, tagPosition.y); // Para depuración
           tagPoint.style.transform = `translate(calc(-50% + ${tagPosition.x}px), calc(-50% + ${tagPosition.y}px))`;
        } else if (tagPoint) {
           console.log("Posición de TAG inválida o tagPoint no listo:", tagPosition.x, tagPosition.y); // Para depuración
           // Opcional: ocultar el tag si la posición no es válida, en lugar de dejarlo donde estaba
           // tagPoint.style.transform = 'translate(-10000px, -10000px)'; 
        }
      }
    }
    // --- Fin Optimización Visualización ---

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
    Serial.print("Configuring WiFi mode AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP created. IP address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.print("Configuring WiFi mode STA...");
    WiFi.mode(WIFI_STA);
    Serial.println(" Done.");
    Serial.print("Beginning WiFi connection to SSID: ");
    Serial.println(STA_SSID);
    WiFi.begin(STA_SSID, STA_PASS);

    Serial.print("Attempting WiFi connection (15s timeout)...");
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) { // 15-second timeout
      Serial.print("."); // Print dots while waiting
      delay(500);
    }
    Serial.println(); // Newline after dots

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected successfully!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.print("WiFi connection FAILED! Status: ");
      Serial.println(WiFi.status()); // Print the specific failure status code
      // WL_NO_SSID_AVAIL = 1, WL_CONNECT_FAILED = 3, WL_CONNECTION_LOST = 4, WL_DISCONNECTED = 6 etc.
    }
  }
}

// Genera los datos en formato JSON usando ArduinoJson
String getDataJson() {
  // Calcular el tamaño necesario para el JSON
  // Tamaño base + (tamaño por ancla * NUM_ANCHORS) + tamaño posición
  const int capacity = JSON_OBJECT_SIZE(3) + // Nivel raíz: battery, anchors, position
                       JSON_ARRAY_SIZE(NUM_ANCHORS) + // Array 'anchors'
                       NUM_ANCHORS * JSON_OBJECT_SIZE(3) + // Objetos dentro de 'anchors': id, dist, rssi
                       JSON_OBJECT_SIZE(2); // Objeto 'position': x, y
                       
  StaticJsonDocument<capacity> doc;

  // Añadir nivel de batería (placeholder)
  doc["battery"] = 100.0; // Usar 100.0 como valor temporal

  // Añadir datos de anclajes
  JsonArray anchorsArray = doc.createNestedArray("anchors");
  for (int i = 0; i < NUM_ANCHORS; i++) {
    JsonObject anchorObject = anchorsArray.createNestedObject();
    anchorObject["id"] = ID_PONG[i];
    // Asegurarse de enviar valores válidos (evitar NaN o Inf)
    anchorObject["dist"] = isnan(anchor_distance[i]) || isinf(anchor_distance[i]) ? 0.0 : anchor_distance[i]; // Distancia en cm
    anchorObject["rssi"] = isnan(pot_sig[i]) || isinf(pot_sig[i]) ? -100.0 : pot_sig[i];       // Potencia de señal
  }

  // Añadir posición del tag
  JsonObject positionObject = doc.createNestedObject("position");
  // Asegurarse de enviar valores válidos
  positionObject["x"] = isnan(tagPositionX) || isinf(tagPositionX) ? 0.0 : tagPositionX; // Posición X en metros
  positionObject["y"] = isnan(tagPositionY) || isinf(tagPositionY) ? 0.0 : tagPositionY; // Posición Y en metros

  // Serializar JSON a String
  String output;
  serializeJson(doc, output);
  return output;
}

// Configura el servidor web
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", INDEX_HTML);
  });
  
  server.on("/data", HTTP_GET, []() {
    server.send(200, "application/json", getDataJson());
  });
  
  server.begin();
  Serial.println("Servidor HTTP iniciado en puerto 80");
}

// Envía datos por UDP (broadcasting)
void broadcastUDP() {
  if(WiFi.status() == WL_CONNECTED && !USE_AP_MODE) {
    String udpMessage = "Tag: " + String(TAG_ID) + ", LastAnchor: " + String(last_anchor_id);
    udp.beginPacket(broadcastIP, UDP_PORT);
    udp.print(udpMessage); 
    udp.endPacket();
  }
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
      
      if (!zones[i].tagInside) {
        zones[i].tagInside = true;
        zones[i].entryTime = millis();
        
        Serial.print("Tag entró en zona ");
        Serial.println(i);
      }
      
      if (millis() - zones[i].entryTime >= zones[i].minStayTime && !zones[i].stayTimeReached) {
        zones[i].stayTimeReached = true;
        
        Serial.print("Tiempo mínimo alcanzado en zona ");
        Serial.println(i);
      }
    } else {
      if (zones[i].tagInside) {
        zones[i].tagInside = false;
        zones[i].stayTimeReached = false;
        
        Serial.print("Tag salió de zona ");
        Serial.println(i);
      }
    }
  }
}

// --- Funciones MQTT --- 

void reconnectMQTT() {
  // Loop until we're reconnected
  if (!client.connected()) {
    long now = millis();
    // Try to reconnect every 5 seconds
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      Serial.print("Attempting MQTT connection...");
      // Create a unique client ID using MAC address and TAG_ID
      String clientId = "ESP32-Tag-";
      clientId += String(TAG_ID);
      clientId += "-";
      clientId += WiFi.macAddress();

      // Attempt to connect
      if (client.connect(clientId.c_str())) {
        Serial.println("connected");
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
      }
    }
  }
}

void publishStatus() {
   if (client.connected()) {
     StaticJsonDocument<200> doc; // Adjust size as needed
     doc["tag_id"] = TAG_ID;
     doc["last_anchor_id"] = last_anchor_id; // Use the global last_anchor_id
     doc["timestamp_ms"] = millis();

     char buffer[200];
     size_t n = serializeJson(doc, buffer);

     if (client.publish(status_topic, buffer, n)) { 
         // Serial.println("MQTT Status published"); // Optional debug message
     } else {
         Serial.println("Failed to publish MQTT status");
     }
   }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== UWB TAG with WiFi, Web Server, and SD Logging ===\n");
  
  setupWiFi();
  
  setupWebServer();
  
  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  snprintf(status_topic, sizeof(status_topic), "%s%d/status", "uwb/tag/", TAG_ID); // Construir topic de estado ej: uwb/tag/1/status
  Serial.print("MQTT Status topic set to: ");
  Serial.println(status_topic);

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
  
  lastActivityTime = millis();
  lastUpdate = millis();
}

void loop() {
  server.handleClient(); // Handle web server requests
  
  // Handle MQTT Client
  if (!client.connected()) {
    reconnectMQTT(); // Try to reconnect if disconnected
  }
  client.loop(); // Allow MQTT client to process messages/maintain connection

  unsigned long currentMillis = millis();
  
  if (!lowPowerMode && (currentMillis - lastActivityTime >= SLEEP_TIMEOUT)) {
    Serial.println("Entrando en modo de bajo consumo...");
    lowPowerMode = true;
    updateInterval = 1000;
  }
  
  if (currentMillis - lastUpdate >= updateInterval) {
    lastUpdate = currentMillis;
    lastActivityTime = currentMillis;
    
    unsigned long time_in_cycle = currentMillis % TDMA_CYCLE_MS;
    unsigned long assigned_slot_start = (TAG_ID - 1) * TDMA_SLOT_DURATION_MS;
    unsigned long assigned_slot_end = assigned_slot_start + TDMA_SLOT_DURATION_MS;

    bool is_my_slot = (time_in_cycle >= assigned_slot_start && time_in_cycle < assigned_slot_end);

    if (is_my_slot && !lowPowerMode) { 
        lastActivityTime = currentMillis; // Update activity time when ranging

        String dataString = ""; // Declare dataString outside the switch

        // Reset anchor status for this cycle
        for(int k=0; k<NUM_ANCHORS; k++) {
          anchor_responded[k] = false;
        }

        for (int ii = 0; ii < NUM_ANCHORS; ii++) {
          DW3000.setDestinationID(ID_PONG[ii]);
          fin_de_com = 0;
          
          while (fin_de_com == 0) {
            if (waitingForResponse && ((millis() - timeoutStart) >= RESPONSE_TIMEOUT)) {
              Serial.print("Timeout REFORZADO para ancla ID: "); 
              Serial.println(ID_PONG[ii]);

              DW3000.softReset();
              delay(100); 
              DW3000.init(); 
              DW3000.configureAsTX(); 
              DW3000.clearSystemStatus(); 

              anchor_distance[ii] = 0;
              pot_sig[ii] = -120.0f; // Valor de dBm muy bajo para indicar mala calidad/sin señal
              anchor_responded[ii] = false;
              
              curr_stage = 0; 
              ranging_time = 0;
              waitingForResponse = false; 
              fin_de_com = 1; 
              
              Serial.println("[INFO] TAG DW3000 re-inicializado post-timeout.");
              break; 
            }
            
            switch (curr_stage) {
              case 0:
                t_roundA = 0;
                t_replyA = 0;
                DW3000.ds_sendFrame(1);
                tx = DW3000.readTXTimestamp();
                curr_stage = 1;
                timeoutStart = millis();
                waitingForResponse = true;
                break;
                
              case 1:
                if (rx_status = DW3000.receivedFrameSucc()) {
                  DW3000.clearSystemStatus(); // Limpiar estado del sistema después de la recepción
                  if ((rx_status == 1) && (DW3000.getDestinationID() == ID_PONG[ii])) {
                    if (DW3000.ds_isErrorFrame()) {
                      Serial.println("[WARNING] Error frame detected! Reverting to stage 0.");
                      curr_stage = 0;
                      waitingForResponse = false;
                      // No rompemos el while (fin_de_com), dejamos que intente de nuevo o timeoutee si el ancla sigue mal
                    } else if ((DW3000.getDestinationID() != ID_PONG[ii])) {
                      // Mensaje para otra ancla, ignorar y seguir esperando (o timeout)
                      break; // Sale del switch, pero no del while, sigue esperando respuesta para ID_PONG[ii]
                    } else if (DW3000.ds_getStage() != 2) {
                      Serial.println("[WARNING] Stage incorrecto del ancla. Enviando error frame.");
                      DW3000.ds_sendErrorFrame();
                      curr_stage = 0; // Reiniciar protocolo para esta ancla
                      waitingForResponse = false;
                      // No rompemos el while (fin_de_com), dejamos que intente de nuevo o timeoutee
                    } else {
                      curr_stage = 2;
                      waitingForResponse = false;
                    }
                  } else { // rx_status != 1 O el ID no es el correcto
                    Serial.print("[ERROR] Receiver Error (case 1) o ID incorrecto. RX_STATUS: ");
                    Serial.print(rx_status);
                    Serial.print(", DEST_ID: ");
                    Serial.println(DW3000.getDestinationID());
                    
                    DW3000.softReset();
                    delay(100);
                    DW3000.init();
                    DW3000.configureAsTX();
                    DW3000.clearSystemStatus();

                    anchor_distance[ii] = 0;
                    pot_sig[ii] = -120.0f; // Valor de dBm muy bajo para indicar mala calidad/sin señal
                    anchor_responded[ii] = false;
                    curr_stage = 0;
                    waitingForResponse = false;
                    fin_de_com = 1; 
                    Serial.println("[INFO] TAG DW3000 re-inicializado post-receiver error (case 1).");
                  }
                }
                break;
                
              case 2:
                rx = DW3000.readRXTimestamp();
                DW3000.ds_sendFrame(3);
                t_roundA = rx - tx;
                tx = DW3000.readTXTimestamp();
                t_replyA = tx - rx;
                curr_stage = 3;
                timeoutStart = millis();
                waitingForResponse = true;
                break;
                
              case 3:
                if (rx_status = DW3000.receivedFrameSucc()) {
                  DW3000.clearSystemStatus(); // Limpiar estado del sistema después de la recepción
                  if (rx_status == 1) { // ASUMIMOS que el PONG no cambia el Destination ID para el último msg
                    if (DW3000.ds_isErrorFrame()) {
                      Serial.println("[WARNING] Error frame detected (case 3)! Reverting to stage 0.");
                      curr_stage = 0;
                      waitingForResponse = false;
                      // No rompemos el while (fin_de_com)
                    } else {
                      waitingForResponse = false;
                      clock_offset = DW3000.getRawClockOffset();
                      curr_stage = 4;
                    }
                  } else { // rx_status != 1
                    Serial.print("[ERROR] Receiver Error (case 3)! RX_STATUS: ");
                    Serial.println(rx_status);

                    DW3000.softReset();
                    delay(100);
                    DW3000.init();
                    DW3000.configureAsTX();
                    DW3000.clearSystemStatus();

                    anchor_distance[ii] = 0;
                    pot_sig[ii] = -120.0f; // Valor de dBm muy bajo para indicar mala calidad/sin señal
                    anchor_responded[ii] = false;
                    curr_stage = 0;
                    waitingForResponse = false;
                    fin_de_com = 1; 
                    Serial.println("[INFO] TAG DW3000 re-inicializado post-receiver error (case 3).");
                  }
                }
                break;
                
              case 4:
                ranging_time = DW3000.ds_processRTInfo(t_roundA, t_replyA, DW3000.read(0x12, 0x04), DW3000.read(0x12, 0x08), clock_offset);
                distance = DW3000.convertToCM(ranging_time);
                
                // Leer la potencia de la señal recibida y almacenarla
                pot_sig[ii] = DW3000.getSignalStrength();

                anchor_responded[ii] = true; 
                if (distance > 0) { 
                    anchor_distance[ii] = kalmanFilterDistance(distance, ii);
                } else {
                    anchor_distance[ii] = 0; 
                    // Si la distancia es inválida, también deberíamos considerar la señal como mala para WLS
                    // pot_sig[ii] = -120.0f; // Opcional: si distancia es 0, forzar mala señal para WLS
                }
 
                dataString = String(TAG_ID) + "," +
                                  String(millis()) + "," +
                                  String(ID_PONG[ii]) + "," +
                                  String(distance, 2) + "," +
                                  String(anchor_distance[ii], 2) + "," +
                                  String(pot_sig[ii], 2) + "," +
                                  String(anchor_responded[ii] ? 1 : 0); 
                
                // --- Publish SINGLE log line IMMEDIATELY --- 
                if (client.connected()) {
                    if (!client.publish(log_topic, dataString.c_str())) {
                       Serial.println("MQTT Publish Failed (single log line)"); 
                    }
                } else {
                    // Optional: Indicate MQTT isn't connected when trying to send log
                    // Serial.println("MQTT disconnected, cannot send log line."); 
                }
                // ----------------------------------------------
 
                // Respond with a PONG
                curr_stage = 0;
                fin_de_com = 1;
                
                // REMOVED: delay(50); // This was slowing down the measurement cycle
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
        
        // Count responding anchors
        int responding_anchors = 0;
        for(int k=0; k<NUM_ANCHORS; k++) {
          if(anchor_responded[k]) responding_anchors++;
        }

        // Only trilaterate if enough anchors responded
        if (responding_anchors >= 3) { 
          // Calcular posición del tag usando multilateración PONDERADA
          
          float anchorsPos[NUM_ANCHORS][2] = {
            {0.0, 1.10},
            {0.0, 4.55},
            {3.45, 3.5},
            {3.45, 0.66}
          };
          
          float distances[NUM_ANCHORS];
          for (int i = 0; i < NUM_ANCHORS; i++) {
            distances[i] = anchor_distance[i] / 100.0; 
          }

          float quality[NUM_ANCHORS];
          for (int i = 0; i < NUM_ANCHORS; i++) {
            // Si anchor_responded[i] es falso, pot_sig[i] ya se habrá establecido a -120.0f
            quality[i] = max(0.01f, pot_sig[i] + 120.1f); 
          }

          float w_eq1 = (quality[0] + quality[1]) / 2.0f; 
          float w_eq2 = (quality[1] + quality[2]) / 2.0f; 
          float w_eq3 = (quality[2] + quality[3]) / 2.0f; 
          float w_eq4 = (quality[3] + quality[0]) / 2.0f; 
          
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
          
          float ATA11 = w_eq1*A1*A1 + w_eq2*A2*A2 + w_eq3*A3*A3 + w_eq4*A4*A4;
          float ATA12 = w_eq1*A1*B1 + w_eq2*A2*B2 + w_eq3*A3*B3 + w_eq4*A4*B4;
          float ATA21 = ATA12; 
          float ATA22 = w_eq1*B1*B1 + w_eq2*B2*B2 + w_eq3*B3*B3 + w_eq4*B4*B4;
          
          float ATb1 = w_eq1*A1*C1 + w_eq2*A2*C2 + w_eq3*A3*C3 + w_eq4*A4*C4;
          float ATb2 = w_eq1*B1*C1 + w_eq2*B2*C2 + w_eq3*B3*C3 + w_eq4*B4*C4;
          
          float detATA = ATA11 * ATA22 - ATA12 * ATA21;
          
          if (abs(detATA) > 0.001) {
            float invATA11 = ATA22 / detATA;
            float invATA12 = -ATA12 / detATA;
            float invATA21 = -ATA21 / detATA;
            float invATA22 = ATA11 / detATA;
            
            float x = invATA11 * ATb1 + invATA12 * ATb2;
            float y = invATA21 * ATb1 + invATA22 * ATb2;
            
            x = max(0.0f, min(3.45f, x));
            y = max(0.0f, min(5.1f, y));
            
            tagPositionX = x;
            tagPositionY = y;
            
            // COMMENTED OUT: Optional Kalman filter on final position
            // kalmanFilterPosition(x, y);
            
            checkZones();
          }
        }
        
        for (int i = 0; i < NUM_ANCHORS; i++) {
          Serial.print("Anclaje ");
          Serial.print(ID_PONG[i]);
          Serial.print(" ");
          DW3000.printDouble(anchor_distance[i], 100, false);
          Serial.print(" cm, Potencia = ");
          Serial.print(pot_sig[i]);
          Serial.println("dBm");
        }
        
        // broadcastUDP();
        
        // Publish MQTT status periodically
        if (currentMillis - lastStatusUpdate >= statusUpdateInterval) {
            lastStatusUpdate = currentMillis;
            publishStatus();
        }

        fin_de_com = 0;
    }
  } // <<< ADDED: Closing brace for if (currentMillis - lastUpdate >= updateInterval)

    // UDP Broadcast (Commented out for now)
    // if (currentMillis - lastUpdate >= 1000) {
    //     broadcastUDP();
    //     lastUpdate = currentMillis;
    // }

} // Closing brace for loop()
