#include "TCA9554PWR.h"

static uint8_t s_outputCache = 0x00;

uint8_t Read_REG(uint8_t REG) {
    uint8_t data = 0;
    I2C_Read(TCA9554_ADDRESS, REG, &data, 1);
    return data;
}

uint8_t Write_REG(uint8_t REG, uint8_t Data) {
    return I2C_Write(TCA9554_ADDRESS, REG, &Data, 1) ? 0 : 1;
}

void Mode_EXIO(uint8_t Pin, uint8_t State) {
    uint8_t config = Read_REG(TCA9554_CONFIG_REG);
    if (State) {
        config |= (1 << (Pin - 1));
    } else {
        config &= ~(1 << (Pin - 1));
    }
    Write_REG(TCA9554_CONFIG_REG, config);
}

void Mode_EXIOS(uint8_t PinState) {
    Write_REG(TCA9554_CONFIG_REG, PinState);
}

uint8_t Read_EXIO(uint8_t Pin) {
    uint8_t input = Read_REG(TCA9554_INPUT_REG);
    return (input >> (Pin - 1)) & 0x01;
}

uint8_t Read_EXIOS(uint8_t REG) {
    if (REG == TCA9554_OUTPUT_REG) {
        return s_outputCache;
    }
    return Read_REG(TCA9554_INPUT_REG);
}

void Set_EXIO(uint8_t Pin, uint8_t State) {
    if (State) {
        s_outputCache |= (1 << (Pin - 1));
    } else {
        s_outputCache &= ~(1 << (Pin - 1));
    }
    Write_REG(TCA9554_OUTPUT_REG, s_outputCache);
}

void Set_EXIOS(uint8_t PinState) {
    s_outputCache = PinState;
    Write_REG(TCA9554_OUTPUT_REG, s_outputCache);
}

void Set_Toggle(uint8_t Pin) {
    s_outputCache ^= (1 << (Pin - 1));
    Write_REG(TCA9554_OUTPUT_REG, s_outputCache);
}

void TCA9554PWR_Init(uint8_t PinState) {
    // Configure all pins as input first
    Write_REG(TCA9554_CONFIG_REG, 0xFF);
    
    // Set EXIO4 (SYS_OUT/PWR button) as input
    Mode_EXIO(EXIO_PIN4, 1);
    
    // Set EXIO5 (AXP_IRQ) as input  
    Mode_EXIO(EXIO_PIN5, 1);
    
    // Set EXIO7 (GPS_RST) as output, default LOW
    Mode_EXIO(EXIO_PIN7, 0);
    Set_EXIO(EXIO_PIN7, Low);
    
    // Set EXIO3 (RTC_INT) as input
    Mode_EXIO(EXIO_PIN3, 1);
    
    // Set EXIO6 (QMI_INT1) as input
    Mode_EXIO(EXIO_PIN6, 1);
    
    // Apply output state for output pins
    Write_REG(TCA9554_OUTPUT_REG, s_outputCache);
}
