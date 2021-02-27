#ifndef _CC1101_H
#define _CC1101_H

#include <Arduino.h>
#include <SPI.h>
#include "ccpacket.h"

// -----------------------------------------------------------------
// Don't change this - http://www.ti.com/lit/an/swra112b/swra112b.pdf
#define NUM_CONFIG_REGISTERS		0x2F  //47 registers
// -----------------------------------------------------------------

/**
 * Carrier frequencies
 * Use the right one for your location:
 * https://www.analog.com/media/en/analog-dialogue/volume-40/number-1/articles/wireless-short-range-devices.pdf
 * https://www.acma.gov.au/Industry/Spectrum/Radiocomms-licensing/Class-licences/shortrange-spreadspectrum-devices-fact-sheet
 */
enum CFREQ
{
  CFREQ_433, // Europe and Australia 
  CFREQ_868, // Europe Only
  CFREQ_922  // For the U.S., Canada and Australia
};


/** 
 *  Data Rate
 */
enum DATA_RATE
{
  KBPS_250,
  KBPS_38,
  KBPS_4
};

/**
 * RF STATES
 */
enum CC_STATE
{
  STATE_IDLE,
  STATE_RX,
  STATE_TX,
  STATE_FSTXON,
  STATE_CALIBRATE,
  STATE_SETTLING,
  STATE_RXFIFO_OVERFLOW,
  STATE_TXFIFO_UNDERFLOW,
  STATE_UNKNOWN // shouldn't be in this state for long
};
   

/**
 * Miscellaneous
 */
#define CRYSTAL_FREQUENCY         26000000
#define FIFOBUFFER                0x42  //size of Fifo Buffer
#define RSSI_OFFSET_868MHZ        0x4E  //dec = 74
#define BROADCAST_ADDRESS         0x00  //broadcast address
#define CC1101_TEMP_ADC_MV        3.225 //3.3V/1023 . mV pro digit
#define CC1101_TEMP_CELS_CO       2.47  //Temperature coefficient 2.47mV per Grad Celsius

#define CHECK_BIT(var,pos)        ((var) & (1<<(pos)))
#define MIN(x, y)                 (((x) < (y)) ? (x) : (y))

/*
 * From cc1101.pdf:
 * 
 * All transactions on the SPI interface start with
 * a header byte containing a R/W¯ bit, a burst
 * access bit (B), and a 6-bit address (A5 – A0).
 * The CSn pin must be kept low during transfers
 * on the SPI bus. If CSn goes high during the
 * transfer of a header byte or during read/write
 * from/to a register, the transfer will be
 * cancelled. The timing for the address and data
 * transfer on the SPI interface is shown in Figure
 * 15 with reference to Table 22.
 */

/*---------------------------[CC1101 - R/W offsets]---------------------------*/
#define WRITE_SINGLE_BYTE   0x00 // 0x00000000
#define WRITE_BURST         0x40 // 0x01000000
#define READ_SINGLE_BYTE    0x80 // 0x10000000
#define READ_BURST          0xC0 // 0x11000000
/*---------------------------[END R/W offsets]--------------------------------*/

/*
/*------------------------[CC1101 - FIFO commands]----------------------------*/
#define TXFIFO_BURST        0x7F    //write burst only      0b01111111
#define TXFIFO_SINGLE_BYTE  0x3F    //write single only     0b00111111
#define RXFIFO_BURST        0xFF    //read burst only       0b11111111
#define RXFIFO_SINGLE_BYTE  0xBF    //read single only      0b10111111
/*---------------------------[END FIFO commands]------------------------------*/

/**
 * Type of register
 */
#define CC1101_CONFIG_REGISTER   READ_SINGLE_BYTE
#define CC1101_STATUS_REGISTER   READ_BURST

/**
 * PATABLE & FIFO's
 * The PATABLE is an 8-byte table that defines
 * the PA control settings to use for each of the
 * eight PA power values (selected by the 3-bit
 * value FREND0.PA_POWER). The table is
 * written and read from the lowest setting (0) to
 * the highest (7), one byte at a time.
 * Note: However we only write to the first entry only.
 */
