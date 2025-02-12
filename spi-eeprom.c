///
//  i2c-eeprom.c
//  SLAA208 updated for the MSPM0
//  Copyright Bruce McKenney 2025
//  BSD 2-clause license
//

//  We don't include "ti_msp_dl_config.h" since main() takes care of all that
#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#include "spi-eeprom.h"


#define EEP_READ    0x03
#define EEP_WRITE   0x02
#define EEP_WREN    0x06
#define EEP_RDSR    0x05

#define EEP_RDSR_WIP 0x01

///
//  eep_param block
//  Saved here so they don't need to be provided to each function.
//  This preserves the signatures from SLAA208.
//
typedef struct _eep_params
{
    SPI_Regs *ep_spi;
    GPIO_Regs *ep_cs_port;  // PORTA, e.g.
    unsigned ep_cs_pin;
    eep_addr ep_curr_addr;
#if EEP_DMA
    uint8_t ep_dma_chanid;
#endif // EEP_DMA
}eep_params;
eep_params eep_param;
eep_params * const eep = &eep_param;

///
//  InitSPI()
//  spidev should point to a configured SPI unit, e.g. from sysconfig.
//  eeprom_cs is 32 bits: 00pp00bb, where pp=port index (PORTA=0) and bb is pin bit number.
//
void
InitSPI(SPI_Regs *spidev, GPIO_Regs *cs_port, unsigned cs_pin)
{
    eep->ep_spi = spidev;
    eep->ep_cs_pin = cs_pin;
    eep->ep_cs_port = cs_port;
#if EEP_DMA
    eep->ep_dma_chanid = EEP_DMA_NOCHAN;
#endif // EEP_DMA

    return;
}

#if EEP_DMA
void
InitSPI_DMA(SPI_Regs *spidev, GPIO_Regs *cs_port, unsigned cs_pin, uint8_t chanid)
{
    eep->ep_spi = spidev;
    eep->ep_cs_pin = cs_pin;
    eep->ep_cs_port = cs_port;
    eep->ep_dma_chanid = chanid;
    if (chanid != EEP_DMA_NOCHAN)
    {
        // Clear out whatever Sysconfig set
        DL_I2C_disableDMAEvent(i2c, DL_I2C_EVENT_ROUTE_1,    // DMA_TRIG1
                           (DL_I2C_DMA_INTERRUPT_TARGET_RXFIFO_TRIGGER|DL_I2C_DMA_INTERRUPT_TARGET_RXFIFO_TRIGGER));
    }

    return;
}
#endif // EEP_DMA

static inline uint8_t
EEPROM_spix(SPI_Regs *spi, uint8_t c)
{
    DL_SPI_transmitDataBlocking8(spi, c);
    c = DL_SPI_receiveDataBlocking8(spi);
    return(c);
}

static inline void
EEPROM_cs_on(void)
{
    DL_GPIO_clearPins(eep->ep_cs_port, eep->ep_cs_pin);
    return;
}
static inline void
EEPROM_cs_off(void)
{
    DL_GPIO_setPins(eep->ep_cs_port, eep->ep_cs_pin);
    return;
}
///
//  EEPROM_SetAddr()
//  Shorthand: Stuff the EEPROM memory address into the Tx FIFO as a prefix
//
static inline void
EEPROM_SetAddr(SPI_Regs *spi, unsigned int addr)
{
    if (EEP_ADDRBITS > 24)
        (void)EEPROM_spix(spi, (addr >> 24) & 0xFF);
    if (EEP_ADDRBITS > 16)
        (void)EEPROM_spix(spi, (addr >> 16) & 0xFF);
    if (EEP_ADDRBITS >  8)
        (void)EEPROM_spix(spi, (addr >>  8) & 0xFF);

    EEPROM_spix(spi, (addr >> 0) & 0xFF);

    return;
}


///
//  EEPROM_ByteWrite()
//
void
EEPROM_ByteWrite(unsigned int Address , unsigned char Data)
{
    //  A Byte Write is just a 1-byte Page Write
    unsigned char dat[1];

    dat[0] = Data;
    EEPROM_PageWrite(Address, &dat[0], 1);

    return;
}

