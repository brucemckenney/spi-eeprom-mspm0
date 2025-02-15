## Example Summary

This is an update of TI AppNote SLAA208 for the MSPM0 series. 
It provides a simple interface to read and write an SPI EEPROM, such as the M95P32.

The function signatures are (almost) the same as those in SLAA208. 
The only visible difference from the AppNote is that InitSPI requires different arguments:
1. a (configured) SPI device (SPI_Regs *)
2. the GPIO port (GPIO_Regs *) for the Chip Select (/CS) pin
3. the GPIO pin (GPIO_PIN_x) for the Chip Select (/CS) pin

to communicate with the EEPROM. sysconfig can provide this; it will be named something similar to "SPI_1_INST".

No error checking is performed, since SLAA208 did not provide for this.

The code relies on DriverLib, but not sysconfig. All the driverlib-related configuration is passed in.

Polling  (busy-waiting) is used rather than interrupts. This is because only sysconfig knows the ISR name. 
Also, SPI rarely benefits from interrupts. 

This was tested using a M95P32 (EEPROM) and an MB85RS1 (FRAM). These devices use a 3-byte address.
To use a smaller device (shorter address) change the definitions of EEP_ADDRBITS and EEP_PAGESIZE accordingly.

## DMA
If EEP_DMA=1, an alternate initialization InitSPI_DMA() may be called with two additional arguments: the Rx/Tx DMA channel numbers. 

These channels should be configured in Sysconfig to use the appropriate SPI triggers. (The SPI DMA publishers are specialized.)

The other DMA channel configuration is overwritten.

## Peripherals & Pin Assignments

| Peripheral | Pin | Function |
| --- | --- | --- |
| GPIOA | PA22 | Standard Output (LED) |
| GPIOA | PA2 | Standard Output (scope probe point) |
| SYSCTL |  |  |
| SPI0 | PA17 | SPI SCLK |
| SPI0 | PA18 | SPI PICO (nee MOSI) |
| SPI0 | PA16 | SPI POCI (nee MISO) |
| GPIOA | PA4 | SPI /CS |
| EVENT |  |  |
| DEBUGSS | PA20 | Debug Clock |
| DEBUGSS | PA19 | Debug Data In Out |

## BoosterPacks, Board Resources & Jumper Settings

Visit [LP_MSPM0C1104](https://www.ti.com/tool/LP-MSPM0C1104) for LaunchPad information, including user guide and hardware files.

| Pin | Peripheral | Function | LaunchPad Pin | LaunchPad Settings |
| --- | --- | --- | --- | --- |
| PA22 | GPIOA | PA22    | J1_8  | N/A |
| PA2  | GPIOA | PA2     | J2_13 | N/A |
| PA17 | SPI   | SCLK    | J2_18 |
| PA18 | SPI0  | PICO    | J2_15 |
| PA16 | SPI0  | POCI    | J2_19 |
| GPIOA | PA4  | SPI /CS | J2_14 |
| PA20 | DEBUGSS | SWCLK | J2_11 | <ul><li>PA20 is used by SWD during debugging<br><ul><li>`J101 13:14 ON` Connect to XDS-110 SWCLK while debugging<br><li>`J101 13:14 OFF` Disconnect from XDS-110 SWCLK if using pin in application</ul></ul> |
| PA19 | DEBUGSS | SWDIO | J2_17 | <ul><li>PA19 is used by SWD during debugging<br><ul><li>`J101 11:12 ON` Connect to XDS-110 SWDIO while debugging<br><li>`J101 11:12 OFF` Disconnect from XDS-110 SWDIO if using pin in application</ul></ul> |

### Device Migration Recommendations
Except for DMA, the features used are available on any MSPM0 device.
Porting (migration) to other devices can (in theory) be done using Sysconfig.

Truth be known: I've never succeeded in using Sysconfig to "migrate" a project to a different device.
The anticipated usage here is to copy spi-eeprom.[ch] into your own project (using main-X.c as a guide).

## Example Usage
Connect a suitable SPI EEPROM device as above. 
Be sure  /WP and /HOLD are pulled high.

Compile, load and run the example.

LED1 and the USER_TEST_PIN will go high at the end.