#define CC1101_PATABLE           0x3E        // PATABLE address
#define CC1101_TXFIFO            0x3F        // TX FIFO address
#define CC1101_RXFIFO            0x3F        // RX FIFO address

/**
 * Command strobes
 */
#define CC1101_SRES              0x30        // Reset CC1101 chip
#define CC1101_SFSTXON           0x31        // Enable and calibrate frequency synthesizer (if MCSM0.FS_AUTOCAL=1). If in RX (with CCA):
                                             // Go to a wait state where only the synthesizer is running (for quick RX / TX turnaround).
#define CC1101_SXOFF             0x32        // Turn off crystal oscillator
#define CC1101_SCAL              0x33        // Calibrate frequency synthesizer and turn it off. SCAL can be strobed from IDLE mode without
                                             // setting manual calibration mode (MCSM0.FS_AUTOCAL=0)
#define CC1101_SRX               0x34        // Enable RX. Perform calibration first if coming from IDLE and MCSM0.FS_AUTOCAL=1
#define CC1101_STX               0x35        // In IDLE state: Enable TX. Perform calibration first if MCSM0.FS_AUTOCAL=1.
                                             // If in RX state and CCA is enabled: Only go to TX if channel is clear
#define CC1101_SIDLE             0x36        // Exit RX / TX, turn off frequency synthesizer and exit Wake-On-Radio mode if applicable
#define CC1101_SWOR              0x38        // Start automatic RX polling sequence (Wake-on-Radio) as described in Section 19.5 if
                                             // WORCTRL.RC_PD=0
#define CC1101_SPWD              0x39        // Enter power down mode when CSn goes high
#define CC1101_SFRX              0x3A        // Flush the RX FIFO buffer. Only issue SFRX in IDLE or RXFIFO_OVERFLOW states
#define CC1101_SFTX              0x3B        // Flush the TX FIFO buffer. Only issue SFTX in IDLE or TXFIFO_UNDERFLOW states
#define CC1101_SWORRST           0x3C        // Reset real time clock to Event1 value
#define CC1101_SNOP              0x3D        // No operation. May be used to get access to the chip status byte

/**
 * CC1101 configuration registers
 */
// Register values set regardless of frequency and bitrate
#define CC1101_IOCFG2            0x00        // GDO2 Output Pin Configuration
#define CC1101_IOCFG1            0x01        // GDO1 Output Pin Configuration
#define CC1101_IOCFG0            0x02        // GDO0 Output Pin Configuration
#define CC1101_FIFOTHR           0x03        // RX FIFO and TX FIFO Thresholds
#define CC1101_SYNC1             0x04        // Sync Word, High Byte
#define CC1101_SYNC0             0x05        // Sync Word, Low Byte
#define CC1101_PKTLEN            0x06        // Packet Length
#define CC1101_PKTCTRL1          0x07        // Packet Automation Control
#define CC1101_PKTCTRL0          0x08        // Packet Automation Control
#define CC1101_ADDR              0x09        // Device Address
#define CC1101_CHANNR            0x0A        // Channel Number