///
//  EEPROM_PageWrite()
//
void
EEPROM_PageWrite(unsigned int Address , unsigned char * Data , unsigned int Size)
{
    SPI_Regs *spi = eep->ep_spi;
#if EEP_DMA
    uint8_t dmachan = eep->ep_dma_chanid;
#endif // EEP_DMA

    unsigned cnt = Size;
    unsigned char *ptr = Data;
    eep_addr addr = (eep_addr)Address;  // Wrap address as needed

#if EEP_DMA
    if (dmachan != EEP_DMA_NOCHAN)
    {
        DL_DMA_setDestAddr(DMA, dmachan, (uint32_t)&i2c->MASTER.MTXDATA);
        DL_I2C_enableDMAEvent(i2c, DL_I2C_EVENT_ROUTE_1, DL_I2C_DMA_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER); // DMA_TRIG1
        DL_DMA_configTransfer(DMA, dmachan,
                              DL_DMA_SINGLE_TRANSFER_MODE, DL_DMA_NORMAL_MODE,
                              DL_DMA_WIDTH_BYTE, DL_DMA_WIDTH_BYTE,
                              DL_DMA_ADDR_INCREMENT, DL_DMA_ADDR_UNCHANGED);// inc src, not dest
   }
#endif // EEP_DMA
    //  Fill pages until we run out of data.
    while (cnt > 0)
    {
        unsigned fragsiz, fragcnt;
        eep_addr pagetop;

        //  See how much can fit into the requested page
        pagetop = (addr + EEP_PAGESIZE) & ~(EEP_PAGESIZE-1); // addr of next page
        fragsiz = pagetop - addr;       // Amount that can fit in this page
        if (fragsiz > cnt)              // Don't go overboard
            fragsiz = cnt;
        fragcnt = fragsiz;              // Prepare to count

        EEPROM_cs_on();
        EEPROM_spix(spi, EEP_WREN);
        EEPROM_cs_off();

        //  Stuff the EEPROM memory address into the Tx FIFO as a prefix
        EEPROM_cs_on();
        EEPROM_spix(spi, EEP_WRITE);
        EEPROM_SetAddr(spi, addr);

#if EEP_DMA
        if (dmachan != EEP_DMA_NOCHAN)
        {
            DL_DMA_setSrcAddr(DMA, dmachan, (uint32_t)ptr);
            DL_DMA_setTransferSize(DMA, dmachan, fragsiz); // don't count what's already in the FIFO
            DL_I2C_clearDMAEvent(i2c, DL_I2C_EVENT_ROUTE_1, DL_I2C_DMA_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
            DL_DMA_enableChannel(DMA, dmachan); // Prime
        }
#endif // EEP_DMA
        while (fragcnt > 0)
        {
            EEPROM_spix(spi, *ptr);
            ++ptr;
            --fragcnt;
        }
        EEPROM_cs_off();

        addr += fragsiz;
        cnt -= fragsiz;
#if EEP_DMA
        if (dmachan != EEP_DMA_NOCHAN)
        {
            ptr += fragsiz;
        }
#endif // EEP_DMA

        // Wait for EEPROM update to complete (Twr)
        EEPROM_AckPolling();
    } // while (cnt > 0)
    eep->ep_curr_addr = addr;

    //  Only needed in case of an error
#if EEP_DMA
    if (dmachan != EEP_DMA_NOCHAN)
    {
        DL_DMA_disableChannel(DMA, dmachan);
    }
#endif // EEP_DMA

    return;
}

///
//  EEPROM_RandomRead()
//
unsigned char
EEPROM_RandomRead(unsigned int Address)
{
    //  A RandomRead is just a 1-byte SequentialRead
    uint8_t dat[1];

    EEPROM_SequentialRead(Address, &dat[0], 1);

    return(dat[0]);
}

///
//  EEPROM_CurrentAddressRead()
//  Doesn't mean much for an SPI EEPROM, but we maintain the illusion.
//
unsigned char
EEPROM_CurrentAddressRead(void)
{
    unsigned char dat[1];
    EEPROM_SequentialRead(eep->ep_curr_addr, &dat[0], sizeof(dat));
    return(dat[0]);
}

///
//  EEPROM_SequentialRead()
//
void
EEPROM_SequentialRead(unsigned int Address , unsigned char * Data , unsigned int Size)
{
    SPI_Regs *spi = eep->ep_spi;
#if EEP_DMA
    uint8_t dmachan = eep->ep_dma_chanid;
#endif // EEP_DMA
    unsigned i;

    // Insert the address in the Tx FIFO as a prefix
    EEPROM_cs_on();
    EEPROM_spix(spi, EEP_READ);
    EEPROM_SetAddr(spi, Address);

#if EEP_DMA
    if (dmachan != EEP_DMA_NOCHAN)
    {
        DL_DMA_setSrcAddr(DMA, dmachan, (uint32_t)&i2c->MASTER.MRXDATA);
        DL_DMA_setDestAddr(DMA, dmachan, (uint32_t)Data);
        DL_DMA_setTransferSize(DMA, dmachan, Size); // don't count what's in the Tx FIFO
        DL_DMA_configTransfer(DMA, dmachan,
                              DL_DMA_SINGLE_TRANSFER_MODE, DL_DMA_NORMAL_MODE,
                              DL_DMA_WIDTH_BYTE, DL_DMA_WIDTH_BYTE,
                              DL_DMA_ADDR_UNCHANGED,  DL_DMA_ADDR_INCREMENT);// inc dest, not src
        DL_I2C_clearDMAEvent(i2c, DL_I2C_EVENT_ROUTE_1, DL_I2C_DMA_INTERRUPT_CONTROLLER_RXFIFO_TRIGGER);
        DL_I2C_enableDMAEvent(i2c, DL_I2C_EVENT_ROUTE_1, DL_I2C_DMA_INTERRUPT_CONTROLLER_RXFIFO_TRIGGER);
        DL_DMA_enableChannel(DMA, dmachan); // Prime
    }
#endif // EEP_DMA

    for (i = 0 ; i < Size ; ++i)
    {
        Data[i] = EEPROM_spix(spi, 0xFF);
    }
    EEPROM_cs_off();
    eep->ep_curr_addr = Address + Size;
#if EEP_DMA
    if (dmachan != EEP_DMA_NOCHAN)
    {
        DL_DMA_disableChannel(DMA, dmachan);
    }
#endif // EEP_DMA

    return;
}

///
//  EEPROM_AckPolling()
//  The EEPROM doesn't respond to much while it's writing its memory
//  (Twr, typically <5ms), so just keep poking it until it WIP=0.
//
void
EEPROM_AckPolling(void)
{
    SPI_Regs *spi = eep->ep_spi;
    unsigned OK = 0;
    do
    {
        uint8_t sr;
        EEPROM_cs_on();
        (void)EEPROM_spix(spi, EEP_RDSR);
        sr = EEPROM_spix(spi, 0xFF);
        EEPROM_cs_off();
        if ((sr & EEP_RDSR_WIP) == 0)
            {OK = 1;}                                           // No error -> OK
    } while (!OK);

    return;
}
