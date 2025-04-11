#include "DW3000.h"

/*
   BE AWARE: Baud Rate got changed to 2.000.000!

   Approach based on the application note APS011 ("SOURCES OF ERROR IN DW1000 BASED
   TWO-WAY RANGING (TWR) SCHEMES")

   see chapter 2.4 figure 6 and the corresponding description for more information

   This approach tackles the problem of a big clock offset between the ping and pong side
   by reducing the clock offset to a minimum.

   This approach is a more advanced version of the classical ping and pong with timestamp examples.
*/

static int frame_buffer = 0; // Variable to store the transmitted message
static int rx_status; // Variable to store the current status of the receiver operation
static int tx_status; // Variable to store the current status of the receiver operation

/*
   valid stages:
   0 - default stage; await ranging
   1 - ranging received; sending response
   2 - response sent; await second response
   3 - second response received; sending information frame
   4 - information frame sent
*/
static int curr_stage = 0;

static int t_roundB = 0;
static int t_replyB = 0;

static long long rx = 0;
static long long tx = 0;

// Identificador único para este anchor (cambiar para cada dispositivo)
#define ANCHOR_ID 1  // Cambia a 2, 3 o 4 según corresponda

// Modificación para soportar múltiples tags
#define NUM_TAGS 2
static int VALID_TAG_IDS[NUM_TAGS] = {1, 2}; // IDs de los tags: Tag 1 y Tag 2

// Función para verificar si un ID de tag es válido
bool isValidTagID(int tagID) {
  for (int i = 0; i < NUM_TAGS; i++) {
    if (tagID == VALID_TAG_IDS[i]) {
      return true;
    }
  }
  return false;
}

// Función para verificar si este mensaje es para nosotros
bool isForThisAnchor(int srcID, int destID) {
  // Verificar si el mensaje viene de un tag válido
  bool validTag = isValidTagID(srcID);
  
  // Verificar si el destino es este anchor o es broadcast (0)
  bool correctDest = (destID == ANCHOR_ID) || (destID == 0);
  
  return validTag && correctDest;
}

// Función para obtener el ID de origen del mensaje actual
int getSenderID() {
  // En la biblioteca DW3000, el ID del remitente está en el campo SOURCE_ID
  return DW3000.getSenderID();
}

// Función para depuración: imprime detalles de mensaje
void printMessageDetails() {
  Serial.print("Tag ID: ");
  Serial.print(getSenderID());
  Serial.print(", Destino: ");
  Serial.print(DW3000.getDestinationID());
  Serial.print(", Stage: ");
  Serial.println(DW3000.ds_getStage());
}

// Debug flag 
#define DEBUG_RANGING false  // Cambiar a true para activar mensajes de depuración

void setup()
{
  Serial.begin(115200); // Init Serial con velocidad estándar
  DW3000.begin(); // Init SPI
  DW3000.hardReset(); // hard reset in case that the chip wasn't disconnected from power
  delay(200); // Wait for DW3000 chip to wake up
  while (!DW3000.checkForIDLE()) // Make sure that chip is in IDLE before continuing
  {
    Serial.println("[ERROR] IDLE1 FAILED\r");
    while (100);
  }
  DW3000.softReset(); // Reset in case that the chip wasn't disconnected from power
  delay(200); // Wait for DW3000 chip to wake up

  if (!DW3000.checkForIDLE())
  {
    Serial.println("[ERROR] IDLE2 FAILED\r");
    while (100);
  }

  DW3000.init(); // Initialize chip (write default values, calibration, etc.)
  DW3000.setupGPIO(); //Setup the DW3000s GPIO pins for use of LEDs

  Serial.println("> double-sided PONG with timestamp example <\n");
  Serial.print("[INFO] Este es el ANCHOR ID: ");
  Serial.println(ANCHOR_ID);

  Serial.println("[INFO] Setup finished.");

  DW3000.configureAsTX(); // Configure basic settings for frame transmitting

  DW3000.clearSystemStatus();

  DW3000.standardRX();
}