// Register values which change depending on frequency and bitrate
#define CC1101_FSCTRL1           0x0B        // Frequency Synthesizer Control
#define CC1101_FSCTRL0           0x0C        // Frequency Synthesizer Control
#define CC1101_FREQ2             0x0D        // Frequency Control Word, High Byte
#define CC1101_FREQ1             0x0E        // Frequency Control Word, Middle Byte
#define CC1101_FREQ0             0x0F        // Frequency Control Word, Low Byte
#define CC1101_MDMCFG4           0x10        // Modem Configuration
#define CC1101_MDMCFG3           0x11        // Modem Configuration
#define CC1101_MDMCFG2           0x12        // Modem Configuration
#define CC1101_MDMCFG1           0x13        // Modem Configuration
#define CC1101_MDMCFG0           0x14        // Modem Configuration
#define CC1101_DEVIATN           0x15        // Modem Deviation Setting
#define CC1101_MCSM2             0x16        // Main Radio Control State Machine Configuration
#define CC1101_MCSM1             0x17        // Main Radio Control State Machine Configuration
#define CC1101_MCSM0             0x18        // Main Radio Control State Machine Configuration
#define CC1101_FOCCFG            0x19        // Frequency Offset Compensation Configuration
#define CC1101_BSCFG             0x1A        // Bit Synchronization Configuration
#define CC1101_AGCCTRL2          0x1B        // AGC Control
#define CC1101_AGCCTRL1          0x1C        // AGC Control
#define CC1101_AGCCTRL0          0x1D        // AGC Control
#define CC1101_WOREVT1           0x1E        // High Byte Event0 Timeout
#define CC1101_WOREVT0           0x1F        // Low Byte Event0 Timeout
#define CC1101_WORCTRL           0x20        // Wake On Radio Control
#define CC1101_FREND1            0x21        // Front End RX Configuration
#define CC1101_FREND0            0x22        // Front End TX Configuration
#define CC1101_FSCAL3            0x23        // Frequency Synthesizer Calibration
#define CC1101_FSCAL2            0x24        // Frequency Synthesizer Calibration
#define CC1101_FSCAL1            0x25        // Frequency Synthesizer Calibration
#define CC1101_FSCAL0            0x26        // Frequency Synthesizer Calibration

// Others
#define CC1101_RCCTRL1           0x27        // RC Oscillator Configuration
#define CC1101_RCCTRL0           0x28        // RC Oscillator Configuration
#define CC1101_FSTEST            0x29        // Frequency Synthesizer Calibration Control
#define CC1101_PTEST             0x2A        // Production Test
#define CC1101_AGCTEST           0x2B        // AGC Test
#define CC1101_TEST2             0x2C        // Various Test Settings
#define CC1101_TEST1             0x2D        // Various Test Settings
#define CC1101_TEST0             0x2E        // Various Test Settings

/**
 * Status registers
 */
#define CC1101_PARTNUM           0x30        // Chip ID
#define CC1101_VERSION           0x31        // Chip ID
#define CC1101_FREQEST           0x32        // Frequency Offset Estimate from Demodulator
#define CC1101_LQI               0x33        // Demodulator Estimate for Link Quality
#define CC1101_RSSI              0x34        // Received Signal Strength Indication
#define CC1101_MARCSTATE         0x35        // Main Radio Control State Machine State
#define CC1101_WORTIME1          0x36        // High Byte of WOR Time
#define CC1101_WORTIME0          0x37        // Low Byte of WOR Time
#define CC1101_PKTSTATUS         0x38        // Current GDOx Status and Packet Status
#define CC1101_VCO_VC_DAC        0x39        // Current Setting from PLL Calibration Module
#define CC1101_TXBYTES           0x3A        // Underflow and Number of Bytes
#define CC1101_RXBYTES           0x3B        // Overflow and Number of Bytes
#define CC1101_RCCTRL1_STATUS    0x3C        // Last RC Oscillator Calibration Result
#define CC1101_RCCTRL0_STATUS    0x3D        // Last RC Oscillator Calibration Result 

/**
 * RX/TXBYTES Status Byte Masks
 */
#define BYTES_IN_FIFO         0x7F // bitmask to get number in FIFO (first 7 bits)
#define OVERFLOW_IN_FIFO      0x80 // byte number in RXfifo


/**
 * Marcstates from 0x35 (0xF5): MARCSTATE – Main Radio Control State Machine State
 */
#define MARCSTATE_SLEEP         0x00
#define MARCSTATE_IDLE          0x01
#define MARCSTATE_XOFF          0x02
#define MARCSTATE_VCOON_MC      0x03
#define MARCSTATE_REGON_MC      0x04
#define MARCSTATE_MANCAL        0x05
#define MARCSTATE_VCOON         0x06
#define MARCSTATE_REGON         0x07
#define MARCSTATE_STARTCAL      0x08
#define MARCSTATE_BWBOOST       0x09
#define MARCSTATE_FS_LOCK       0x0A
#define MARCSTATE_IFADCON       0x0B
#define MARCSTATE_ENDCAL        0x0C
#define MARCSTATE_RX            0x0D
#define MARCSTATE_RX_END        0x0E
#define MARCSTATE_RX_RST        0x0F
#define MARCSTATE_TXRX_SWITCH   0x10
#define MARCSTATE_RXFIFO_OVERFLOW       0x11
#define MARCSTATE_FSTXON        0x12
#define MARCSTATE_TX            0x13
#define MARCSTATE_TX_END        0x14
#define MARCSTATE_RXTX_SWITCH   0x15
#define MARCSTATE_TXFIFO_UNDERFLOW      0x16


