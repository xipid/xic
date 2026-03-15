#include <Hardware/MPU.hpp>

#if defined(ARDUINO)
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#endif

namespace Xi {

void MPU9250::init() {
#if defined(ARDUINO)
    if (useSPI) {
        pinMode(cs, OUTPUT);
        digitalWrite(cs, HIGH);
        SPI.begin();
    } else {
        Wire.begin();
    }
    writeReg(0x6B, 0x80); // reset
    delay(100);
    writeReg(0x6B, 0x00); // wake
    delay(50);
    writeReg(0x1C, 0x00); // accel ±2g
    writeReg(0x1B, 0x00); // gyro ±250°/s
    writeReg(0x6A, 0x00); // disable master
    writeReg(0x37, 0x02); // bypass for AK8963
    
    // AK8963 init
    writeReg0xC(0x0A, 0x16); // 16-bit, 100Hz
#endif
}

void MPU9250::update() {
#if defined(ARDUINO)
    u8 b[14];
    readRegs(0x3B, b, 14);
    accel.x = (int16_t)(b[0] << 8 | b[1]) / 16384.0f;
    accel.y = (int16_t)(b[2] << 8 | b[3]) / 16384.0f;
    accel.z = (int16_t)(b[4] << 8 | b[5]) / 16384.0f;
    gyro.x = (int16_t)(b[8] << 8 | b[9]) / 131.0f;
    gyro.y = (int16_t)(b[10] << 8 | b[11]) / 131.0f;
    gyro.z = (int16_t)(b[12] << 8 | b[13]) / 131.0f;

    u8 m[7];
    readRegs0xC(0x03, m, 7);
    if ((m[6] & 0x08) == 0) {
        mag.x = (int16_t)(m[1] << 8 | m[0]) * 0.15f;
        mag.y = (int16_t)(m[3] << 8 | m[2]) * 0.15f;
        mag.z = (int16_t)(m[5] << 8 | m[4]) * -0.15f;
    }
#endif
}

#if defined(ARDUINO)
void MPU9250::writeReg(u8 reg, u8 val) {
    if (useSPI) {
        digitalWrite(cs, LOW);
        SPI.transfer(reg & 0x7F);
        SPI.transfer(val);
        digitalWrite(cs, HIGH);
    } else {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }
}

void MPU9250::readRegs(u8 reg, u8 *buf, int len) {
    if (useSPI) {
        digitalWrite(cs, LOW);
        SPI.transfer(reg | 0x80);
        for (int i = 0; i < len; i++) buf[i] = SPI.transfer(0);
        digitalWrite(cs, HIGH);
    } else {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        Wire.endTransmission(false);
        Wire.requestFrom((int)addr, len);
        for (int i = 0; i < len; i++) buf[i] = Wire.available() ? Wire.read() : 0;
    }
}

void MPU9250::writeReg0xC(u8 reg, u8 val) {
    Wire.beginTransmission(0x0C);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

void MPU9250::readRegs0xC(u8 reg, u8 *buf, int len) {
    Wire.beginTransmission(0x0C);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(0x0C, len);
    for (int i = 0; i < len; i++) buf[i] = Wire.available() ? Wire.read() : 0;
}
#endif

} // namespace Xi
