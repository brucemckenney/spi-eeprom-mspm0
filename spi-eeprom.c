///
//  spi-eeprom.c
//  Rev 0.1.1
//  SLAA208 updated for the MSPM0 and SPI
//  Copyright Bruce McKenney 2025
//  BSD 2-clause license
//

//  We don't include "ti_msp_dl_config.h" since main() takes care of all that
#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#include "spi-eeprom.h"

//  SPI commands. Pretty much everyone supports this subset.
#define EEP_READ    0x03
#define EEP_WRITE   0x02
#define EEP_WREN    0x06
#define EEP_RDSR    0x05

//  SR bits
#define EEP_RDSR_WIP 0x01   // Write In Progress

///
//  eep_param block
//  Saved here so they don't need to be provided to each function.
//  This preserves the signatures from SLAA208.
//
typedef struct _eep_params
{
    SPI_Regs *ep_spi;
    GPIO_Regs *ep_cs_port;      // PORTA, e.g.
    unsigned ep_cs_pin;         // GPIO_PIN_4, e.g.
    unsigned ep_curr_addr;      // Pretend we can do EEPROM_ReadCurrent()
#if EEP_DMA
    uint8_t ep_use_dma;         // Shorthand
    uint8_t ep_dma_rx_chanid;   // DMA Provider 1
    uint8_t ep_dma_tx_chanid;   // DMA Provider 2
#endif // EEP_DMA
}eep_params;
eep_params eep_param;
eep_params * const eep = &eep_param;

//  Infinite source (Tx) and infinite sink (Rx)
static uint8_t eep_tx_inf = 0xFF, eep_rx_inf = 0xff;

///
//  InitSPI()
//  spidev should point to a configured SPI unit, e.g. from sysconfig.
//  cs_port:cs_pin describes /CS
//
void
InitSPI(SPI_Regs *spidev, GPIO_Regs *cs_port, unsigned cs_pin)
{
    eep->ep_spi = spidev;
    eep->ep_cs_pin = cs_pin;
    eep->ep_cs_port = cs_port;
#if EEP_DMA
    eep->ep_use_dma = 0;
    eep->ep_dma_rx_chanid =
            eep->ep_dma_tx_chanid = EEP_DMA_NOCHAN;
#endif // EEP_DMA

    return;
}

#if EEP_DMA
///
//  InitSPI_DMA()
//  Same as above, plus a DMA channel.
void
InitSPI_DMA(SPI_Regs *spi, GPIO_Regs *cs_port, unsigned cs_pin, uint8_t rx_chanid, uint8_t tx_chanid)
{
    eep->ep_spi = spi;
    eep->ep_cs_pin = cs_pin;
    eep->ep_cs_port = cs_port;
    //  Caller needs to specify both DMA channels or neither
    if (rx_chanid != EEP_DMA_NOCHAN && tx_chanid != EEP_DMA_NOCHAN)
    {
        eep->ep_use_dma = 1;
        //  Constant settings:
        DL_DMA_setDestAddr(DMA, tx_chanid, (uint32_t)&spi->TXDATA);
        DL_DMA_setSrcAddr(DMA, rx_chanid, (uint32_t)&spi->RXDATA);
    }
    else
    {
        eep->ep_use_dma = 0;
        rx_chanid = EEP_DMA_NOCHAN;
        tx_chanid = EEP_DMA_NOCHAN;
    }
    eep->ep_dma_rx_chanid = rx_chanid;
    eep->ep_dma_tx_chanid = tx_chanid;

    return;
}
#endif // EEP_DMA

//
//  EEPROM_spix()
//  Exchange one Tx byte for one Rx byte.
//
static inline uint8_t
EEPROM_spix(SPI_Regs *spi, uint8_t c)
{
    DL_SPI_transmitDataBlocking8(spi, c);
    c = DL_SPI_receiveDataBlocking8(spi);
    return(c);
}

///
//  EEPROM_cs_on()
//  Assert /CS
//
static inline void
EEPROM_cs_on(void)
{
    DL_GPIO_clearPins(eep->ep_cs_port, eep->ep_cs_pin);
    return;
}

///
//  EEPROM_cs_off()
//  De-assert /CS
//
static inline void
EEPROM_cs_off(void)
{
    DL_GPIO_setPins(eep->ep_cs_port, eep->ep_cs_pin);
    return;
}