/**
 * CC1101 configuration register names labels for Serial printing
 */
static const char CC1101_CONFIG_REGISTER_NAME[NUM_CONFIG_REGISTERS][16] PROGMEM = {"IOCFG2","IOCFG1","IOCFG0","FIFOTHR","SYNC1","SYNC0","PKTLEN","PKTCTRL1","PKTCTRL0","ADDR","CHANNR","FSCTRL1","FSCTRL0","FREQ2","FREQ1","FREQ0","MDMCFG4","MDMCFG3","MDMCFG2","MDMCFG1","MDMCFG0","DEVIATN","MCSM2","MCSM1","MCSM0","FOCCFG","BSCFG","AGCCTRL2","AGCCTRL1","AGCCTRL0","WOREVT1","WOREVT0","WORCTRL","FREND1","FREND0","FSCAL3","FSCAL2","FSCAL1","FSCAL0","RCCTRL1","RCCTRL0","FSTEST","PTEST","AGCTEST","TEST2","TEST1","TEST0" };



/**
 * 
 * Essentially we store what we have requested the registers to be via. writeReg,
 * then we check it against what the CC1101 says it is. Helps weed out CC1101 
 * configuration mis-matches which seem to occur with SPI or coding issues.
 */  
static uint8_t currentConfig[NUM_CONFIG_REGISTERS];


/**
 * CC1101 PA (TX Output Power) TABLE
 * Based on Design Note DN013 (swra151a.pdf), but also https://github.com/SpaceTeddy/CC1101/blob/master/cc1100_arduino.cpp
 * Note: We don't actually bother populating all 8 PA Table entries on the CC1101, we only set the PATABLE0 to the relevant
 *       value below according to the configured frequency and requested dBm (aka. This is a config lookup matrix of sorts).
 */
                            //Patable index: -30  -20  -15  -10    0    5    7   10 dBm
static uint8_t patable_power_433[] = {0x6C,0x1C,0x06,0x3A,0x51,0x85,0xC8,0xC0};
static uint8_t patable_power_868[] = {0x03,0x17,0x1D,0x26,0x50,0x86,0xCD,0xC0};
static uint8_t patable_power_9XX[] = {0x0B,0x1B,0x6D,0x67,0x50,0x85,0xC9,0xC1};


/**
 * CC1101 generic configuration defaults - will need to be overwritten!
 */
#define CC1101_DEFVAL_IOCFG2     0x06        // GDO2 Output Pin Configuration - used to interrupt on packet received! (either use this pin on the CC or GDO0/IOCFG0)
#define CC1101_DEFVAL_IOCFG1     0x2E        // GDO1 Output Pin Configuration - not used
#define CC1101_DEFVAL_IOCFG0     0x06        // GDO0 Output Pin Configuration - used to interrupt on packet received! Useless on TX
#define CC1101_DEFVAL_FIFOTHR    0x07        // RX FIFO and TX FIFO Thresholds
#define CC1101_DEFVAL_SYNC1      0xD3        // Synchronization word, high byte
#define CC1101_DEFVAL_SYNC0      0x91        // Synchronization word, low byte
#define CC1101_DEFVAL_PKTLEN     61          // Max RADIO Packet Length (61 bytes of data for this library). Packet is discarded if bigger.
#define CC1101_DEFVAL_PKTCTRL1   0x06        // Packet Automation Control //  Address check and 0 (0x00) broadcast + append two bytes on receipt for CRC info and RSSI/LQI
#define CC1101_DEFVAL_PKTCTRL0   0x44        // Packet Automation Control // whitening enabled + fixed length packet + crc appended
#define CC1101_DEFVAL_ADDR       0x00        // This devices address, used for packet filtration. Optional broadcast addresses are 0(0x00) and 255 (0xFF).
#define CC1101_DEFVAL_CHANNR     0x00        // Channel Number - The 8-bit unsigned channel number, which is multiplied by the channel spacing setting and added to the base frequency.

