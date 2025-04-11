/*
 * Código Final del Tag (Ping DS-TWR Secuencial) - Adaptado para Anchor MQTT
 * Este Tag cicla entre los Anchors conocidos, realiza ranging DS-TWR modificado
 * y envía sus tiempos (t_roundA, t_replyA) al Anchor.
 * No tiene conexión Wi-Fi ni realiza cálculos de posición.
 */

 #include "DW3000.h"           // Librería base UWB
 #include "DW3000_Extension.h"  // Extensión para payload y timestamp
 #include <stdint.h>           // Para tipos uint16_t, uint64_t
 
 // --- [CONFIGURACIÓN DEL TAG] ---
 const uint16_t MY_TAG_ID = 1; // ID único para ESTE tag (ej. 1, 2, etc.)
 
 // --- IDs NUMÉRICOS de los Anchors con los que intentará comunicar ---
 // ¡¡ASEGÚRATE de que coinciden con los MY_ANCHOR_UWB_ID configurados en cada Anchor!!
 #define NUM_ANCHORS 4
 const uint16_t ANCHOR_IDS[NUM_ANCHORS] = {10, 20, 30, 40};
 int current_anchor_index = 0; // Índice del anchor actual
 
 // --- Variables UWB ---
 static int rx_status;
 static int tx_status;
 
 /* Estados del ranging MODIFICADOS para este Tag:
  * 0 - Espera para iniciar ranging con el anchor actual
  * 1 - Frame inicial (stage 1) enviado; esperando respuesta del Anchor (stage 2)
  * 2 - Respuesta recibida; enviando frame final (stage 3) CON t_roundA, t_replyA
  * 3 - Frame final enviado. Pasar al siguiente anchor.
  */
 static int curr_stage = 0;
 
 // Tiempos calculados por este Tag (Ping) - Usar long long (64 bits)
 static long long t_roundA_ll = 0;
 static long long t_replyA_ll = 0;
 
 // Timestamps UWB brutos (usar long long)
 static long long tx1_ts_ll = 0;
 static long long rx1_ts_ll = 0;
 static long long tx2_ts_ll = 0;
 
 // --- Constantes y Variables Auxiliares ---
 #define INTER_ANCHOR_DELAY_MS 50 // Pausa (ms) entre intentar con diferentes anchors
 unsigned long last_ranging_finish_time = 0;
 
 // Timeout para esperar respuesta de un anchor (ms)
 #define RESPONSE_TIMEOUT_MS 50
 unsigned long response_timer_start = 0;
 
 
 // --- SETUP ---
 void setup() {
   Serial.begin(115200);
   delay(100);
   Serial.println("\n=== UWB Tag Final (Ciclo Secuencial Anchors - Protocolo Modificado) ===");
   Serial.print("[INFO] Tag ID: "); Serial.println(MY_TAG_ID);
   Serial.print("[INFO] Intentando con Anchors (IDs UWB): ");
   for (int i = 0; i < NUM_ANCHORS; i++) {
     Serial.print(ANCHOR_IDS[i]); Serial.print(" ");
   }
   Serial.println();
 
   // --- Inicialización DW3000 ---
   DW3000.begin(); // Inicializa SPI
   DW3000.hardReset();
   delay(200);
   while (!DW3000.checkForIDLE()) { Serial.println("[ERROR] IDLE1 FAILED"); delay(100); }
   DW3000.softReset();
   delay(200);
   if (!DW3000.checkForIDLE()) { Serial.println("[ERROR] IDLE2 FAILED"); delay(100); }
 
   DW3000.init();      // Inicializa chip y configuraciones básicas
   DW3000.setupGPIO(); // Configura LEDs/GPIO si se usan
   Serial.println("[INFO] DW3000 inicializado.");
 
   // Configurar DW3000 para actuar como Tag (TX/RX)
   DW3000.configure(); // Aplicar configuración UWB (canal, preámbulo, etc.)
 
   // Establecer el ID de este Tag que se incluirá en el payload de los frames enviados
   // La función base setSenderID escribe en la variable global que usa la extensión
   DW3000.setSenderID(MY_TAG_ID);
   Serial.print("[INFO] Sender ID (para payload) configurado a: "); Serial.println(MY_TAG_ID);
 
   // [Opcional pero recomendado] Configurar la dirección UWB fuente si la librería lo soporta
   // DW3000.setDeviceAddress(MY_TAG_ID); // Verifica si esta u otra función existe
 
   Serial.println("[INFO] Setup completo. Iniciando ciclo de ranging...");
   last_ranging_finish_time = millis(); // Para empezar el primer ranging pronto
 }
 
 // --- Función para pasar al siguiente anchor y resetear estado ---
 void next_anchor() {
   Serial.print("Fin intento con Anchor ID: "); Serial.println(ANCHOR_IDS[current_anchor_index]);
   current_anchor_index = (current_anchor_index + 1) % NUM_ANCHORS;
   curr_stage = 0; // Listo para iniciar con el siguiente anchor
   last_ranging_finish_time = millis(); // Registrar tiempo para el delay inter-anchor
   Serial.print("--> Pasando a Anchor ID: "); Serial.println(ANCHOR_IDS[current_anchor_index]);
   DW3000.standardRX(); // Asegurarse de estar en modo escucha entre ciclos
 }
 
 // --- LOOP ---
 void loop() {
 
   unsigned long current_millis = millis();
 
   // --- Máquina de Estados UWB ---
   switch (curr_stage) {
     case 0: // Esperar para iniciar ranging con el anchor actual
       // Solo iniciar si ha pasado el delay desde el último intento completo
       if (current_millis - last_ranging_finish_time >= INTER_ANCHOR_DELAY_MS) {
         uint16_t target_anchor_id = ANCHOR_IDS[current_anchor_index];
         Serial.println("-----------------------------------------");
         Serial.print("Stage 0: Iniciando ranging con Anchor ID: "); Serial.println(target_anchor_id);
 
         t_roundA_ll = 0; // Resetear tiempos
         t_replyA_ll = 0;
 
         DW3000.setDestinationID(target_anchor_id); // Establecer a quién va dirigido
         // Enviar frame inicial (stage 1) usando la función base (no necesita payload)
         DW3000.ds_sendFrame(1);
 
         tx1_ts_ll = DW3000.readTXTimestamp(); // Guardar timestamp TX
 
         if (tx1_ts_ll == 0) { // Chequeo básico por si la lectura falló
             Serial.println("[ERROR] Falló lectura de TX Timestamp inicial. Reintentando...");
             last_ranging_finish_time = current_millis; // Añadir delay antes de reintentar
             break; // Salir y reintentar en el siguiente ciclo del loop
         }
 
         curr_stage = 1; // Pasar a esperar la primera respuesta
         response_timer_start = current_millis; // Iniciar temporizador de timeout
       }
       break;
 
     case 1: // Esperar primera respuesta del Anchor actual (frame stage 2)
       // Check Timeout
       if (current_millis - response_timer_start > RESPONSE_TIMEOUT_MS) {
         Serial.println("[TIMEOUT] Stage 1.");
         next_anchor(); // Pasar al siguiente anchor
         break;
       }
 
       if (rx_status = DW3000.receivedFrameSucc()) {
         DW3000.clearSystemStatus(); // Limpiar flags
 
         if (rx_status == 1) { // Recepción OK
           // Obtener ID del emisor (Anchor) del payload
           uint16_t source_anchor_id = DW3000.getSenderID();
           uint16_t target_anchor_id = ANCHOR_IDS[current_anchor_index];
 
           // Verificar que es del Anchor esperado
           if (source_anchor_id != target_anchor_id) {
             Serial.print("[INFO] Ignorando frame de Anchor inesperado (ID: ");
             Serial.print(source_anchor_id); Serial.print(") mientras esperaba a "); Serial.println(target_anchor_id);
             DW3000.standardRX(); // Volver a escuchar
             break; // Seguir esperando respuesta del correcto (o timeout)
           }
 
           if (DW3000.ds_isErrorFrame()) {
             Serial.println("[WARNING] Recibido Error frame (Stage 1).");
             next_anchor(); // Pasar al siguiente
           } else if (DW3000.ds_getStage() != 2) { // Esperamos frame tipo "stage 2"
             Serial.print("[WARNING] Recibido frame inesperado (no stage 2, sino "); Serial.print(DW3000.ds_getStage()); Serial.println(").");
             next_anchor(); // Pasar al siguiente
           } else {
             // Frame correcto recibido del Anchor actual
             Serial.print("Stage 1: Recibido frame (Stage 2) de Anchor ID: "); Serial.println(source_anchor_id);
             rx1_ts_ll = DW3000.readRXTimestamp(); // Guardar timestamp RX
 
             if (rx1_ts_ll == 0) {
                  Serial.println("[ERROR] Falló lectura de RX Timestamp. Abortando ciclo.");
                  next_anchor();
                  break;
             }
 
             // Calcular tiempo de ida y vuelta para la primera parte
             t_roundA_ll = rx1_ts_ll - tx1_ts_ll;
 
             curr_stage = 2; // Pasar a enviar el segundo frame con nuestros tiempos
           }
         } else { // Error en recepción UWB
           Serial.println("[ERROR] Error recepción UWB (Stage 1).");
           next_anchor(); // Pasar al siguiente
           DW3000.standardRX();
         }
       }
       // Si no se recibió nada, seguir esperando (hasta timeout)
       break;
 
     case 2: // Enviar segundo frame (stage 3) CON nuestros tiempos (t_roundA, t_replyA) al anchor actual
       { // Scope local para variables
         uint16_t target_anchor_id = ANCHOR_IDS[current_anchor_index];
         Serial.print("Stage 2: Enviando frame final (Stage 3) con tiempos a Anchor ID: "); Serial.println(target_anchor_id);
 
         // Estimar t_replyA usando el timestamp actual del dispositivo ANTES de enviar
         // Usamos la función de la Extensión
         long long estimated_tx_start_ts = DW3000Extension::getDeviceTimestamp();
         if (estimated_tx_start_ts == 0 || estimated_tx_start_ts < rx1_ts_ll) { // Chequeo básico
              Serial.println("[ERROR] Falló lectura de Device Timestamp para t_replyA. Abortando ciclo.");
              next_anchor();
              break;
         }
         t_replyA_ll = estimated_tx_start_ts - rx1_ts_ll;
 
         // Crear el payload de 16 bytes (8 para t_roundA_ll, 8 para t_replyA_ll)
         byte payload[16];
 
         // Empaquetar t_roundA_ll (64 bits) en los primeros 8 bytes (Big Endian)
         for(int i=0; i<8; i++) {
             payload[i] = (byte)(t_roundA_ll >> (56 - i*8));
         }
 
         // Empaquetar t_replyA_ll (64 bits) en los siguientes 8 bytes (Big Endian)
          for(int i=0; i<8; i++) {
             payload[i+8] = (byte)(t_replyA_ll >> (56 - i*8));
         }
 
         // Debug: Imprimir los tiempos que se van a enviar
         Serial.print("       -> Enviando Payload: t_roundA="); Serial.print((long)t_roundA_ll);
         Serial.print(", t_replyA="); Serial.println((long)t_replyA_ll); // Este es el estimado
 
         // Enviar el frame usando la función de la Extensión
         DW3000.setDestinationID(target_anchor_id);
         bool sent_ok = DW3000Extension::ds_sendFrameWithData(3, payload, sizeof(payload)); // Enviamos Stage 3
 
         if (sent_ok) {
           tx2_ts_ll = DW3000.readTXTimestamp(); // Guardar timestamp TX REAL
           long long real_t_replyA = tx2_ts_ll - rx1_ts_ll; // Calcular t_replyA real para debug
 
           Serial.print("Stage 2: Frame final enviado. t_replyA real = "); Serial.println((long)real_t_replyA);
           // Podrías comparar 'real_t_replyA' con 't_replyA_ll' si quieres ver la precisión de la estimación
 
           curr_stage = 3; // Pasar a estado final de este ciclo
         } else {
           Serial.println("[ERROR] Falló envío frame final (Stage 2).");
           next_anchor(); // Pasar al siguiente
         }
       } // Fin scope local
       break;
 
     case 3: // Fin del ciclo de ranging con el anchor actual
       // El trabajo del Tag para este anchor está hecho (envió sus tiempos).
       // Pasamos al siguiente anchor llamando a next_anchor().
       next_anchor();
       break;
 
     default:
       Serial.print("[ERROR] Estado desconocido ("); Serial.print(curr_stage); Serial.println("). Reseteando ciclo.");
       current_anchor_index = 0; // Reiniciar desde el primer anchor
       curr_stage = 0;
       last_ranging_finish_time = millis();
       break;
   } // Fin del switch
 
 } // Fin de loop()