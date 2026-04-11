#include "../../include/Hardware/GPS.hpp"
#include "../../include/Xi/Time.hpp"

namespace Xi {

UbloxGPS::UbloxGPS(int pinRX, int pinTX, int pps) : _rx(pinRX), _tx(pinTX), _pps(pps) {
    name = "UbloxGPS";
}

void UbloxGPS::update() {
    // 1. Simulate or perform serial read (placeholder for actual hardware)
    // In actual implementation, we'd use bit-banging or UART to read NMEA/UBX
    
    // Simulate fix for demo
    hasFix = true;
    
    // London coordinates (example)
    pos.x = 51.5074f;
    pos.y = -0.1278f;
    pos.z = 25.0f;
    
    // Sync time
    lastTimeSync = Xi::epochMicros();
}

} // namespace Xi