// ****** START: Configuration Settings that can change depending on FREQ / Data Rate (Modulation)
  #define CC1101_DEFVAL_FSCTRL1    0x21        // Frequency Synthesizer Control
  #define CC1101_DEFVAL_FSCTRL0    0x00        // Frequency Synthesizer Control
  
  // Carrier frequency = 433 MHz
  #define CC1101_DEFVAL_FREQ2_433  0x10        // Frequency Control Word, High Byte
  #define CC1101_DEFVAL_FREQ1_433  0xA7        // Frequency Control Word, Middle Byte
  #define CC1101_DEFVAL_FREQ0_433  0x62        // Frequency Control Word, Low Byte
  
  // Carrier frequency = 868 MHz - 868.299866
  #define CC1101_DEFVAL_FREQ2_868  0x21        // Frequency Control Word, High Byte
  #define CC1101_DEFVAL_FREQ1_868  0x65        // Frequency Control Word, Middle Byte
  #define CC1101_DEFVAL_FREQ0_868  0x6A        // Frequency Control Word, Low Byte
  
  // Carrier frequency = 922 MHz - 921.999878
  #define CC1101_DEFVAL_FREQ2_922  0x23        // Frequency Control Word, High Byte
  #define CC1101_DEFVAL_FREQ1_922  0x76        // Frequency Control Word, Middle Byte
  #define CC1101_DEFVAL_FREQ0_922  0x27        // Frequency Control Word, Low Byte 
  
  // The MDMCFG values will change depending on bitrate
  #define CC1101_DEFVAL_MDMCFG4    0x2D        // Modem Configuration - For 250kbps
  #define CC1101_DEFVAL_MDMCFG3    0x3B        // Modem Configuration - For 250kbps
  #define CC1101_DEFVAL_MDMCFG2    0x13        // Modem Configuration - For 250kbps    (doesn't change)
  #define CC1101_DEFVAL_MDMCFG1    0x22        // Modem Configuration - FEC / Preamble (doesn't change)
  #define CC1101_DEFVAL_MDMCFG0    0xF8        // Modem Configuration - Channel Spacing (apparently the same across bands - doesn't change)
  
  #define CC1101_DEFVAL_DEVIATN    0x35        // Modem Deviation Setting (default for 38.4kbaud)
  
  // These won't change - Modem behaviour. Irrelevant to freq / bitrate
  #define CC1101_DEFVAL_MCSM2      0x07        // Main Radio Control State Machine Configuration  // Stay in RX until end of packet.
  //#define CC1101_DEFVAL_MCSM1      0x30        // Main Radio Control State Machine Configuration  // What do do after a packet is sent or recieved. 0x30 = go to idle, 0x33 - switch to rx after tx 
  #define CC1101_DEFVAL_MCSM1      0x33        // Main Radio Control State Machine Configuration  // What do do after a packet is sent or recieved. 0x30 = go to idle, 0x33 - switch to rx after tx 
  #define CC1101_DEFVAL_MCSM0      0x18        // Main Radio Control State Machine Configuration
  
  // These might change with freq / bitrate
  #define CC1101_DEFVAL_FOCCFG     0x16        // Frequency Offset Compensation Configuration
  #define CC1101_DEFVAL_BSCFG      0x6C        // Bit Synchronization Configuration
  #define CC1101_DEFVAL_AGCCTRL2   0x43        // AGC Control
  #define CC1101_DEFVAL_AGCCTRL1   0x40        // AGC Control
  #define CC1101_DEFVAL_AGCCTRL0   0x91        // AGC Control
  #define CC1101_DEFVAL_WOREVT1    0x87        // High Byte Event0 Timeout
  #define CC1101_DEFVAL_WOREVT0    0x6B        // Low Byte Event0 Timeout
  #define CC1101_DEFVAL_WORCTRL    0xFB        // Wake On Radio Control
  #define CC1101_DEFVAL_FREND1     0xB6        // Front End RX Configuration
  #define CC1101_DEFVAL_FREND0     0x10        // Front End TX Configuration
  #define CC1101_DEFVAL_FSCAL3     0xEA        // Frequency Synthesizer Calibration
  #define CC1101_DEFVAL_FSCAL2     0x2A        // Frequency Synthesizer Calibration
  #define CC1101_DEFVAL_FSCAL1     0x00        // Frequency Synthesizer Calibration
  #define CC1101_DEFVAL_FSCAL0     0x1F        // Frequency Synthesizer Calibration
