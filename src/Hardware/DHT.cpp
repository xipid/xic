#include <Hardware/DHT.hpp>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace Xi {

void DHTDevice::update() {
#if defined(ARDUINO)
    if (pin < 0) return;
    unsigned long now = millis();
    if (now - lastRead < 2000) return; // Rate limit
    lastRead = now;

    captureData();
#endif
}

#if defined(ARDUINO)
void DHTDevice::captureData() {
    u8 data[5] = {0};
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delay(20);
    digitalWrite(pin, HIGH);
    delayMicroseconds(40);
    pinMode(pin, INPUT_PULLUP);

    // Simple bit-banging capture
    uint32_t t = 10000;
    while(digitalRead(pin) == HIGH) if (--t == 0) return;
    t = 10000;
    while(digitalRead(pin) == LOW) if (--t == 0) return;
    t = 10000;
    while(digitalRead(pin) == HIGH) if (--t == 0) return;

    noInterrupts();
    for (int i=0; i<40; i++) {
        uint32_t tLow = 0;
        while(digitalRead(pin) == LOW) if (++tLow > 10000) break;
        uint32_t tHigh = 0;
        while(digitalRead(pin) == HIGH) if (++tHigh > 10000) break;
        if (tHigh > tLow) data[i/8] |= (1 << (7-(i%8)));
    }
    interrupts();

    if (data[4] == ((data[0]+data[1]+data[2]+data[3]) & 0xFF)) {
         humidity = ((data[0] << 8) | data[1]) * 0.1f;
         float f = ((data[2] & 0x7F) << 8 | data[3]) * 0.1f;
         if (data[2] & 0x80) f = -f;
         temperature = f;
    }
}
#endif

} // namespace Xi
