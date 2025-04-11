/*
 * DW3000 Library Extension
 * Extension for the DW3000 library to add support for custom payloads in frames
 */

#ifndef _DW3000_EXTENSION_H_INCLUDED
#define _DW3000_EXTENSION_H_INCLUDED

#include "DW3000.h"

/*
 * This class extends the functionalities of the DW3000 library to support
 * custom payloads in frames for the DS-TWR protocol.
 */
class DW3000Extension {
  public:
    // Send a frame with custom payload data
    static bool ds_sendFrameWithData(int stage, byte* payload, int payloadSize);
    
    // Get the payload size of the received frame
    static int getReceivedPayloadSize();
    
    // Get the payload data from the received frame
    static bool getReceivedPayload(byte* buffer, int bufferSize);
    
    // Helper function to get device current timestamp
    static unsigned long long getDeviceTimestamp();
};

#endif // _DW3000_EXTENSION_H_INCLUDED