// ****** END: Configuration Settings that will change depending on FREQ / Data Rate (Modulation)

#define CC1101_DEFVAL_RCCTRL1    0x41        // RC Oscillator Configuration
#define CC1101_DEFVAL_RCCTRL0    0x00        // RC Oscillator Configuration

//#define CC1101_DEFVAL_FSTEST     0x59        // Frequency Synthesizer Calibration Control
//#define CC1101_DEFVAL_PTEST      0x7F        // Production Test
//#define CC1101_DEFVAL_AGCTEST    0x3F        // AGC Test
//#define CC1101_DEFVAL_TEST2      0x81        // Various Test Settings
//#define CC1101_DEFVAL_TEST1      0x35        // Various Test Settings
//#define CC1101_DEFVAL_TEST0      0x09        // Various Test Settings

/**
 * Alias for some default values
 */
#define CCDEF_CHANNR  CC1101_DEFVAL_CHANNR
#define CCDEF_SYNC0  CC1101_DEFVAL_SYNC0
#define CCDEF_SYNC1  CC1101_DEFVAL_SYNC1
#define CCDEF_ADDR  CC1101_DEFVAL_ADDR

/**
 * Macros
 */
// Read CC1101 Config register
#define readConfigReg(regAddr)    readReg(regAddr, CC1101_CONFIG_REGISTER)
// Read CC1101 Status register
//#define readStatusReg(regAddr)    readReg(regAddr, CC1101_STATUS_REGISTER)
#define readStatusReg(regAddr)    readStatusRegSafe(regAddr) // Safe version

// Enter Rx state
//#define setRxState()              cmdStrobe(CC1101_SRX)
// Enter Tx state
//#define setTxState()              cmdStrobe(CC1101_STX)
// Enter IDLE state
#define setIdleState()            cmdStrobe(CC1101_SIDLE)
// Flush Rx FIFO
#define flushRxFifo()             cmdStrobe(CC1101_SFRX)
// Flush Tx FIFO
#define flushTxFifo()             cmdStrobe(CC1101_SFTX)
// Disable address check
#define disableAddressCheck()     writeReg(CC1101_PKTCTRL1, 0x04)
// Enable address check
#define enableAddressCheck()      writeReg(CC1101_PKTCTRL1, 0x06)
// Disable CCA
#define disableCCA()              writeReg(CC1101_MCSM1, 0)
// Enable CCA
#define enableCCA()               writeReg(CC1101_MCSM1, CC1101_DEFVAL_MCSM1)

/**
 * Class: CC1101
 * 
 * Description:
 * CC1101 interface
 */
class CC1101
{
  private:

    CFREQ     carrierFreq;  // The frequency chosen
    DATA_RATE dataRate;     // The data rate.
    CC_STATE  currentState; // What the state of the CC1101 is according to our last check
	
	uint8_t CC1101_GDO0_interrupt_pin;

    void configureGPIO(void);   

    uint8_t channel;
    uint8_t syncWord[2];
    uint8_t devAddress;    
    bool    serialDebug; 
	
	uint16_t receivedStreamSize = 0; // Number of bytes or chars recieved
	int      receivedRSSI = 0; // in dBm

    // For period state checking
    unsigned long last_CCState_check = 0;

