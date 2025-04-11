/*
 * DW3000 Library Extension
 * Extension for the DW3000 library to add support for custom payloads in frames
 */

#include "DW3000_Extension.h"
#include "Arduino.h"
#include "SPI.h"

// TX Buffer base address is 0x14
#define TX_BUFFER_REG 0x14
// RX Buffer base address is 0x12
#define RX_BUFFER_REG 0x12

/*
 * Send a frame with custom payload data for double-sided ranging
 * @param stage The stage identifier for the DS-TWR protocol (must be between 0-7)
 * @param payload Pointer to the payload data
 * @param payloadSize Size of the payload in bytes
 * @return True if successful, false otherwise
 */
bool DW3000Extension::ds_sendFrameWithData(int stage, byte* payload, int payloadSize) {
    // Set double-sided ranging mode
    DW3000.setMode(1);
    
    // Set sender and destination in TX buffer
    DW3000.write(TX_BUFFER_REG, 0x01, DW3000.getSenderID() & 0xFF);
    DW3000.write(TX_BUFFER_REG, 0x02, DW3000.getDestinationID() & 0xFF);
    DW3000.write(TX_BUFFER_REG, 0x03, stage & 0x7); // Ensure stage is valid
    
    // Write payload data to TX buffer
    // The header takes 4 bytes, payload starts at offset 0x04
    for (int i = 0; i < payloadSize; i++) {
        DW3000.write(TX_BUFFER_REG, 0x04 + i, payload[i]);
    }
    
    // Set frame length to header (4 bytes) + payload size
    DW3000.setFrameLength(4 + payloadSize);
    
    // TX then instantly switch to RX
    DW3000.TXInstantRX();
    
    // Wait for transmission to complete, with timeout
    bool error = true;
    for (int i = 0; i < 50; i++) {
        if (DW3000.sentFrameSucc()) {
            error = false;
            break;
        }
        delay(1); // Small delay for loop
    }
    
    if (error) {
        Serial.println("[ERROR] Could not send frame with payload successfully!");
        return false;
    }
    
    return true;
}

/*
 * Get the size of the payload in the received frame
 * @return Size of the payload in bytes (excluding header)
 */
int DW3000Extension::getReceivedPayloadSize() {
    // Read frame length from RX_FINFO register (base 0x0C, sub 0x00)
    uint32_t frameInfo = DW3000.read(0x0C, 0x00);
    uint16_t frameLength = frameInfo & 0x3FF; // Frame length is in the first 10 bits
    
    // Subtract header size (4 bytes for standard DS-TWR frames)
    if (frameLength >= 4) {
        return frameLength - 4;
    }
    
    return 0; // No payload or invalid frame
}

/*
 * Get the payload from the received frame
 * @param buffer Buffer to store the payload
 * @param bufferSize Size of the provided buffer
 * @return True if successful, false otherwise
 */
bool DW3000Extension::getReceivedPayload(byte* buffer, int bufferSize) {
    int payloadSize = getReceivedPayloadSize();
    
    // Check if buffer is large enough
    if (bufferSize < payloadSize) {
        Serial.println("[ERROR] Buffer too small for payload");
        return false;
    }
    
    // Check if there's any payload
    if (payloadSize <= 0) {
        Serial.println("[WARNING] No payload in frame");
        return false;
    }
    
    // Read payload from RX buffer
    // Payload starts after the header (4 bytes) at offset 0x04
    for (int i = 0; i < payloadSize; i++) {
        buffer[i] = DW3000.read8bit(RX_BUFFER_REG, 0x04 + i);
    }
    
    return true;
}

/*
 * Get the current device timestamp
 * @return Current device timestamp in DW3000 time units (~15.65ps per unit)
 */
unsigned long long DW3000Extension::getDeviceTimestamp() {
    // Read from System Time Counter (SYS_TIME) register
    uint32_t lo = DW3000.read(0x10, 0x00); // Low 32 bits
    uint32_t hi = DW3000.read(0x10, 0x04); // High 32 bits
    
    // Combine to form 64-bit timestamp
    unsigned long long timestamp = ((unsigned long long)hi << 32) | lo;
    
    return timestamp;
}
