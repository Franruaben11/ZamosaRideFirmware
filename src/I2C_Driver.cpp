#include "I2C_Driver.h"

void I2C_Init(void) {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
}

bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t* Reg_data, uint32_t Length) {
    Wire.beginTransmission(Driver_addr);
    Wire.write(Reg_addr);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    uint32_t read_len = Wire.requestFrom((uint16_t)Driver_addr, (uint8_t)Length);
    if (read_len != Length) {
        return false;
    }

    for (uint32_t i = 0; i < Length; ++i) {
        Reg_data[i] = Wire.read();
    }
    return true;
}

bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t* Reg_data, uint32_t Length) {
    Wire.beginTransmission(Driver_addr);
    Wire.write(Reg_addr);
    Wire.write(Reg_data, Length);
    return Wire.endTransmission() == 0;
}