    // For the stream of packets - reassembly. A bit of a hack.
    uint8_t  previous_pkt_seq_num;

    // To quickly get stuff out of cc1101, or buffer what to put into the CC1101 FIFO
    byte cc1101_rx_tx_fifo_buff[64] = { 0 };

    /**
     * writeBurstReg
     * 
     * Write multiple registers into the CC1101 IC via SPI
     * 
     * 'regAddr'  Register address
     * 'buffer' Data to be writen
     * 'len'  Data length
     */
    void writeBurstReg(uint8_t regAddr, uint8_t* buffer, uint8_t len);

    /**
     * readBurstReg
     * 
     * Read burst data from CC1101 via SPI
     * 
     * 'buffer' Buffer where to copy the result to
     * 'regAddr'  Register address
     * 'len'  Data length
     */
    void readBurstReg(uint8_t * buffer, uint8_t regAddr, uint8_t len);
    
    /**
     * explainStatus
     * 
     * Explains the Status Byte returned from the CC1101 on each writeReg() 
     * SPI transaction in human English to serial if enabled
     * 
     * 'byte' is the byte returned from the SPI 
     */    
     void readCCStatus(byte status);     

     /* RSSI and LQI function */
     // Get signal strength indicator in dBm.
     // See: http://www.ti.com/lit/an/swra114d/swra114d.pdf
     inline int decodeCCRSSI(uint8_t raw) 
     {
          uint8_t rssi_dec;
          
          // TODO: This rssi_offset is dependent on baud and MHz; this is for 38.4kbps and 433 MHz.
          rssi_dec = (uint8_t) raw;
          if (rssi_dec >= 128)
              return ((int)( rssi_dec - 256) / 2) - RSSI_OFFSET_868MHZ;
          else
              return (rssi_dec / 2) - RSSI_OFFSET_868MHZ;
     }
      
     // Get link quality indicator.
     inline int decodeCCLQI(uint8_t raw) 
     {
          return 0x3F - raw;
     }
     
    
    
  public:    

    void attachGDO0Interrupt(void);
    void detachGDO0Interrupt(void);   

    CC1101(void);

    /**
     * cmdStrobe
     * 
     * Send command strobe to the CC1101 IC via SPI
     * 
     * 'cmd'  Command strobe
     */
    void cmdStrobe(uint8_t cmd);

    /**
     * wakeUp
     * 
     * Wake up CC1101 from Power Down state
     */
    void wakeUp(void);

    /**
     * readReg
     * 
     * Read CC1101 register via SPI
     * 
     * 'regAddr'  Register address
     * 'regType'  Type of register: CC1101_CONFIG_REGISTER or CC1101_STATUS_REGISTER
     * 
     * Return:
     *  Data byte returned by the CC1101 IC
     */
    uint8_t readReg(uint8_t regAddr, uint8_t regType);

    /**
     * 
     * readStatusRegSafe(uint8_t regAddr)
     * http://e2e.ti.com/support/wireless-connectivity/other-wireless/f/667/t/334528?CC1101-Random-RX-FIFO-Overflow
     * 
     */
    uint8_t readStatusRegSafe(uint8_t regAddr);

    /**
     * writeReg
     * 
     * Write single register into the CC1101 IC via SPI
     * 
     * 'regAddr'  Register address
     * 'value'  Value to be writen
     */
    void writeReg(uint8_t regAddr, uint8_t value);

    /**
     * setCCregs
     * 
     * Configure CC1101 registers
     */
    void setCCregs(void);

    /**
     * Power Cycle Reset 
     * Ref: http://e2e.ti.com/support/wireless-connectivity/other-wireless/f/667/t/396609
     * Reset CC1101
     */
    void hardReset(void);

    /**
     * Soft Reset
     * 
     * Reset CC1101
     */
    void softReset(void);    
    
    /**
     * init
     * 
     * Initialize CC1101 radio from inital power-on
     *
     * @param freq Carrier frequency
     * @param mode Working mode (speed, ...)
     */
	 
