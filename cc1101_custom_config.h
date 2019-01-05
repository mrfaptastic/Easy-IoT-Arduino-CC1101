/*******************************************************************
 * CC1101 Custom Radio Configuration options - all 47 regs.
 * 
 * Only use this if you know what you are doing. In your sketch 
 * use something like 'radio.begin(cc1101_GFSK_250_kb_testing_works);'
 * 
 * Note: Certain configuration registers will be changed by the 
 * configuration setting functions as in cc1101.cpp
 * 
 * General princple, all modulation radio configuration options use 
 * GFSK, as it is superior.
 * 
 * This library expects a fix packet size of 61 bytes (PKTLEN) and
 * IOCFG2/IOCFG0 must be 0x06. Anything else will break this library.
 *
 *******************************************************************/

/* IOCFGX Value Guide:
 
 6 (0x06) (What this library expects)
    Asserts when sync word has been sent / received, and de-asserts at the end of the packet. In RX, the pin will also deassert
    when a packet is discarded due to address or maximum length filtering or when the radio enters
    RXFIFO_OVERFLOW state. In TX the pin will de-assert if the TX FIFO underflows.
    
 7 (0x07) 
    Asserts when a packet has been received with CRC OK. De-asserts when the first byte is read from the RX FIFO.
    
*/

 const byte cc1101_GFSK_250_kb_testing_works[47] PROGMEM = {
  
          // Default from SmartRF Studio
          // # Address Config = No address check 
          // # Base Frequency = 868.299866 
          // # CRC Enable = true 
          // # Carrier Frequency = 868.299866 
          // # Channel Number = 0 
          // # Channel Spacing = 199.951172 
          // # Data Rate = 249.939 
          // # Deviation = 126.953125 
          // # Device Address = 0 
          // # Manchester Enable = false 
          // # Modulated = true 
          // # Modulation Format = GFSK 
          // # PA Ramping = false 
          // # Packet Length = 255 
          // # Packet Length Mode = Variable packet length mode. Packet length configured by the first byte after sync word 
          // # Preamble Count = 4 
          // # RX Filter BW = 541.666667 
          // # Sync Word Qualifier Mode = 30/32 sync word bits detected 
          // # TX Power = 0 
          // # Whitening = true   
          
          0x06,  // IOCFG2        GDO2 Output Pin Configuration // Must be same as IOCFG0, also needs to be 0x06 if PKTCTRL1 CRC autoflush isn't being used.
          0x2E,  // IOCFG1        GDO1 Output Pin Configuration
          0x06,  // IOCFG0        GDO0 Output Pin Configuration // needs to be 0x06 if PKTCTRL1 CRC autoflush isn't being used.
          0x07,  // FIFOTHR       RX FIFO and TX FIFO Thresholds 
          0xD3,  // SYNC1         Sync Word, High Byte
          0x91,  // SYNC0         Sync Word, Low Byte
          61,    // PKTLEN        Packet Length // MUST NOT CHANGE FROM 61 bytes to avoid this BUG: https://e2e.ti.com/support/wireless_connectivity/proprietary_sub_1_ghz_simpliciti/f/156/t/372505         
          0x06,  // PKTCTRL1      Packet Automation Control 
          //0x0E,  // PKTCTRL1      Packet Automation Control // crc bad autoflush + append status + 0x00 broadcast
          0x44,  // PKTCTRL0      Packet Automation Control // fixed width packet is 0x44 vs, 0x45
          0x00,  // ADDR          Device Address  // changed to 71! Our device number is ASCII 'G'
          16,    // CHANNR        Channel Number  // avoid using 0 
          0x0C,  // FSCTRL1       Frequency Synthesizer Control
          0x00,  // FSCTRL0       Frequency Synthesizer Control
          0x21,  // FREQ2         Frequency Control Word, High Byte
          0x65,  // FREQ1         Frequency Control Word, Middle Byte
          0x6A,  // FREQ0         Frequency Control Word, Low Byte
          0x2D,  // MDMCFG4       Modem Configuration
          0x3B,  // MDMCFG3       Modem Configuration
          0x13,  // MDMCFG2       Modem Configuration
          0x22,  // MDMCFG1       Modem Configuration
          0xF8,  // MDMCFG0       Modem Configuration
          0x62,  // DEVIATN       Modem Deviation Setting
          0x07,  // MCSM2         Main Radio Control State Machine Configuration
          0x3F,  // MCSM1         Main Radio Control State Machine Configuration // WAS 0x0F (no clear channel check, always in RX)
          0x18,  // MCSM0         Main Radio Control State Machine Configuration
          0x1D,  // FOCCFG        Frequency Offset Compensation Configuration
          0x1C,  // BSCFG         Bit Synchronization Configuration
          0xC7,  // AGCCTRL2      AGC Control
          0x00,  // AGCCTRL1      AGC Control
          0xB0,  // AGCCTRL0      AGC Control
          0x87,  // WOREVT1       High Byte Event0 Timeout
          0x6B,  // WOREVT0       Low Byte Event0 Timeout
          0xFB,  // WORCTRL       Wake On Radio Control
          0xB6,  // FREND1        Front End RX Configuration
          0x10,  // FREND0        Front End TX Configuration
          0xEA,  // FSCAL3        Frequency Synthesizer Calibration
          0x2A,  // FSCAL2        Frequency Synthesizer Calibration
          0x00,  // FSCAL1        Frequency Synthesizer Calibration
          0x1F,  // FSCAL0        Frequency Synthesizer Calibration
          0x41,  // RCCTRL1       RC Oscillator Configuration
          0x00,  // RCCTRL0       RC Oscillator Configuration
          0x59,  // FSTEST        Frequency Synthesizer Calibration Control,
          0x7F,  // PTEST         Production Test
          0x3F,  // AGCTEST       AGC Test
          0x88,  // TEST2         Various Test Settings
          0x31,  // TEST1         Various Test Settings
          0x09   // TEST0         Various Test Settings
 };



 