void loop()
{
  switch (curr_stage) {
    case 0:  // Await ranging.
      t_roundB = 0;
      t_replyB = 0;

      if (rx_status = DW3000.receivedFrameSucc()) {
    
    //Serial.print("Destination ");
    //Serial.println(DW3000.getDestinationID());

    //Serial.print("Status");
    //Serial.println(rx_status);
    //if(DW3000.getDestinationID()!=ID_PONG){curr_stage=0;break;}
    //&& (DW3000.getDestinationID()==ID_PONG) 

        DW3000.clearSystemStatus();
        if ( (rx_status == 1) ) { // If frame reception was successful
          if (DW3000.ds_isErrorFrame()) {
            Serial.print("Tag ");
            Serial.print(getSenderID());
            Serial.println(": [WARNING] Error Frame sent. Reverting back to stage 0.");
            curr_stage = 0;
            DW3000.standardRX();
          }
          else if(!isForThisAnchor(getSenderID(), DW3000.getDestinationID())){
            // Este mensaje no es para nosotros, lo ignoramos
            if (DW3000.getDestinationID() != 0) {  // Solo imprimir para mensajes no broadcast
              Serial.print("Ignorando mensaje de Tag ");
              Serial.print(getSenderID());
              Serial.print(" para Anchor ");
              Serial.println(DW3000.getDestinationID());
            }
            DW3000.standardRX();
            break;
          }
          else if (DW3000.ds_getStage() != 1) {
            DW3000.ds_sendErrorFrame();
            DW3000.standardRX();
            curr_stage = 0;
          }
          else {
            curr_stage = 1;
            //Serial.println("Es mi destino,paso al siguiente stage");
          }
        } else // if rx_status returns error (2)
        {
          Serial.println("[ERROR] Receiver Error occured! Aborting event.");
          DW3000.clearSystemStatus();
        }
      }
      break;
    case 1:  // Ranging received. Sending response.
    //if(DW3000.getDestinationID()!= ID_PONG){curr_stage=0;break;}
    DW3000.setDestinationID(DW3000.getDestinationID());
      DW3000.ds_sendFrame(2);

      rx = DW3000.readRXTimestamp();
      tx = DW3000.readTXTimestamp();

      t_replyB = tx - rx;
      curr_stage = 2;
      break;
    case 2:  // Awaiting response.
      if (rx_status = DW3000.receivedFrameSucc()) {
        DW3000.clearSystemStatus();
        if (rx_status == 1) { // If frame reception was successful
          if (DW3000.ds_isErrorFrame()) {
            Serial.print("Tag ");
            Serial.print(getSenderID());
            Serial.println(": [WARNING] Error Frame sent. Reverting back to stage 0.");
            curr_stage = 0;
            DW3000.standardRX();
          }else if(!isForThisAnchor(getSenderID(), DW3000.getDestinationID())){
            Serial.print("Ignorando mensaje de Tag ");
            Serial.print(getSenderID());
            Serial.print(" para Anchor ");
            Serial.println(DW3000.getDestinationID());
            curr_stage=4;
            break;
            }
           else if (DW3000.ds_getStage() != 3) {
            DW3000.ds_sendErrorFrame();
            DW3000.standardRX();
            curr_stage = 0;
          }
          else {
            curr_stage = 3;
            //Serial.println("Es mi destino,paso al siguiente stage");
          }
        } else // if rx_status returns error (2)
        {
          Serial.println("[ERROR] Receiver Error occured! Aborting event.");
          DW3000.clearSystemStatus();
        }
      }
      break;
    case 3:  // Second response received. Sending information frame.
      rx = DW3000.readRXTimestamp();
      t_roundB = rx - tx;
      DW3000.setDestinationID(DW3000.getDestinationID());
       Serial.print("El destino q envio al case 3 es :");
      Serial.println(DW3000.getDestinationID());
      DW3000.ds_sendRTInfo(t_roundB, t_replyB);

      curr_stage = 0;
      break;

      case 4:
      Serial.print("Llegue al case 4 de pong");
      curr_stage = 0;
      DW3000.standardRX();
      break;

    default:
      Serial.print("[ERROR] Entered unknown stage (");
      Serial.print(curr_stage);
      Serial.println("). Reverting back to stage 0");

      curr_stage = 0;
      DW3000.standardRX();
      break;
  }
}