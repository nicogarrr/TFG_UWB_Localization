/*
 * Código Final del Anchor (Respondedor DS-TWR con MQTT)
 * Compatible con el Tag que cicla secuencialmente y envía sus tiempos.
 * Este código debe ser personalizado para CADA anchor antes de flashearlo.
 */

#include <WiFi.h>
#include <PubSubClient.h>    // Para MQTT
#include "DW3000.h"          // Librería base UWB
#include "DW3000_Extension.h" // Extensión para payload y timestamp
#include <stdint.h>          // Para tipos uint16_t, uint64_t

// --- [>>> CONFIGURACIÓN PERSONALIZABLE POR ANCHOR <<<] ---

// --- Wi-Fi y MQTT ---
const char* ssid = "iPhone de Nicolas";         // Sustituir por tu SSID
const char* password = "12345678"; // Sustituir por tu contraseña
const char* mqtt_server = "172.20.10.2"; // Sustituir por la IP local de tu PC Gateway
const int mqtt_port = 1883;                 // Puerto estándar MQTT (normalmente 1883)

// --- Identificación y Coordenadas del Anchor ---
// ¡¡IMPORTANTE!! Cambia estos valores para CADA anchor específico
const char* MY_ANCHOR_LABEL = "A";     // Etiqueta legible ("A", "B", "C", "D")
const uint16_t MY_ANCHOR_UWB_ID = 10;    // ID numérico UWB (10, 20, 30, 40) - Debe coincidir con ANCHOR_IDS en el Tag
const float   MY_ANCHOR_X = 0.0;         // Coordenada X medida exacta (en metros)
const float   MY_ANCHOR_Y = 1.10;         // Coordenada Y medida exacta (en metros)
const float   MY_ANCHOR_Z = 1.5;         // Coordenada Z/Altura (en metros, opcional)

// --- [>>> FIN DE CONFIGURACIÓN PERSONALIZABLE <<<] ---


// --- Variables Globales MQTT ---
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (512)
char msg[MSG_BUFFER_SIZE];
char mqtt_topic[40];

// --- Variables UWB ---
static int rx_status;
static int tx_status;

/* Estados del ranging:
 * 0 - Esperando mensaje inicial del Tag (Ping, stage 1)
 * 1 - Mensaje inicial recibido; enviando primera respuesta (Pong, stage 2)
 * 2 - Primera respuesta enviada; esperando mensaje final del Tag (stage 3 con tiempos)
 * 3 - Mensaje final recibido; calculando distancia y publicando vía MQTT
 */
static int curr_stage = 0;

// Tiempos calculados por este Anchor (Pong)
static long long t_roundB_ll = 0; // Usar long long (64 bits)
static long long t_replyB_ll = 0;

// Tiempos recibidos del Tag (Ping)
static long long t_roundA_ll = 0;
static long long t_replyA_ll = 0;

// Timestamps UWB brutos
static long long rx1_ts_ll = 0;
static long long tx1_ts_ll = 0;
static long long rx2_ts_ll = 0;

// ID del Tag con el que se está comunicando
static uint16_t current_tag_id = 0;

// Constante física
// Renombrar para evitar conflicto con la definición en DW3000Constants.h
const double LIGHT_SPEED_MPS = 299702547.0; // m/s

// --- Funciones Auxiliares Wi-Fi y MQTT ---

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
}

void mqtt_reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Intentando conexión MQTT...");
    String clientId = "ESP32AnchorClient-";
    clientId += MY_ANCHOR_LABEL; // Usar etiqueta para ID de cliente
    clientId += "-";
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("conectado");
      // Opcional: Publicar estado de conexión
      // String status_topic = String("uwb/status/") + MY_ANCHOR_LABEL;
      // mqttClient.publish(status_topic.c_str(), "online");
    } else {
      Serial.print("falló, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" intentando de nuevo en 5 segundos");
      delay(5000);
    }
  }
}

// --- Función para Calcular Distancia DS-TWR ---
double calculate_distance_ds_twr(long long tRoundA, long long tReplyA, long long tRoundB, long long tReplyB) {
  if ((tRoundA + tRoundB + tReplyA + tReplyB) == 0) {
    return -1.0; // Evitar división por cero
  }

  double tof_uwb_units = ((double)tRoundA * tRoundB - (double)tReplyA * tReplyB) /
                         ((double)tRoundA + tRoundB + tReplyA + tReplyB);

  // DW3000 Tick approx 15.65 ps
  double T_DW_approx = 15.65e-12; // Segundos por unidad de tiempo UWB
  double tof_seconds = tof_uwb_units * T_DW_approx;
  double distance = tof_seconds * LIGHT_SPEED_MPS; // Distancia en metros

  return distance;
}


