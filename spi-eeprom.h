//
//  spi-eeprom.h
//  Rev 0.1.0
//  SLAA208 updated for the MSPM0 and SPI
//  Copyright Bruce McKenney 2025
//  BSD 2-Clause license
//
#ifndef SPI_EEPROM_H_
#define SPI_EEPROM_H_ 1

#include <stdint.h>

#define EEP_ADDRBITS   24
#define EEP_ADDRMASK    ((1ul << EEP_ADDRBITS)-1)  // for bits=32, this over-shifts but the result is correct

//typedef uint32_t eep_addr;          // 24-bit addresses
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
extern void InitSPI_DMA(SPI_Regs *spidev, GPIO_Regs *cs_port, unsigned cs_pin, uint8_t rx_chanid, uint8_t tx_chanid);
#define EEP_DMA_NOCHAN  ((uint8_t)-1)
#endif // EEP_DMA

#endif // SPI_EEPROM_H_
