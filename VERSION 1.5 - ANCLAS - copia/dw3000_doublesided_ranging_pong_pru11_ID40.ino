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
static int ID_PONG = 40;
void setup()
{
  Serial.begin(2000000); // Init Serial
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
            Serial.println("[WARNING] Error frame detected! Reverting back to stage 0.");
            curr_stage = 0;
            DW3000.standardRX();}
          else if(DW3000.getDestinationID() != ID_PONG){
            //Serial.println("No es mi destino, ignoro y sigo escuchando");
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
    DW3000.setDestinationID(ID_PONG);
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
            Serial.println("[WARNING] Error frame detected! Reverting back to stage 0.");
            curr_stage = 0;
            DW3000.standardRX();
          }else if(DW3000.getDestinationID() != ID_PONG){
            Serial.println("No es mi destino, ignoro y sigo escuchando");
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
      //DW3000.setDestinationID(ID_PONG);
       Serial.print("El destino q envio al case 3 es :");
      Serial.println(DW3000.getDestinationID2());
      DW3000.ds_sendRTInfo(t_roundB, t_replyB);

      curr_stage = 0;
      DW3000.standardRX();  // Volver a modo de recepción para escuchar nuevos mensajes
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