// --- SETUP ---
void setup() {
  Serial.begin(115200);
  delay(100);

  // --- Setup Wi-Fi y MQTT ---
  setup_wifi();
  mqttClient.setServer(mqtt_server, mqtt_port);
  // mqttClient.setCallback(callback); // No necesario para el anchor

  // Construir topic MQTT (usando ID numérico para consistencia con Tag)
  sprintf(mqtt_topic, "uwb/range/%u", MY_ANCHOR_UWB_ID);
  Serial.print("Publicando en el topic MQTT: ");
  Serial.println(mqtt_topic);

  // --- Setup UWB ---
  DW3000.begin(); // Inicializa SPI
  DW3000.hardReset();
  delay(200);
  while (!DW3000.checkForIDLE()) { Serial.println("[ERROR] IDLE1 FAILED"); delay(100); }
  DW3000.softReset();
  delay(200);
  if (!DW3000.checkForIDLE()) { Serial.println("[ERROR] IDLE2 FAILED"); delay(100); }

  DW3000.init();      // Inicializa chip y configuraciones básicas
  DW3000.setupGPIO(); // Configura LEDs/GPIO si se usan

  Serial.println("> Anchor MQTT DS-TWR <");
  Serial.print("[INFO] Anchor Label: "); Serial.println(MY_ANCHOR_LABEL);
  Serial.print("[INFO] Anchor UWB ID: "); Serial.println(MY_ANCHOR_UWB_ID);
  Serial.print("[INFO] Coords: (");
  Serial.print(MY_ANCHOR_X); Serial.print(", "); Serial.print(MY_ANCHOR_Y); Serial.print(", "); Serial.print(MY_ANCHOR_Z);
  Serial.println(")");
  Serial.println("[INFO] Setup UWB finalizado.");

  // Configurar parametros UWB basicos
  DW3000.setChannel(5);           // Canal 5 (frecuencia ~6.5GHz)
  DW3000.setPreambleLength(64);   // Longitud de preambulo
  DW3000.setPreambleCode(9);      // Codigo de preambulo 9
  DW3000.setDatarate(0);          // Tasa de datos 110kbps
  DW3000.writeSysConfig();        // Aplicar la configuracion
  // DW3000.enableDoubleBuffering(); // Puede ser util si hay mucho trafico

  // [IMPORTANTE] Configurar la dirección de este Anchor para que el Tag pueda verificarla si quisiera
  // Esto usa la función base, verifica si tu librería la soporta o si necesita otra.
  // DW3000.setDeviceAddress(MY_ANCHOR_UWB_ID); // Opcional, depende de la librería y si el Tag lo usa

  // Empezar escuchando por un frame
  DW3000.clearSystemStatus();
  DW3000.standardRX();
  Serial.println("Esperando primer mensaje del Tag (Stage 0)...");
}