	 //!radio.begin(CFREQ_868, KBPS_250, /* channel num */ 16, /* address */ 0, CC1101_DEFVAL_ADDR, INTERRUPT_PIN /* Interrupt */) ) // channel 16! Whitening enabled 
	 
    bool begin(CFREQ freq, DATA_RATE rate, uint8_t channr, uint8_t addr, uint8_t interrupt_pin);
    
    /* For when all the configuration data is provided */
    bool begin(const byte regConfig[NUM_CONFIG_REGISTERS], uint8_t interrupt_pin);    


    /**
     * setSyncWord
     * 
     * Set synchronization word
     * 
     * 'syncH'  Synchronization word - High byte
     * 'syncL'  Synchronization word - Low byte
     */
    void setSyncWord(uint8_t syncH, uint8_t syncL);

    /**
     * setSyncWord (overriding method)
     * 
     * Set synchronization word
     * 
     * 'syncH'  Synchronization word - pointer to 2-byte array
     */
    void setSyncWord(uint8_t *sync);

    /**
     * setDevAddress
     * 
     * Set device address
     * 
     * 'addr' Device address
     */
    void setDevAddress(uint8_t addr);

    /**
     * setCarrierFreq
     * 
     * Set carrier frequency
     * 
     * 'freq' New carrier frequency
     */
    void setCarrierFreq(CFREQ freq);
    
    /**
     * setChannel
     * 
     * Set frequency channel
     * 
     * 'chnl' Frequency channel
     */
    void setChannel(uint8_t chnl);

    /**
     * setPowerDownState
     * 
     * Put CC1101 into power-down state
     */
    void setPowerDownState();


    /**
     * Sending Messages or Data
     */
    bool sendChars(const char * data, uint8_t dst_address=BROADCAST_ADDRESS);
    bool sendBytes(byte * data,  uint16_t size, uint8_t dst_address=BROADCAST_ADDRESS);  
    uint16_t getSize(void);	
		
	inline int getLastRSSI(void)
	{ return receivedRSSI; }; // get the RSSI of the last packet to be received

    /**
     * Returns pointer to RX buffer
     */
    char *  getChars(void);
    //bool sendBytes(byte * data);    


    /**
     * Returns pointer to RX buffer
     */
    byte *  getBytes(void);
    //bool sendBytes(byte * data);   
    

    /**
     * Listen for a CC1101 user data stream
     */
    bool dataAvailable(void);
    
    /**
     * sendPacket
     * 
     * Send data packet via RF
     * 
     * 'packet' Packet to be transmitted. First byte is the destination address
     *
     *  Return:
     *    True if the transmission succeeds
     *    False otherwise
     */
    bool sendPacket(CCPACKET packet);
    

    /**
     * receivePacket
     * 
     * Read data packet from RX FIFO
     * 
     * Return:
     *  Amount of bytes received
     */
    uint8_t receivePacket(CCPACKET *packet);
    
    /**
     * setRxState
     * 
     * Enter Rx state
     */
    void setRxState(void);

    /**
     * setTxState
     * 
     * Enter Tx state
     */
    void setTxState(void);
	
    /**
     * setOutputPowerLeveldBm
     * 
     * Sets the output power level, parameter passed is a dBm value wanted.
     */
    void setOutputPowerLeveldBm(int8_t dBm);	
	
	
    /**
     * printPATable
     * 
     * Print the current PA Table Values
     */
    void printPATable(void);	
	
    
    /**
     * printCConfigCheck
     * 
     * Print to console the CC1101's current config, also check for obvious signs that something is wrong. 
	 * If things aren't looking good, return false.
     */
    bool printCConfigCheck(void);

    /* Enable verbose output on serial port */
    void enableSerialDebug(void)
    {
        serialDebug = true;
    }

    /* Validate config registers with what is expected */
    bool checkCC(void);

    /* Print the Chipcon MarcState in English to Serial */
    void printCCState(void);
    
     /* Print the Chipcon TX and RX FIFO Status in English to Serial */
    void printCCFIFOState(void);

    /* Print the Marcstate */
    void printMarcstate(void);
       
 };

#endif
