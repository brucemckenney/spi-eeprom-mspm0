//
//  i2c-eeprom.h
//
//  Copyright Bruce McKenney 2025
//  BSD 2-Clause license
//
#ifndef SPI_EEPROM_H_
#define SPI_EEPROM_H_ 1

#include <stdint.h>

#define EEP_ADDRBITS   24

typedef uint32_t eep_addr;          // 24-bit addresses
#define EEP_PAGESIZE    16          // Page size (from the data sheet); power of 2
#define EEP_DMA         0

extern void InitSPI(SPI_Regs *spidev, GPIO_Regs *cs_port, unsigned cs_pin);
extern void EEPROM_ByteWrite(unsigned int Address , unsigned char Data);
extern void EEPROM_PageWrite(unsigned int StartAddress , unsigned char * Data , unsigned int Size);
extern unsigned char EEPROM_RandomRead(unsigned int Address);
extern unsigned char EEPROM_CurrentAddressRead(void);
extern void EEPROM_SequentialRead(unsigned int Address , unsigned char * Data , unsigned int Size);
extern void EEPROM_AckPolling(void);
#if EEP_DMA
extern void InitSPI_DMA(SPI_Regs *spidev,  GPIO_Regs *cs_port, unsigned cs_pin, uint8_t chanid);
#define EEP_DMA_NOCHAN  ((uint8_t)-1)
#endif // EEP_DMA

#endif // SPI_EEPROM_H_