// --- LOOP ---
void loop() {

  // --- Mantener conexión MQTT ---
  if (!mqttClient.connected()) {
    mqtt_reconnect();
  }
  mqttClient.loop(); // Procesar MQTT

  // --- Máquina de Estados UWB ---
  double distancia_calculada = 0.0; // Mover la declaración fuera del switch para evitar errores de salto
  switch (curr_stage) {
    case 0: // Esperar mensaje inicial del Tag (Ping, frame stage 1)
      if (rx_status = DW3000.receivedFrameSucc()) {
        DW3000.clearSystemStatus();

        if (rx_status == 1) { // Recepción OK
          // Obtener ID del Tag (asumiendo que está en el byte 1 del payload)
          // Usar la función base getSenderID() que lee RX_BUFFER_0_REG offset 1
          current_tag_id = DW3000.getSenderID();

          if (DW3000.ds_isErrorFrame()) {
            Serial.println("[WARNING] Error frame detectado! Volviendo a stage 0.");
            curr_stage = 0;
            DW3000.standardRX();
          } else if (DW3000.ds_getStage() != 1) { // Esperamos frame "stage 1"
            Serial.println("[WARNING] Frame inesperado recibido (no stage 1). Volviendo a stage 0.");
            // Podríamos enviar error, pero por simplicidad reiniciamos
            curr_stage = 0;
            DW3000.standardRX();
          } else {
            // Frame inicial correcto recibido
            Serial.print("Stage 0: Recibido frame inicial de Tag ID: "); Serial.println(current_tag_id);
            rx1_ts_ll = DW3000.readRXTimestamp(); // Guardar timestamp RX
            curr_stage = 1; // Pasar a enviar primera respuesta
          }
        } else { // Error en recepción UWB
          Serial.println("[ERROR] Error en recepción UWB (Stage 0)! Abortando ciclo.");
          curr_stage = 0;
          DW3000.standardRX();
        }
      }
      break;

    case 1: // Enviar primera respuesta (Pong, frame stage 2) al Tag 'current_tag_id'
      Serial.print("Stage 1: Enviando primera respuesta (Stage 2) a Tag ID: "); Serial.println(current_tag_id);

      // Establecer ID de destino para el frame UWB
      // *** CORRECCIÓN APLICADA *** Usar setDestinationID
      DW3000.setDestinationID(current_tag_id);
      // Enviar frame estándar DS-TWR stage 2 (sin payload)
      DW3000.ds_sendFrame(2);

      tx1_ts_ll = DW3000.readTXTimestamp(); // Guardar timestamp TX

      // Calcular tiempo de procesamiento de este anchor para la primera respuesta
      t_replyB_ll = tx1_ts_ll - rx1_ts_ll;

      Serial.println("Stage 1: Respuesta enviada. Esperando mensaje final del Tag (Stage 3)...");
      curr_stage = 2; // Pasar a esperar la respuesta final del Tag
      DW3000.standardRX(); // Poner radio en modo recepción
      break;

    case 2: // Esperar mensaje final del Tag (frame stage 3 con tiempos t_roundA, t_replyA)
      if (rx_status = DW3000.receivedFrameSucc()) {
        DW3000.clearSystemStatus();

        if (rx_status == 1) { // Recepción OK
           uint16_t source_id = DW3000.getSenderID(); // Obtener ID del Tag del payload

          // Opcional: Verificar que el mensaje es del mismo Tag con el que empezamos
          if (source_id != current_tag_id) {
              Serial.print("[WARNING] Recibido frame de Tag inesperado (ID: ");
              Serial.print(source_id); Serial.println("). Ignorando.");
              DW3000.standardRX(); // Ignorar y seguir esperando
              break; // Salir de este case y volver a esperar en stage 2
          }

          if (DW3000.ds_isErrorFrame()) {
            Serial.println("[WARNING] Error frame detectado! Volviendo a stage 0.");
            curr_stage = 0;
            DW3000.standardRX();
          }
          // Esperamos el frame "stage 3" que envía el Tag con los datos
          else if (DW3000.ds_getStage() != 3) {
             Serial.println("[WARNING] Frame inesperado recibido (no stage 3). Volviendo a stage 0.");
             curr_stage = 0;
             DW3000.standardRX();
          }
          else {
             // Frame final del Tag recibido correctamente
             Serial.print("Stage 2: Recibido frame final (Stage 3) de Tag ID: "); Serial.println(current_tag_id);
             rx2_ts_ll = DW3000.readRXTimestamp(); // Guardar timestamp RX

             // --- Extraer t_roundA y t_replyA del payload usando la Extensión ---
             byte received_payload[16]; // Buffer para los 16 bytes (8+8)
             int payload_size = DW3000Extension::getReceivedPayloadSize();

             bool times_ok = false;
             if (payload_size >= 16) {
                if (DW3000Extension::getReceivedPayload(received_payload, 16)) {
                  // Reconstruir t_roundA_ll (64 bits)
                  t_roundA_ll = 0; // Limpiar antes de reconstruir
                  for(int i=0; i<8; i++) {
                    t_roundA_ll |= ((uint64_t)received_payload[i] << (56 - i*8));
                  }

                  // Reconstruir t_replyA_ll (64 bits)
                  t_replyA_ll = 0; // Limpiar antes de reconstruir
                   for(int i=0; i<8; i++) {
                    t_replyA_ll |= ((uint64_t)received_payload[i+8] << (56 - i*8));
                  }

                  times_ok = true;
                  Serial.println("[INFO] Tiempos extraídos correctamente del payload.");
                } else {
                  Serial.println("[ERROR] Falló la lectura del payload con la extensión.");
                }
             } else {
                 Serial.print("[ERROR] Payload recibido demasiado corto ("); Serial.print(payload_size); Serial.println(" bytes). Esperaba 16.");
             }

             if (!times_ok) {
                Serial.println("[ERROR] No se pudieron extraer los tiempos del Tag. Volviendo a stage 0.");
                curr_stage = 0;
                DW3000.standardRX();
             } else {
                Serial.print("Stage 2: Recibidos tiempos del Tag: t_roundA="); Serial.print((long)t_roundA_ll);
                Serial.print(", t_replyA="); Serial.println((long)t_replyA_ll);

                // Calcular el t_roundB de este Anchor (tiempo entre enviar resp 1 y recibir resp 2)
                t_roundB_ll = rx2_ts_ll - tx1_ts_ll;

                curr_stage = 3; // Pasar a calcular distancia y publicar
             }
          }
        } else { // Error en recepción UWB
           Serial.println("[ERROR] Error en recepción UWB (Stage 2). Volviendo a stage 0.");
           curr_stage = 0;
           DW3000.standardRX();
        }
      }
      break; // Fin de case 2

    case 3: // Calcular distancia y publicar vía MQTT
      Serial.print("Stage 3: Calculando distancia para Tag ID: "); Serial.println(current_tag_id);
      Serial.print("Usando: t_roundA="); Serial.print((long)t_roundA_ll); Serial.print(", t_replyA="); Serial.print((long)t_replyA_ll);
      Serial.print(", t_roundB="); Serial.print((long)t_roundB_ll); Serial.print(", t_replyB="); Serial.println((long)t_replyB_ll);

      // Calcular distancia usando los 4 tiempos
      distancia_calculada = calculate_distance_ds_twr(t_roundA_ll, t_replyA_ll, t_roundB_ll, t_replyB_ll);

      if (distancia_calculada < 0 || distancia_calculada > 1000.0) { // Chequeo básico de validez (0m a 1km)
        Serial.print("[WARNING] Distancia calculada inválida o fuera de rango: "); Serial.println(distancia_calculada);
      } else {
        Serial.print("Stage 3: Distancia calculada = "); Serial.print(distancia_calculada, 3); Serial.println(" m"); // Imprimir con 3 decimales

        // Obtener timestamp del sistema (milisegundos desde el arranque)
        unsigned long current_millis = millis();

        // Crear el payload JSON para MQTT
        // Incluye timestamp, ID tag, ID numérico anchor, etiqueta anchor, distancia, coords anchor
        snprintf(msg, MSG_BUFFER_SIZE,
                 "{\"ts\":%lu,\"tag\":%u,\"anchor_id\":%u,\"anchor_label\":\"%s\",\"dist\":%.3f,\"anchor_x\":%.2f,\"anchor_y\":%.2f,\"anchor_z\":%.2f}",
                 current_millis,
                 current_tag_id,
                 MY_ANCHOR_UWB_ID,    // ID numérico
                 MY_ANCHOR_LABEL,     // Etiqueta "A", "B", etc.
                 distancia_calculada, // Distancia en metros
                 MY_ANCHOR_X,
                 MY_ANCHOR_Y,
                 MY_ANCHOR_Z
                 );

        // Publicar en MQTT
        Serial.print("Publicando en MQTT (topic "); Serial.print(mqtt_topic); Serial.print("): ");
        Serial.println(msg);
        if (!mqttClient.publish(mqtt_topic, msg)) {
             Serial.println("[ERROR] Falló la publicación MQTT!");
             // Podría haber lógica de reintento aquí si fuera necesario
        }
         lastMsg = millis(); // Registrar tiempo de última publicación
      }

      // Ranging completado, volver a escuchar para el próximo Tag/intento
      Serial.println("Stage 3: Ranging completado. Volviendo a Stage 0 (escuchando)...");
      curr_stage = 0;
      current_tag_id = 0; // Resetear tag ID actual hasta recibir el siguiente
      DW3000.standardRX();
      break; // Fin de case 3

    default:
      Serial.print("[ERROR] Estado desconocido ("); Serial.print(curr_stage); Serial.println("). Volviendo a stage 0");
      curr_stage = 0;
      current_tag_id = 0;
      DW3000.standardRX();
      break;
  } // Fin del switch
} // Fin de loop()