///
//  EEPROM_SetAddr()
//  Shorthand: Write the EEPROM memory address as a prefix
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
//  EEPROM_spi_burst()
//
void
EEPROM_spi_burst(SPI_Regs *spi, unsigned char *src, unsigned char *dst, unsigned cnt)
{
    unsigned txincr = 1, rxincr = 1;

    if (src == 0)
    {
        src = &eep_tx_inf;
        txincr = 0;
    }
    if (dst == 0)
    {
        dst = &eep_rx_inf;
        rxincr = 0;
    }
#if EEP_DMA
    if (eep->ep_use_dma)
    {
        //  This code looks bulky, but it distills to not much.
        uint8_t rxchan = eep->ep_dma_rx_chanid;
        uint8_t txchan = eep->ep_dma_tx_chanid;
        uint32_t incr;

        //  Tx side:
        DL_DMA_setSrcAddr(DMA, txchan, (uint32_t)src);
        DL_DMA_setTransferSize(DMA, txchan, cnt);
        incr = (txincr == 0) ? DL_DMA_ADDR_UNCHANGED : DL_DMA_ADDR_INCREMENT;
        DL_DMA_configTransfer(DMA, txchan,
                          DL_DMA_SINGLE_TRANSFER_MODE, DL_DMA_NORMAL_MODE,
                          DL_DMA_WIDTH_BYTE, DL_DMA_WIDTH_BYTE,
                          incr, DL_DMA_ADDR_UNCHANGED);         // Don't incr dest
        DL_SPI_clearDMATransmitEventStatus(spi);                // Clear stale
        DL_SPI_enableDMATransmitEvent(spi);                     // SPI_DMA_TRIG_TX_IMASK_TX_SET

        //  Rx side:
        DL_DMA_setDestAddr(DMA, rxchan, (uint32_t)dst);
        DL_DMA_setTransferSize(DMA, rxchan, cnt);
        incr = (rxincr == 0) ? DL_DMA_ADDR_UNCHANGED : DL_DMA_ADDR_INCREMENT;
        DL_DMA_configTransfer(DMA, rxchan,
                              DL_DMA_SINGLE_TRANSFER_MODE, DL_DMA_NORMAL_MODE,
                              DL_DMA_WIDTH_BYTE, DL_DMA_WIDTH_BYTE,
                              DL_DMA_ADDR_UNCHANGED,  incr);    // don't incr src
        DL_SPI_clearDMAReceiveEventStatus(spi, DL_SPI_DMA_INTERRUPT_RX);    // Clear stale
        DL_SPI_enableDMAReceiveEvent(spi, DL_SPI_DMA_INTERRUPT_RX);

        //  Run the DMA for this burst
        DL_SPI_clearInterruptStatus(spi, DL_SPI_INTERRUPT_DMA_DONE_RX); // Clear termination event
        DL_DMA_enableChannel(DMA, rxchan); // Prime
        DL_DMA_enableChannel(DMA, txchan); // Go

        while (!DL_SPI_getRawInterruptStatus(spi, DL_SPI_INTERRUPT_DMA_DONE_RX)) {/*EMPTY*/;}
    }
    else
    {

#else // EEP_DMA
    //  Byte-by-byte
    while (cnt > 0)
    {
        *dst = EEPROM_spix(spi, *src);
        src += txincr;
        dst += rxincr;
        --cnt;
    }
#endif // EEP_DMA
#if EEP_DMA
    } // end if(use_dma)
#endif // EEP_DMA

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
    unsigned cnt = Size;
    unsigned char *ptr = Data;
    unsigned addr = Address & EEP_ADDRMASK;  // Wrap address as needed

    //  Fill pages until we run out of data.
    while (cnt > 0)
    {
        unsigned fragsiz;
        unsigned pagetop;

        //  See how much can fit into the requested page
        pagetop = (addr + EEP_PAGESIZE) & ~(EEP_PAGESIZE-1); // addr of next page
        fragsiz = pagetop - addr;       // Amount that can fit in this page
        fragsiz &= EEP_ADDRMASK;
        if (fragsiz > cnt)              // Don't go overboard
            fragsiz = cnt;

        // WRite ENable
        EEPROM_cs_on();
        EEPROM_spix(spi, EEP_WREN);
        EEPROM_cs_off();

        //  Send a Write command, followed by the memory address.
        EEPROM_cs_on();
        EEPROM_spix(spi, EEP_WRITE);
        EEPROM_SetAddr(spi, addr);

        //  Do the main work
        EEPROM_spi_burst(spi, ptr, 0, fragsiz);

        EEPROM_cs_off();

        addr += fragsiz;
        addr &= EEP_ADDRMASK;
        ptr += fragsiz;
        cnt -= fragsiz;

        // Wait for EEPROM update to complete (Twr/Tpp)
        EEPROM_AckPolling();
    } // while (cnt > 0)

    eep->ep_curr_addr = addr;   // Save for CurrentAddressRead()

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
    unsigned i;

    //  Send a Read command, followed by the memory address.
    EEPROM_cs_on();
    EEPROM_spix(spi, EEP_READ);
    EEPROM_SetAddr(spi, Address);

    //  Do the main work
    EEPROM_spi_burst(spi, 0, Data, Size);

    EEPROM_cs_off();
    eep->ep_curr_addr = (Address + Size) & EEP_ADDRMASK;

    return;
}

///
//  EEPROM_AckPolling()
//  The EEPROM doesn't respond to much while it's writing its memory
//  (Twr, typically <5ms), so just keep poking it until WIP=0.
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
        (void)EEPROM_spix(spi, EEP_RDSR);       // Read Status Register
        sr = EEPROM_spix(spi, 0xFF);
        EEPROM_cs_off();
        if ((sr & EEP_RDSR_WIP) == 0)
            {OK = 1;}                           // WIP=0 -> OK
    } while (!OK);

    return;
}
