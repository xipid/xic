#include <Hardware/GPS.hpp>
#include <cstring>
#include <cstdlib>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace Xi {

void GPSDevice::update() {
#if defined(ARDUINO)
    if (!port) return;
    while (port->available()) {
        char c = port->read();
        if (c == '\n') {
            gpsBuf[gpsIdx] = 0;
            parseNMEA(gpsBuf);
            gpsIdx = 0;
        } else if (gpsIdx < 88) {
            gpsBuf[gpsIdx++] = c;
        }
    }
#endif
}

void GPSDevice::onPPS() {
    // Handle Pulse-Per-Second signal for nanosecond sync
    // In a real implementation, this would reset sub-second counters
    lastPPS = lastTimeSync; // Simplified
}

void GPSDevice::parseNMEA(const char *buf) {
    if (strncmp(buf, "$GP", 3) == 0 && strstr(buf, "GGA")) {
         char lt[15] = {}, lg[15] = {}, al[10] = {};
         char ns = 'N', ew = 'E';
         int cm = 0;
         for (const char *p = buf; *p; p++) {
            if (*p == ',') { cm++; continue; }
            if (cm == 2) strncat(lt, p, 1);
            if (cm == 3) ns = *p;
            if (cm == 4) strncat(lg, p, 1);
            if (cm == 5) ew = *p;
            if (cm == 9) strncat(al, p, 1);
            if (cm > 9) break;
         }
         if (lt[0] && lg[0]) {
            f32 t = atof(lt), g = atof(lg);
            f32 newLat = (int)(t/100) + (t - ((int)(t/100)*100))/60.0f;
            f32 newLng = (int)(g/100) + (g - ((int)(g/100)*100))/60.0f;
            if (ns == 'S') newLat = -newLat;
            if (ew == 'W') newLng = -newLng;
            pos.lat = newLat;
            pos.lng = newLng;
            pos.alt = atof(al);
            hasFix = true;
         }
    }
}

} // namespace Xi
