#include "cc1101.h"

// Do we want the inbuild led to flash when we send or recieve? Useful for diagnostics.
// Comment the below line out to turn this off.
#define ENABLE_BUILTIN_LED 1 


// What debug and internal workings information do we want to output to Serial for debug or monitoring purposes?
//#define SERIAL_INFO  1 


#ifdef ESP32
	#define LED_PIN 2
#else
	#define LED_PIN LED_BUILTIN
#endif

/**
   Macros
*/
// Select (SPI) CC1101
#define cc1101_Select()  digitalWrite(SS, LOW)
// Deselect (SPI) CC1101
#define cc1101_Deselect()  digitalWrite(SS, HIGH)
// Wait until SPI MISO line goes low
#define wait_Miso()  while(digitalRead(MISO)>0)
// Get GDO0 pin state
#define getGDO0state()  digitalRead(CC1101_GDO0_interrupt_pin)
// Wait until GDO0 line goes high
#define wait_GDO0_high()  while(!getGDO0state())
// Wait until GDO0 line goes low
#define wait_GDO0_low()  while(getGDO0state())



/**
 * Global variables for packet and stream interrupts
 */
volatile bool streamReceived = false;
volatile bool packetReceived = false;

/**
 * Global functions for packet and stream interrupts
 */
#ifdef ESP32
	#define ESP_INT_CODE IRAM_ATTR
#elif ESP8266 
	#define ESP_INT_CODE ICACHE_RAM_ATTR
#else 
	#define ESP_INT_CODE
#endif

void ESP_INT_CODE interupt_streamReceived() {
    streamReceived = true;
}

void ESP_INT_CODE interupt_packetReceived() {
    packetReceived = true;
}


/**
   CC1101

   Class constructor
*/
CC1101::CC1101(void)
{
  carrierFreq   = CFREQ_868;
  channel       = CC1101_DEFVAL_CHANNR;
  devAddress    = CC1101_DEFVAL_ADDR;

  syncWord[0]   = CC1101_DEFVAL_SYNC1;
  syncWord[1]   = CC1101_DEFVAL_SYNC0;

  // for the fifo
  memset(cc1101_rx_tx_fifo_buff, 0x00,  sizeof(cc1101_rx_tx_fifo_buff)); // flush 
  
  // for longer than one packet messages
  memset(stream_multi_pkt_buffer, 0x00, sizeof(stream_multi_pkt_buffer)); // flush

  // State of CC as told by the CC from the returned status byte from each SPI write transfer
  currentState = STATE_UNKNOWN;

  last_CCState_check = millis();
}

/* Configure the ESP's GPIO etc */
void CC1101::configureGPIO(void)
{
	
#ifdef ESP32
  pinMode(SS, OUTPUT);                       // Make sure that the SS Pin is declared as an Output
#endif
  SPI.begin();     
  
#if defined(ESP32) || defined(ESP8266)
  // Initialize SPI interface
  SPI.setFrequency(400000);  
#endif

  pinMode(CC1101_GDO0_interrupt_pin, INPUT);          // Config GDO0 as input

#ifdef ENABLE_BUILTIN_LED
  pinMode(LED_PIN, OUTPUT);         // Led will flash on recieve and send of packet
#endif

  if (serialDebug) {
    Serial.print(F("Using ESP Pin "));
    Serial.print(CC1101_GDO0_interrupt_pin);
    Serial.println(F(" for CC1101 GDO0 / GDO2."));
  }

}

/**
   begin

   Initialize CC1101 radio

   @param freq Carrier frequency
   @param mode Working mode (speed, ...)
*/
bool CC1101::begin(CFREQ freq, DATA_RATE rate, uint8_t channr, uint8_t addr, uint8_t _int_pin)
{
  carrierFreq = freq;
  dataRate    = rate;
  
  CC1101_GDO0_interrupt_pin = _int_pin;

  channel       = channr;
  devAddress    = addr; 

  configureGPIO();

  hardReset();                              // Reset CC1101

  delay(100);  

  setCCregs();
  
  
  setIdleState();       // Enter IDLE state before flushing RxFifo (don't need to do this when Overflow has occured)
  delayMicroseconds(1);
  flushRxFifo();        // Flush Rx FIFO  
  flushTxFifo();     


  attachGDO0Interrupt();

  return checkCC();  

}

/* Begin function when we're given all the 47 configuration registers in one hit */
bool CC1101::begin(const byte regConfig[NUM_CONFIG_REGISTERS], uint8_t _int_pin)
{
	
  CC1101_GDO0_interrupt_pin = _int_pin;	

  configureGPIO();

  hardReset();                              // Reset CC1101

  delay(100);

  // Go through the array and set
  for (uint8_t i = 0; i < NUM_CONFIG_REGISTERS-6; i++)
  {
   
    writeReg(i, pgm_read_byte(regConfig + i));
    //writeReg(i, regConfig[i]);
    _NOP(); _NOP(); _NOP(); _NOP();
  }

  attachGDO0Interrupt();

  return  checkCC();

}



/* Check the health of the CC. i.e. The partnum response is OK */
bool CC1101::checkCC(void)
{

  // Do a check of the partnum
  uint8_t version = readReg(CC1101_VERSION, CC1101_STATUS_REGISTER);

  if (version != 20)
  {
    detachGDO0Interrupt();
    
    setPowerDownState();

    // Close the SPI connection
    SPI.end();

    // ALERT
#ifdef ENABLE_BUILTIN_LED    
    digitalWrite(LED_PIN, LOW);
#endif    
    Serial.println(F("Error: CC1101 not detected!"));

    return false;
    /*
    bool led_state = 0;
    while (1)
    {
       Serial.println(F("CC1101 not detected!"));
       delay(1000);
       digitalWrite(LED_BUILTIN, led_state ^= 1);
    } 
    */      

  }

  return true;


} // check CC

/* Attach the interrupt from CC1101 when packet recieved */
void CC1101::attachGDO0Interrupt(void)
{
  if (serialDebug)
    Serial.println(F("Attaching Interrupt to GDO0"));

  attachInterrupt(CC1101_GDO0_interrupt_pin, interupt_packetReceived, FALLING);
}

void CC1101::detachGDO0Interrupt(void)
{
  if (serialDebug)
    Serial.println(F("Detaching Interrupt to GDO0"));

  detachInterrupt(CC1101_GDO0_interrupt_pin);
}


/**
   wakeUp

   Wake up CC1101 from Power Down state
*/
void CC1101::wakeUp(void)
{
  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  cc1101_Deselect();                    // Deselect CC1101
  
  if (serialDebug)
	Serial.println("CC1101 has been woken up.");
  
  attachGDO0Interrupt(); 
  // Reload config just to be sure?
  hardReset();                              // Reset CC1101
  delay(100);  
  setCCregs();
  setIdleState();       // Enter IDLE state before flushing RxFifo (don't need to do this when Overflow has occured)
  delayMicroseconds(1);
  flushRxFifo();        // Flush Rx FIFO  
  flushTxFifo();     
  detachGDO0Interrupt();
  
}

/**
   writeReg

   Write single register into the CC1101 IC via SPI

   'regAddr'  Register address
   'value'  Value to be writen
*/
void CC1101::writeReg(byte regAddr, byte value)
{
  byte status;

  _NOP(); _NOP(); _NOP(); _NOP();  _NOP(); _NOP(); _NOP(); _NOP(); _NOP();   // HACK2

  // Print extra info when we're writing to CC register
  if (regAddr <= CC1101_TEST0) // for some reason when this is disable config writes don't work!!
  {
    char reg_name[16] = {0};
    strcpy_P(reg_name, CC1101_CONFIG_REGISTER_NAME[regAddr]);
    Serial.print(F("Writing to CC1101 reg "));
    Serial.print(reg_name);
    Serial.print(F(" ["));
    Serial.print(regAddr, HEX);
    Serial.print(F("] "));    
    Serial.print(F("value (HEX):\t"));
    Serial.println(value, HEX);
    //    Serial.print(F(" value (DEC) "));
    //    Serial.println(value, DEC);
	
	// Store the configuration state we requested the CC1101 to be
	currentConfig[regAddr] = value;	
  }
  

  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  
  readCCStatus(SPI.transfer(regAddr));  // Send register address
  //delayMicroseconds(1);   // HACK
  _NOP(); _NOP(); _NOP(); _NOP();  _NOP(); _NOP(); _NOP(); _NOP(); _NOP();   // HACK2
  readCCStatus(SPI.transfer(value));    // Send value

  cc1101_Deselect();                    // Deselect CC1101
  delay(1);




}


/**
   cmdStrobe

   Send command strobe to the CC1101 IC via SPI

   'cmd'  Command strobe
*/
void CC1101::cmdStrobe(byte cmd)
{

  if (serialDebug)
  {
    Serial.print(F("Sending strobe: "));
    switch (cmd)
    {
      case 0x30: Serial.println(F("CC1101_SRES      ")); break;
      case 0x31: Serial.println(F("CC1101_SFSTXON   ")); break;
      case 0x32: Serial.println(F("CC1101_SXOFF     ")); break;
      case 0x33: Serial.println(F("CC1101_SCAL      ")); break;
      case 0x34: Serial.println(F("CC1101_SRX       ")); break;
      case 0x35: Serial.println(F("CC1101_STX       ")); break;
      case 0x36: Serial.println(F("CC1101_SIDLE     ")); break;
      case 0x38: Serial.println(F("CC1101_SWOR      ")); break;
      case 0x39: Serial.println(F("CC1101_SPWD      ")); break;
      case 0x3A: Serial.println(F("CC1101_SFRX      ")); break;
      case 0x3B: Serial.println(F("CC1101_SFTX      ")); break;
      case 0x3C: Serial.println(F("CC1101_SWORRST   ")); break;
      case 0x3D: Serial.println(F("CC1101_SNOP      ")); break;
    }
  }


  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  readCCStatus(SPI.transfer(cmd));                    // Send strobe command
  cc1101_Deselect();                    // Deselect CC1101
}

/**
   readReg

   Read CC1101 register via SPI

   'regAddr'  Register address
   'regType'  Type of register: CC1101_CONFIG_REGISTER or CC1101_STATUS_REGISTER

   Return:
    Data byte returned by the CC1101 IC
*/
byte CC1101::readReg(byte regAddr, byte regType)
{
  byte addr, val;

  addr = regAddr | regType;
  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  SPI.transfer(addr);                   // Send register address
  delayMicroseconds(1);  // HACK
  _NOP(); _NOP(); _NOP(); _NOP();_NOP(); _NOP(); _NOP(); _NOP();_NOP(); _NOP(); _NOP(); _NOP();_NOP(); _NOP(); _NOP(); _NOP(); // HACK2
  val = SPI.transfer(0x00);             // Read result
  cc1101_Deselect();                    // Deselect CC1101

  return val;
}


/**
 * 
 * readStatusRegSafe(uint8_t regAddr)
 *  CC1101 bug with SPI and return values of Status Registers
 *  https://e2e.ti.com/support/wireless-connectivity/sub-1-ghz/f/156/t/570498?CC1101-stuck-waiting-for-CC1101-to-bring-GDO0-low-with-IOCFG0-0x06-why-#
 *  as per: http://e2e.ti.com/support/wireless-connectivity/other-wireless/f/667/t/334528?CC1101-Random-RX-FIFO-Overflow
 * 
 */
byte CC1101::readStatusRegSafe(uint8_t regAddr)
{
  byte statusRegByte, statusRegByteVerify;
  
  statusRegByte = readReg(regAddr, CC1101_STATUS_REGISTER);
  do
  {
      statusRegByte       = statusRegByteVerify;
      statusRegByteVerify = readReg(regAddr, CC1101_STATUS_REGISTER);
	  
      //yield();
  }
  while(statusRegByte != statusRegByteVerify);   

  return statusRegByte;
  
}


/**
   hard reset after power on
   Ref: http://e2e.ti.com/support/wireless-connectivity/other-wireless/f/667/t/396609
   Reset CC1101
*/
void CC1101::hardReset(void)
{

  cc1101_Deselect();                    // Deselect CC1101
  delayMicroseconds(5);
  cc1101_Select();                      // Select CC1101
  delayMicroseconds(10);
  cc1101_Deselect();                    // Deselect CC1101
  delayMicroseconds(41);
  cc1101_Select();                      // Select CC1101

  softReset();

}

/**
   soft reset

   Reset CC1101
*/
void CC1101::softReset(void)
{
  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  SPI.transfer(CC1101_SRES);            // Send reset command strobe
  wait_Miso();                          // Wait until MISO goes low
  cc1101_Deselect();                    // Deselect CC1101

  //setCCregs();                          // Reconfigure CC1101
}

/**
   setCCregs

   Configure CC1101 Configuration Registers
*/
void CC1101::setCCregs(void)
{

  if (serialDebug)
    Serial.println(F("Setting CC Configuration Registers..."));

  /* NOTE: Any Configuration registers written here are done so
   * because they aren't changed in any of the subroutines.
   * i.e. They're the same regardless of channel, frequency, etc.
   */
   
  writeReg(CC1101_IOCFG2,   CC1101_DEFVAL_IOCFG2);
  writeReg(CC1101_IOCFG1,   CC1101_DEFVAL_IOCFG1);
  writeReg(CC1101_IOCFG0,   CC1101_DEFVAL_IOCFG0);
  writeReg(CC1101_FIFOTHR,  CC1101_DEFVAL_FIFOTHR);
  writeReg(CC1101_PKTLEN,   CC1101_DEFVAL_PKTLEN);
  writeReg(CC1101_PKTCTRL1, CC1101_DEFVAL_PKTCTRL1);
  writeReg(CC1101_PKTCTRL0, CC1101_DEFVAL_PKTCTRL0);

  // Set default synchronization word
  setSyncWord(syncWord);

  // Set default device address
  setDevAddress(devAddress);

  // Set default frequency channel
  setChannel(channel);

  // Modem freq. control stuff
  //writeReg(CC1101_FSCTRL1,  CC1101_DEFVAL_FSCTRL1);
  writeReg(CC1101_FSCTRL0,  CC1101_DEFVAL_FSCTRL0);

  // Set default carrier frequency = 868 MHz
  setCarrierFreq(carrierFreq);

  // Modem, behaviour
  writeReg(CC1101_MCSM2,    CC1101_DEFVAL_MCSM2);
  writeReg(CC1101_MCSM1,    CC1101_DEFVAL_MCSM1);
  writeReg(CC1101_MCSM0,    CC1101_DEFVAL_MCSM0);

  // Stuff that's most certainly going to get overwritten by dataRate specific stuff.
  //writeReg(CC1101_MDMCFG3,  CC1101_DEFVAL_MDMCFG3);
  writeReg(CC1101_MDMCFG2,  CC1101_DEFVAL_MDMCFG2);
  writeReg(CC1101_MDMCFG1,  CC1101_DEFVAL_MDMCFG1);
  writeReg(CC1101_MDMCFG0,  CC1101_DEFVAL_MDMCFG0);
  //writeReg(CC1101_DEVIATN,  CC1101_DEFVAL_DEVIATN);
  //writeReg(CC1101_FOCCFG,  CC1101_DEFVAL_FOCCFG);
  writeReg(CC1101_BSCFG,  CC1101_DEFVAL_BSCFG);
  writeReg(CC1101_AGCCTRL2,  CC1101_DEFVAL_AGCCTRL2);
  writeReg(CC1101_AGCCTRL1,  CC1101_DEFVAL_AGCCTRL1);
  writeReg(CC1101_AGCCTRL0,  CC1101_DEFVAL_AGCCTRL0);

  writeReg(CC1101_FREND1,  CC1101_DEFVAL_FREND1);
  writeReg(CC1101_FREND0,  CC1101_DEFVAL_FREND0);


  // Data Rate - details extracted from SmartRF Studio
  switch (dataRate)
  {
    case KBPS_250:

      if (serialDebug)
        Serial.print(F("250kbps data rate"));

      writeReg(CC1101_FSCTRL1, 0x0C); // Frequency Synthesizer Control (optimised for sensitivity)
      writeReg(CC1101_MDMCFG4, 0x2D); // Modem Configuration      
      writeReg(CC1101_MDMCFG3, 0x3B); // Modem Configuration
      //writeReg(CC1101_MDMCFG2, 0x93); // Modem Configuration
      writeReg(CC1101_DEVIATN, 0x62); // Modem Deviation Setting
      writeReg(CC1101_FOCCFG, 0x1D); // Frequency Offset Compensation Configuration
      writeReg(CC1101_BSCFG, 0x1C); // Bit Synchronization Configuration
      writeReg(CC1101_AGCCTRL2, 0xC7); // AGC Control
      writeReg(CC1101_AGCCTRL1, 0x00); // AGC Control
      writeReg(CC1101_AGCCTRL0, 0xB0); // AGC Control
      writeReg(CC1101_FREND1, 0xB6); // Front End RX Configuration
      break;

    case KBPS_38:

      if (serialDebug)
        Serial.print(F("38kbps data rate"));

      writeReg(CC1101_FSCTRL1, 0x06); // Frequency Synthesizer Control
      writeReg(CC1101_MDMCFG4, 0xCA); // Modem Configuration      
      writeReg(CC1101_MDMCFG3, 0x83); // Modem Configuration
      //writeReg(CC1101_MDMCFG2, 0x93); // Modem Configuration
      writeReg(CC1101_DEVIATN, 0x35); // Modem Deviation Setting
      writeReg(CC1101_FOCCFG, 0x16); // Frequency Offset Compensation Configuration
      writeReg(CC1101_AGCCTRL2, 0x43); // AGC Control
      break;

    case KBPS_4:

      if (serialDebug)
        Serial.print(F("4kbps data rate"));

      writeReg(CC1101_FSCTRL1, 0x06); // Frequency Synthesizer Control
      writeReg(CC1101_MDMCFG4, 0xC7); // Modem Configuration      
      writeReg(CC1101_MDMCFG3, 0x83); // Modem Configuration
      //writeReg(CC1101_MDMCFG2, 0x93); // Modem Configuration
      writeReg(CC1101_DEVIATN, 0x40); // Modem Deviation Setting
      writeReg(CC1101_FOCCFG, 0x16); // Frequency Offset Compensation Configuration
      writeReg(CC1101_AGCCTRL2, 0x43); // AGC Control
      break;
  }


  // Other defaults
  writeReg(CC1101_WOREVT1,  CC1101_DEFVAL_WOREVT1);
  writeReg(CC1101_WOREVT0,  CC1101_DEFVAL_WOREVT0);
  writeReg(CC1101_WORCTRL,  CC1101_DEFVAL_WORCTRL);

  writeReg(CC1101_FSCAL3,  CC1101_DEFVAL_FSCAL3);
  writeReg(CC1101_FSCAL2,  CC1101_DEFVAL_FSCAL2);
  writeReg(CC1101_FSCAL1,  CC1101_DEFVAL_FSCAL1);
  writeReg(CC1101_FSCAL0,  CC1101_DEFVAL_FSCAL0);

  writeReg(CC1101_RCCTRL1,  CC1101_DEFVAL_RCCTRL1);
  writeReg(CC1101_RCCTRL0,  CC1101_DEFVAL_RCCTRL0);
  //writeReg(CC1101_FSTEST,  CC1101_DEFVAL_FSTEST);
  //writeReg(CC1101_PTEST,  CC1101_DEFVAL_PTEST);
  //writeReg(CC1101_AGCTEST,  CC1101_DEFVAL_AGCTEST);
  //writeReg(CC1101_TEST2,  CC1101_DEFVAL_TEST2);
  //writeReg(CC1101_TEST1,  CC1101_DEFVAL_TEST1);
  //writeReg(CC1101_TEST0,  CC1101_DEFVAL_TEST0);

  // Send empty packet (which won't actually send a packet, but will flush the Rx FIFO only.)
  CCPACKET packet;
  packet.payload_size = 0;
  sendPacket(packet);
}


/**
   setSyncWord

   Set synchronization word

   'syncH'  Synchronization word - High byte
   'syncL'  Synchronization word - Low byte
*/
void CC1101::setSyncWord(uint8_t syncH, uint8_t syncL)
{
  writeReg(CC1101_SYNC1, syncH);
  writeReg(CC1101_SYNC0, syncL);
  syncWord[0] = syncH;
  syncWord[1] = syncL;
}

/**
   setSyncWord (overriding method)

   Set synchronization word

   'syncH'  Synchronization word - pointer to 2-byte array
*/
void CC1101::setSyncWord(byte *sync)
{
  CC1101::setSyncWord(sync[0], sync[1]);
}

/**
   setDevAddress

   Set device address

   @param addr  Device address
*/
void CC1101::setDevAddress(byte addr)
{
  writeReg(CC1101_ADDR, addr);
  devAddress = addr;
}

/**
   setChannel

   Set frequency channel

   'chnl' Frequency channel
*/
void CC1101::setChannel(byte chnl)
{
  writeReg(CC1101_CHANNR,  chnl);
  channel = chnl;
}

/**
   setCarrierFreq

   Set carrier frequency

   'freq' New carrier frequency
*/
void CC1101::setCarrierFreq(CFREQ freq)
{
  switch (freq)
  {
    case CFREQ_922:

      if (serialDebug)
        Serial.print(F("922Mhz frequency (Canada, US, Australia)"));

      writeReg(CC1101_FREQ2,  CC1101_DEFVAL_FREQ2_922);
      writeReg(CC1101_FREQ1,  CC1101_DEFVAL_FREQ1_922);
      writeReg(CC1101_FREQ0,  CC1101_DEFVAL_FREQ0_922);
      break;
    case CFREQ_433:

      if (serialDebug)
        Serial.print(F("433Mhz frequency"));

      writeReg(CC1101_FREQ2,  CC1101_DEFVAL_FREQ2_433);
      writeReg(CC1101_FREQ1,  CC1101_DEFVAL_FREQ1_433);
      writeReg(CC1101_FREQ0,  CC1101_DEFVAL_FREQ0_433);
      break;
    default:

      if (serialDebug)
        Serial.print(F("868Mhz frequency (Europe)"));


      writeReg(CC1101_FREQ2,  CC1101_DEFVAL_FREQ2_868);
      writeReg(CC1101_FREQ1,  CC1101_DEFVAL_FREQ1_868);
      writeReg(CC1101_FREQ0,  CC1101_DEFVAL_FREQ0_868);
      break;
  }

  carrierFreq = freq;
}

/**
   setPowerDownState

   Put CC1101 into power-down state
*/
void CC1101::setPowerDownState()
{
  // Comming from RX state, we need to enter the IDLE state first
  cmdStrobe(CC1101_SIDLE);
  // Enter Power-down state
  cmdStrobe(CC1101_SPWD);
}
/**
   sendChars

   send a stream of characters, could be greater than one CC1101 underlying ~60 byte packet,
   if so, split it up and do what is required.
*/
bool CC1101::sendChars(const char * data, uint8_t cc_dest_address)
{
	
  uint16_t stream_length       = strlen(data) + 1;   // We also need to include the 0 byte at the end of the string

  return sendBytes( (byte *) data, stream_length, cc_dest_address);
}


/**
   sendBytes

   send a stream of BYTES (unsigned chars), could be greater than one CC1101 underlying packet,
   payload limit (STREAM_PKT_MAX_PAYLOAD_SIZE) if so, split it up and do what is required.
*/
bool CC1101::sendBytes(byte * data, uint16_t stream_length,  uint8_t cc_dest_address)
{
  CCPACKET packet; // create a packet
  
  unsigned long start_tm; start_tm = millis();
  bool sendStatus = false;
  
/*
	Serial.println("");
	// HACK: REMOVE - FOR VALIDATION TESTING ONLY
	Serial.println("");
	Serial.print(F("HEX content of data to send:"));
	for (int i = 0; i < stream_length; i++)
	{
		Serial.printf("%02x", data[i]);
	}	 
	Serial.println("");
		Serial.print(F("CHAR content of data to send:"));
	for (int i = 0; i < stream_length; i++)
	{
		Serial.printf("%c", data[i]);
	}	
	Serial.println("");  
*/
	
  detachGDO0Interrupt(); // we don't want to get interrupted at this important moment in history
  
  setIdleState();       // Enter IDLE state before flushing RxFifo (don't need to do this when Overflow has occured)
  delayMicroseconds(1);
  flushRxFifo();        // Flush Rx FIFO

  uint16_t unsent_stream_bytes = stream_length;

  if (stream_length > MAX_STREAM_LENGTH) // from ccpacket.h
  {
    Serial.println(F("Too many bytes to send!"));
    return false;
  }

#ifdef SERIAL_INFO
  Serial.print(F("Sending byte stream of ")); Serial.print(stream_length, DEC); Serial.println(F(" bytes in length."));
#endif

  uint8_t  stream_pkt_seq_num = 1;
  uint8_t  stream_num_of_pkts = 0; // we have to send at least one!

  // calculate number of packets this stream will have
  for (int i = 0; i < stream_length; i = i + STREAM_PKT_MAX_PAYLOAD_SIZE)
    stream_num_of_pkts++;

  while (unsent_stream_bytes > 0)
  {

    // can't use sizeof
    //https://stackoverflow.com/questions/6081842/sizeof-is-not-executed-by-preprocessor
    uint8_t payload_size = MIN(STREAM_PKT_MAX_PAYLOAD_SIZE, unsent_stream_bytes);

    packet.payload_size	        	= payload_size; // CCPACKET_OVERHEAD_STREAM added in sendPacket
    packet.cc_dest_address  		= cc_dest_address; // probably going to broadcast this most of the time (0x00, or 0xFF)
    packet.stream_num_of_pkts  		= stream_num_of_pkts;
    packet.stream_pkt_seq_num  		= stream_pkt_seq_num++;

    // make sure we flush the contents of the packet (otherwise we might append crap from the previous packet in the stream 
    // if this packet is smaller (i.e. the last packet). 
    // Not a big issue though as the receiver should only read up until payload_size)
    memset(packet.payload, 0x00, sizeof(packet.payload));  
    
    uint16_t start_pos = stream_length - unsent_stream_bytes; // (stream_length == unsent_stream_bytes) ? 0:(stream_length-1-unsent_stream_bytes); // because array positions are x-1
	
	//Serial.printf("Packet Start Pos: %d\n", start_pos);
	
    memcpy( (byte *)packet.payload, &data[start_pos], payload_size); // cast as char
/*	
	// REMOVE - FOR VALIDATION TESTING ONLY

	Serial.println("");
	
	Serial.print("HEX content of packet to send:");
	for (uint8_t i = 0; i < packet.payload_size; i++)
	{
		Serial.printf("%02x", packet.payload[i]);
	}	
	Serial.println("");
*/	
	
    // Try and send the packet stream.
    uint8_t tries = 0; 
	
    // Check that the RX state has been entered
    while (tries++ < 3 && ((sendStatus = sendPacket(packet)) != true) )
    {
      Serial.println(F("Failed to send byte packet. Retrying. "));
      delay(10); // random delay interval
    }

    if ( !sendStatus )
    {
      Serial.println(F("Failed to send byte stream. Existing."));
      break; // Get out of this 
    }

    unsent_stream_bytes -= payload_size;

    if (serialDebug){ 
	  Serial.print(unsent_stream_bytes, DEC);
      Serial.println(F(" bytes remain unsent."));
	}

  } // end stream loop

  attachGDO0Interrupt();
  
  if (serialDebug){
	Serial.print((millis() - start_tm), DEC);
	Serial.println(F(" milliseconds to complete sendBytes()"));  
  }

  return sendStatus;
}


/**
   sendPacket

   Send data packet via RF

   'packet' Packet to be transmitted. First byte is the destination address

    Return:
      True if the transmission succeeds
      False otherwise
*/
bool CC1101::sendPacket(CCPACKET packet)
{
	byte marcState;
    byte txBytes, txOverflow;  
	bool res = false;

#ifdef ENABLE_BUILTIN_LED
  digitalWrite(LED_PIN, LOW);
#endif

 /**
  * STEP 0: Build the radio packet of stuff to send
  */

  // Flush the RX/TX Buffer
  memset(cc1101_rx_tx_fifo_buff, 0x00, sizeof(cc1101_rx_tx_fifo_buff));  
  
  // Set dest device address as first position of TX FIFO
  //writeReg(CC1101_TXFIFO,  packet.cc_dest_address); // byte 1
  cc1101_rx_tx_fifo_buff[0] = packet.cc_dest_address;
  
  // Set payload size as the second position of the TX FIFO
  //writeReg(CC1101_TXFIFO,  packet.payload_size); // byte 2
  cc1101_rx_tx_fifo_buff[1] = packet.payload_size;
  
  // Stream stuff
  //writeReg(CC1101_TXFIFO,  packet.stream_num_of_pkts); // byte 3
  //writeReg(CC1101_TXFIFO,  packet.stream_pkt_seq_num); // byte 4
  cc1101_rx_tx_fifo_buff[2] = packet.stream_num_of_pkts;
  cc1101_rx_tx_fifo_buff[3] = packet.stream_pkt_seq_num;
  
  // Copy the payload
  memcpy( &cc1101_rx_tx_fifo_buff[4], packet.payload, packet.payload_size);

  /*
  Serial.print("Payload Data: ");
  for (int i = 0; i < packet.payload_size; i++)
  {
    Serial.print(packet.payload[i], HEX);
    Serial.print(", ");
  }
  Serial.println("");
  */

  
      

 /**
  * STEP 1: Check that the marcState of the CC1101 is RX, and that none of the buffers have
  * overflowed.
  */ 

	// Enter RX state (we might receive a packet whilst getting ready to TX)
	setRxState();
	
	int tries = 0;
	while (tries++ < 1000 && ((marcState = readStatusRegSafe(CC1101_MARCSTATE)) & 0b11111) != MARCSTATE_RX)
	{
		//setRxState();	
		//flushTxFifo();    

		if (marcState == MARCSTATE_RXFIFO_OVERFLOW)        // RX_OVERFLOW
			flushRxFifo();              // flush receive queue

		if (marcState == MARCSTATE_TXFIFO_UNDERFLOW)        // TX_UNDERFLOW
			flushTxFifo();              // flush send queue

		// Page 31, Table 23.
		//   110 RXFIFO_OVERFLOW RX FIFO has overflowed. Read out any useful data, then flush the FIFO with SFRX
		//   111 TXFIFO_UNDERFLOW TX FIFO has underflowed. Acknowledge with SFTX
		//
		
		setRxState();
		
	}
	if (tries >= 100) 
	{
		// TODO: MarcState sometimes never enters the expected state; this is a hack workaround.
		return false;
	}
	

  /* 
   *  Step 2: Check to see if stuff is already in the TX FIFO. If so. Flush it. 
   */
  txBytes = readStatusRegSafe(CC1101_TXBYTES); // Any left over bytes in the TX FIFO?

  // Repurpose these variables
  txBytes     = txBytes & BYTES_IN_FIFO;
  txOverflow  = txBytes & OVERFLOW_IN_FIFO;
  
  //Serial.print("TX FIFO bytes already in FIFO (should be zero): ");
  //Serial.println(txBytes, DEC); 

  if (txBytes != 0) // only do this stuff if it's empty already
  {
    if (serialDebug)
      Serial.println(F("TX FIFO is in overflow or contains garbage. Flushing. "));
    
    setIdleState();       // Enter IDLE state
    flushTxFifo();        // Flush Tx FIFO
    setRxState();         // Back to RX state

  //  return false;    
      
  }

 /**
  * STEP 3: Send the radio packet.
  */  
                    
	
	/*
		15.4 Packet Handling in Transmit Mode
		The payload that is to be transmitted must be
		written into the TX FIFO. The first byte written
		must be the length byte when variable packet
		length is enabled. The length byte has a value
		equal to the payload of the packet (including
		the optional address byte). If address
		recognition is enabled on the receiver, the
		second byte written to the TX FIFO must be
		the address byte.
		If fixed packet length is enabled, the first byte
		written to the TX FIFO should be the address
		(assuming the receiver uses address
		recognition).	
	 */ 

	/* ISSUE: writeBurstReg doesn't work properly on ESP8266, or perhaps
	   other microcontrollers... perhaps they are too fast for the CC1101
	   given it's a 10+ year old chip.

	   Sending each byte individually works without issue however.

	   https://e2e.ti.com/support/wireless-connectivity/sub-1-ghz/f/156/t/554535
	   https://e2e.ti.com/support/microcontrollers/other/f/908/t/217117
	*/
	// writeBurstReg(CC1101_TXFIFO, packet.data, packet.length);

  // Send the contents of th RX/TX buffer to the CC1101, one byte at a time
  // the receiving CC1101 will append two bytes for the LQI and RSSI
  for (int i = 0; i< CCPACKET_MAX_SIZE; i++)
      writeReg(CC1101_TXFIFO,  cc1101_rx_tx_fifo_buff[i]);

  /*
	for (uint8_t len = 0 ; len < packet.payload_size; len++)
	  writeReg(CC1101_TXFIFO,  packet.payload[len]);

  // We're using fixed packet size, so need to fill with null until end
  for (uint8_t len = packet.payload_size ; len < STREAM_PKT_MAX_PAYLOAD_SIZE; len++)
    writeReg(CC1101_TXFIFO,  0x00); 
  */

	// I assume the CC1101 sends the two extra CRC bytes here somewhere.
	if (serialDebug)
	{
		Serial.print(F(">> Number of bytes to send: "));
		Serial.println(readStatusReg(CC1101_TXBYTES) & 0x7F, DEC);
	}


	// CCA enabled: will enter TX state only if the channel is clear
	setTxState();
	delay(1);

	// Check that TX state is being entered (state = RXTX_SETTLING)
	// Could also have been completed already and gone to IDLE as per the 0x18: MCSM0– Main Radio Control State Machine Configuration !
	/*
	if ((marcState != MARCSTATE_TX) && (marcState != MARCSTATE_TX_END) && (marcState != MARCSTATE_RXTX_SWITCH) && (marcState != MARCSTATE_IDLE) )
	{
		

		setIdleState();       // Enter IDLE state
		flushTxFifo();        // Flush Tx FIFO
		setRxState();         // Back to RX state
	}
	*/


  if (currentConfig[CC1101_IOCFG0] == 0x06) //if sync word detect mode is used
  {        
	if (serialDebug)
		Serial.println(F("Sync word mode enabled"));

    // Wait for the sync word to be transmitted
    // wait_GDO0_high(); // should have already happened!!
  
    // Wait until the end of the packet transmission
    wait_GDO0_low();
  }
  
    if (serialDebug)
    {
		Serial.print(F("Bytes remaining in TX FIFO (should be zero):"));
		Serial.println((readStatusRegSafe(CC1101_TXBYTES) & 0x7F), DEC);		
	}

	// Check that the TX FIFO is empty
	if ((readStatusRegSafe(CC1101_TXBYTES) & 0x7F) == 0)
		res = true;

	setIdleState();       // Enter IDLE state
	flushTxFifo();        // Flush Tx FIFO

	// Enter back into RX state
	setRxState();
	
    if (serialDebug)
    {
		if (res == true) 
			Serial.println(F(">>> TX COMPLETED SUCCESSFULLY."));
	}	
	


#ifdef ENABLE_BUILTIN_LED
  digitalWrite(LED_PIN, HIGH);
#endif

	return res;
}




/**
   dataAvailable

   Check the status of the packetReceived and do some processing
   to reconstruct the multi-packet 'stream' if required.
   
   There is no state machine here so if multiple packets come from
   different sources at once for multiple multi-packet streams,
   things will break and/or get corrupted very easily.

*/
bool CC1101::dataAvailable(void)
{ 

/*
  if (currentState != STATE_RX) {
	setRxState(); 
	//Serial.print(".");
  }
*/		
  // Any packets?
  if (!packetReceived)
  {
	  // A double layer of protection every 5 seconds or so
	  // CC1100 will seemingly just randomly stop sending interrupts when RX FIFO full.
	  if ( (millis() - last_CCState_check) > 5000)
	  {
		byte rxBytes = readStatusRegSafe(CC1101_RXBYTES) & 0b01111111;
		if (rxBytes > 60) 
		{
			packetReceived = true;
			last_CCState_check = millis();
		}
	  } 
	  
	  return false;
  }
  
  bool _streamReceived = false;

#ifdef SERIAL_INFO
  Serial.println(F("---------- START: RX Interrupt Request  -------------"));
#endif  

  // We got something
  detachGDO0Interrupt(); // we don't want to get interrupted at this important moment in history
  packetReceived = false;

  CCPACKET packet;

  if (receivePacket(&packet) > 0)
  {
	receivedRSSI = decodeCCRSSI(packet.rssi);

#ifdef SERIAL_INFO	  
    Serial.println(F("Processing packet in dataAvailable()..."));

    if (!packet.crc_ok)
    {
      Serial.println(F("CRC not ok!"));
    }
	
    Serial.print(F("lqi: ")); 	Serial.println(decodeCCLQI(packet.lqi));
    Serial.print(F("rssi: ")); 	Serial.print(decodeCCRSSI(packet.rssi)); Serial.println(F("dBm"));
#endif	
	


    if (packet.crc_ok && packet.payload_size > 0)
    {
#ifdef SERIAL_INFO	  		
      Serial.print(F("stream_num_of_pkts: "));
      Serial.println(packet.stream_num_of_pkts, DEC);
      
      Serial.print(F("stream_pkt_seq_num: "));
      Serial.println(packet.stream_pkt_seq_num, DEC);
      
	  Serial.print(F("Payload_size: "));
	  Serial.println(packet.payload_size, DEC);
	  Serial.print(F("Data: "));
	  Serial.println((const char *) packet.payload);
#endif	  

      // This data stream was only one packets length (i.e. < 57 bytes or so)
      // therefore we just copy to the buffer and we're all good!
	  
	    // Note: Packets from rougue devices / multiple transmitting at the same time can easily break this code.
      if (packet.stream_num_of_pkts == 1 && packet.stream_pkt_seq_num == 1 ) // It's a one packet wonder
      {
        // We got the first packet in the stream so flush the buffer...
        memset(stream_multi_pkt_buffer, 0, sizeof(stream_multi_pkt_buffer)); // flush
        memcpy(stream_multi_pkt_buffer, packet.payload, packet.payload_size);
		
#ifdef SERIAL_INFO	 		
		Serial.println(F("Single packet stream has been received."));
#endif		
		receivedStreamSize = packet.payload_size;

        _streamReceived = true;
      }
      else // Stream
      {
			// TODO: deal with out-of-order packets, or different packets received from multiple 
			//       senders at the same time. Does it matter? We're not trying to implement TCP/IP here.
			if (packet.stream_pkt_seq_num == 1)
				memset(stream_multi_pkt_buffer, 0, sizeof(stream_multi_pkt_buffer)); // flush
			
			// Copy to stream_multi_pkt_buffer to a limit of MAX_STREAM_LENGTH-1
			unsigned int buff_start_pos = packet.stream_pkt_seq_num-1;
			buff_start_pos *= STREAM_PKT_MAX_PAYLOAD_SIZE;
      
			unsigned int buff_end_pos 	= buff_start_pos + (unsigned int) packet.payload_size;
			
			// HACK: REMOVE
#ifdef SERIAL_INFO	 			
			Serial.print("HEX content of packet:");
			for (int i = 0; i < packet.payload_size; i++)
			{
				Serial.print(packet.payload[i], HEX); Serial.print(" ");
			}
#endif			

			
		//	if (serialDebug)
#ifdef SERIAL_INFO	 		
			Serial.printf("Received stream packet %d of %d. Buffer start position: %d, end position %d, payload size: %d\r\n", (int)packet.stream_pkt_seq_num, (int)packet.stream_num_of_pkts, (int)buff_start_pos, (int)buff_end_pos, (int) packet.payload_size);
#endif			

			if ( buff_end_pos > MAX_STREAM_LENGTH)
			{
				if (serialDebug)
					Serial.println(F("Received a stream bigger than allowed. Dropping."));				
			}
			else
			{
				memcpy( &stream_multi_pkt_buffer[buff_start_pos], packet.payload, packet.payload_size); // copy packet payload  
				
				// Are we at the last packet?
				if (packet.stream_num_of_pkts ==  packet.stream_pkt_seq_num)
				{
#ifdef SERIAL_INFO	            
					Serial.printf("Multi-packet stream of %d bytes hase been received in full!\n", buff_end_pos);
#endif					
					receivedStreamSize = buff_end_pos;
					_streamReceived = true;
				}
				
			} // end buffer size check
			

      } // end single packet stream check or not.

    } // end packet length & crc check
	
  } // packet isn't just 0's check

  attachGDO0Interrupt();
  
#ifdef SERIAL_INFO  
  Serial.println(F("---------- END: RX Interrupt Request  -------------"));
#endif

  return _streamReceived;
}



/**
   getChars

   Returns a char pointer to the RX buffer.

*/
char * CC1101::getChars(void)
{
  return (char * ) stream_multi_pkt_buffer;
}

/**
   getBytes

   Returns a byte pointer to the RX buffer.

*/
byte * CC1101::getBytes(void)
{
  return (byte * ) stream_multi_pkt_buffer;
}

/* Get the number of bytes in the stream.
   This is always <= MAX_STREAM_LENGTH
 */
uint16_t CC1101::getSize(void)
{
	return receivedStreamSize;
}



/**
   receivePacket

   Read data packet from RX FIFO

   'packet' Container for the packet received

   Return:
    Amount of bytes received
*/
byte CC1101::receivePacket(CCPACKET * packet) //if RF package received
{
  unsigned long start_tm, end_tm;
  byte val;
  byte rxBytes, rxBytesVerify, rxOverflow;   


  /* 
   *  STEP 1: Check that what we have in the RX buffer makes sense, for this library
   *  we are always sending 61 bytes of payload + 2 bytes for RSSI, so 63 bytes in
   *  total for every radio packet.
   */

  start_tm = millis();

#ifdef SERIAL_INFO	 
  Serial.print(F("* Packet Received on channel: "));
  Serial.println(channel, DEC);
#endif  

#ifdef ENABLE_BUILTIN_LED
  digitalWrite(LED_PIN, LOW);  
#endif
  if (currentConfig[CC1101_IOCFG0] == 0x06) //if sync word detect mode is used
  {
    if (serialDebug)
      Serial.println(F("* Sync Word Wait"));
		
  	// Reference: https://github.com/SpaceTeddy/CC1101
  	wait_Miso();
  }

  _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); _NOP(); 

  
  rxBytes = readStatusReg(CC1101_RXBYTES); // Unread bytes in RX FIFO according to CC1101. TODO: Need to do this safely
  /*
  // as per: http://e2e.ti.com/support/wireless-connectivity/other-wireless/f/667/t/334528?CC1101-Random-RX-FIFO-Overflow
  do
  {
      rxBytes = rxBytesVerify;
      rxBytesVerify = readStatusReg(CC1101_RXBYTES);
  }
  while(rxBytes != rxBytesVerify);   
  */
  
  // Repurpose these variables
  rxBytes     = rxBytes & BYTES_IN_FIFO;
  rxOverflow  = rxBytes & OVERFLOW_IN_FIFO;
  
#ifdef SERIAL_INFO	   
  Serial.print(F("* RX FIFO bytes pending read: "));
  Serial.println(rxBytes, DEC);
#endif  
/*
  ---------- START: RX Interrupt Request  -------------
  Packet Received
  RX FIFO bytes pending read: 63
  ---------- END RX Interrupt Request -------------
 */

 /* 
  *  From the documentation:
  *  
  *  The TX FIFO may be flushed by issuing a
  *  SFTX command strobe. Similarly, a SFRX
  *  command strobe will flush the RX FIFO. A
  *  SFTX or SFRX command strobe can only be
  *  issued in the IDLE, TXFIFO_UNDERFLOW, or
  *  RXFIFO_OVERFLOW states. Both FIFO
  *  
  */

 if ( rxOverflow )
 {
      
    flushRxFifo();        // Flush Rx FIFO
  
    // Back to RX state
    setRxState();

    packet->payload_size = 0;

#ifdef SERIAL_INFO	 
    Serial.println(F("RX was in overflow. Flushing."));
#endif	
    
    return packet->payload_size;   
 }


 /**
  * STEP 2: Got the right number of bytes in RX FIFO. Might be one for us?
  */
 if ( rxBytes == CCPACKET_REC_SIZE  )
 {

    // Copy contents of FIFO in the buffer from CC1101 
    memset(cc1101_rx_tx_fifo_buff, 0x00, sizeof(cc1101_rx_tx_fifo_buff)); // flush the temporary array.
//    readBurstReg(cc1101_rx_tx_fifo_buff, CC1101_RXFIFO, 63); // Can't use readburst due to bizzare SPI issues

    // Copy contents of FIFO in the buffer from CC1101 one byte at a time
    for (int i = 0; i< CCPACKET_REC_SIZE; i++)
        cc1101_rx_tx_fifo_buff[i] = readConfigReg(CC1101_RXFIFO);
            

    /*
     
          /// TEST CODE
          for (int i = 0; i< 63; i++)
            readConfigReg(CC1101_RXFIFO);
        
          flushRxFifo();        // Flush Rx FIFO
          
          // Back to RX state
          setRxState();

          packet->payload_size = 0;

          return packet->payload_size;
          /// TEST CODE          
     */
  
     packet->cc_dest_address      = cc1101_rx_tx_fifo_buff[0]; // byte 1     
     packet->payload_size         = cc1101_rx_tx_fifo_buff[1]; // byte 2

#ifdef SERIAL_INFO
     if (packet->cc_dest_address == BROADCAST_ADDRESS)
      Serial.println(F("* Received broadcast packet"));
#endif	  
     
    if (serialDebug)
    {
      Serial.print(F("Payload size is: "));
      Serial.println(packet->payload_size, DEC);
    }

    // The payload size contained within this radio packet is too big?
    if ( packet->payload_size > STREAM_PKT_MAX_PAYLOAD_SIZE ) 
	  {
		  packet->payload_size = 0;   // Discard packet

      if (serialDebug)
         Serial.println(F("Error: Receieved packet too big or payload length non sensical!"));
    }
    else
    {

      packet->stream_num_of_pkts      = cc1101_rx_tx_fifo_buff[2]; // byte 3
      packet->stream_pkt_seq_num      = cc1101_rx_tx_fifo_buff[3]; // byte 4


      // Want to reduce the 'length' field for high-order functions like dataAvailable(), otherwise we end up reading the RSSI & LQI as characters!
      memcpy( packet->payload, &cc1101_rx_tx_fifo_buff[4], packet->payload_size);
      //readBurstReg(packet->payload, CC1101_RXFIFO, rxBytes-4);

      /*
      Serial.print("Payload Data: ");
      for (int i = 0; i < packet->payload_size; i++)
      {
        Serial.print(packet->payload[i], HEX);
        Serial.print(", ");
      }
      Serial.println("");
      */

      // Read RSSI
      packet->rssi = cc1101_rx_tx_fifo_buff[CCPACKET_REC_SIZE-2];

      // Read LQI and CRC_OK
      val = cc1101_rx_tx_fifo_buff[CCPACKET_REC_SIZE-1]; // 62 in array speak (which is 63 in real life)
      

      // Read RSSI
      // packet->rssi = readConfigReg(CC1101_RXFIFO); //byte 5 + n + 1

      // Read LQI and CRC_OK
      // val = readConfigReg(CC1101_RXFIFO); // byte 5 + n + 2

      packet->lqi = val & 0x7F;
      packet->crc_ok = bitRead(val, 7);
    }
  }
 else
 {           
    packet->payload_size = 0;
 }

  /* 
   *  STEP 3: We're done, so flush the RxFIFO.
   */
   
  setIdleState();       // Enter IDLE state before flushing RxFifo (don't need to do this when Overflow has occured)
  delayMicroseconds(1);
  flushRxFifo();        // Flush Rx FIFO
  //cmdStrobe(CC1101_SCAL);

  // Read what's left, this should be zero?
  rxBytes = readStatusReg(CC1101_RXBYTES); // Unread bytes in RX FIFO according to CC1101. TODO: Need to do this safely
  
  // Repurpose these variables
  rxBytes     = rxBytes & BYTES_IN_FIFO;
  rxOverflow  = rxBytes & OVERFLOW_IN_FIFO;

#ifdef SERIAL_INFO	 
  if ( rxBytes != 0 )
  {
    Serial.print(F("Error: Bytes left over in RX FIFO: "));
    Serial.println(rxBytes, DEC);  
  }
#endif  

  
  // Back to RX state
  setRxState();

#ifdef SERIAL_INFO	 
  Serial.printf("Took %d milliseconds to complete recievePacket()\n", (millis() - start_tm));
#endif  

#ifdef ENABLE_BUILTIN_LED
  digitalWrite(LED_PIN, HIGH);
#endif

  return packet->payload_size;
}

/**
   setRxState

   Enter Rx state
*/
void CC1101::setRxState(void)
{
  cmdStrobe(CC1101_SRX);
}

/**
   setTxState

   Enter Tx state
*/
void CC1101::setTxState(void)
{
  cmdStrobe(CC1101_STX);
}

/**
 * setOutputPowerLevel
 * 
 * Sets the output power level, parameter passed is a dBm value wanted.
 */
void CC1101::setOutputPowerLeveldBm(int8_t dBm)
{	
    uint8_t pa_table_index 	= 4; // use entry 4 of the patable_power_XXX static array. i.e. dBm of 0
	uint8_t pa_value 		= 0x50;

	// Calculate the index offset of our pa table values
    if      (dBm <= -30) pa_table_index = 0x00;
    else if (dBm <= -20) pa_table_index = 0x01;
    else if (dBm <= -15) pa_table_index = 0x02;
    else if (dBm <= -10) pa_table_index = 0x03;
    else if (dBm <= 0)   pa_table_index = 0x04;
    else if (dBm <= 5)   pa_table_index = 0x05;
    else if (dBm <= 7)   pa_table_index = 0x06;
    else if (dBm <= 10)  pa_table_index = 0x07;
	
	// now pick the right PA table values array to 
	switch (carrierFreq)
	{
		case CFREQ_922:

		  if (serialDebug)
			Serial.print(F("Using PA Table for 922Mhz frequency (Canada, US, Australia)"));
		
		  pa_value = patable_power_9XX[pa_table_index];
		  break;
		  
		case CFREQ_433:

		  if (serialDebug)
			Serial.print(F("Using PA Table for 433Mhz frequency"));

		  pa_value = patable_power_433[pa_table_index];
		  break;
		  
		default:

		  if (serialDebug)
			Serial.print(F("Using PA Table for 868Mhz frequency (Europe)"));


		  pa_value = patable_power_868[pa_table_index];
		  break;
		  
	  }
	  
	Serial.print(F("Setting PATABLE0 value to: "));
	Serial.println(pa_value, HEX);
	  
	// Now write the value
	for (uint8_t i = 0; i < 8; i++)
		writeReg(CC1101_PATABLE,  pa_value); // Set all the PA tables to the same value


} // end of setOutputPowerLevel
	
	
	

/**
	Print the PA Table Values to the Serial Console
*/

void CC1101::printPATable(void)
{
  //Serial.print(F("Printing PA Table Value:"));
/*  Serial.println(readReg(CC1101_PARTNUM, CC1101_STATUS_REGISTER));
  Serial.print(F("CC1101_VERSION "));
  Serial.println(readReg(CC1101_VERSION, CC1101_STATUS_REGISTER));
  Serial.print(F("CC1101_MARCSTATE "));
  Serial.println(readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER) & 0x1f);
  */
    
  byte reg_value    = 0;  

  Serial.println(F("--------- CC1101 PA Table Dump --------- "));
  for (uint8_t i = 0; i < 8; i++)
  {
    Serial.print(F("PA Table entry "));
    Serial.print(i);
    Serial.print(F(" = "));

	// reg_value = readStatusRegSafe(CC1101_PATABLE);
    reg_value = readReg(CC1101_PATABLE, CC1101_CONFIG_REGISTER); // CC1101_CONFIG_REGISTER = READ_SINGLE_BYTE
    Serial.println(reg_value, HEX);
	
    delay(10);
  }
  
} // end printPATable



/**
	Print the Configuration Values and Check Them
*/
bool CC1101::printCConfigCheck(void)
{
  Serial.println(F("--------- Checking Key CC1101 h/w values --------- "));
  
  Serial.print(F("CC1101_PARTNUM: "));
  Serial.println(readReg(CC1101_PARTNUM, CC1101_STATUS_REGISTER));
  Serial.print(F("CC1101_VERSION [Expect 20]: "));
  Serial.println(readReg(CC1101_VERSION, CC1101_STATUS_REGISTER));
  Serial.print(F("CC1101_MARCSTATE: "));
  Serial.println(readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER) & 0x1f);

  // Register validation check
  bool reg_check_pass = true;

  char reg_name[16] = {0};
  byte reg_value    = 0;

  Serial.println(F("--------- CC1101 Register Configuration Dump --------- "));
  for (uint8_t i = 0; i < NUM_CONFIG_REGISTERS; i++)
  {
    Serial.print(F("Reg "));
    strcpy_P(reg_name, CC1101_CONFIG_REGISTER_NAME[i]);
    Serial.print(reg_name);
    Serial.print(F(" ( "));
    Serial.print(i, HEX);
    Serial.print(F(" ) "));
    Serial.print(F(" = "));

    reg_value = readReg(i, CC1101_CONFIG_REGISTER);
    Serial.println(reg_value, HEX);

    if (( currentConfig[i] != reg_value) &&  (i <= CC1101_RCCTRL0) ) // ignore the TEST registers beyond CC1101_RCCTRL0 (i.e. register 41 onwards)
    {
      reg_check_pass = false;
      Serial.print(F("ERROR: This register does not match expected value of: "));
      Serial.println(currentConfig[i], HEX);
    }

    delay(10);
  }

  if (reg_check_pass) {
    Serial.println(F("PASS: Config reg values are as expected."));
  } else {
    Serial.println(F("*** WARNING: Config reg values NOT as expected. Check these! ***"));
  }

  return reg_check_pass;
  
} // end printCConfigCheck

/**
	Read the ChipCon Status Byte returned as a by-product of SPI communications.
	Not quite sure this is reliable however.
*/
void CC1101::readCCStatus(byte status)
{
  /*
       10.1 Chip Status Byte
        When the header byte, data byte, or command
        strobe is sent on the SPI interface, the chip
        status byte is sent by the CC1101 on the SO pin.
        The status byte contains key status signals,
        useful for the MCU. The first bit, s7, is the
        CHIP_RDYn signal and this signal must go low
        before the first positive edge of SCLK. The
        CHIP_RDYn signal indicates that the crystal is
        running.
  */


  // Data is MSB, so get rid of the fifo bytes, extract the three we care about.
  // https://stackoverflow.com/questions/141525/what-are-bitwise-shift-bit-shift-operators-and-how-do-they-work
  byte state = (status >> 4) & 0b00000111;
  switch (state)
  {
    case 0x00: currentState =  STATE_IDLE; break;
    case 0x01: currentState =  STATE_RX; break;
    case 0x02: currentState =  STATE_TX; break;
    case 0x03: currentState =  STATE_FSTXON; break;
    case 0x04: currentState =  STATE_CALIBRATE; break;
    case 0x05: currentState =  STATE_SETTLING; break;
    case 0x06: currentState =  STATE_RXFIFO_OVERFLOW; break;
    case 0x07: currentState =  STATE_TXFIFO_UNDERFLOW; break;
    default:
      currentState = STATE_UNKNOWN;
  }
  
  // TODO: Do something when we hit OVERFLOW / UNDERFLOW STATE.

  // Get out here if we're not doing detailed logging
  if (!serialDebug) return;

  //Serial.print("Returned Status in binary: ");
  //Serial.println(status, BIN);

  // Refer to page 31 of cc1101.pdf
  // Bit 7    = CHIP_RDY
  // Bit 6:4  = STATE[2:0]
  // Bit 3:0  = FIFO_BYTES_AVAILABLE[3:0]
  if ( !(0b01000000 & status) == 0x00) // is bit 7 0 (low)
  {
    if (serialDebug)
      Serial.println(F("SPI Result: FAIL: CHIP_RDY is LOW! The CC1101 isn't happy. Has a over/underflow occured?"));
  }

  if (serialDebug)
  {
      printCCState();
  
      Serial.print(F("SPI Result: Bytes free in FIFO: "));
      Serial.println( (0b00001111 & status), DEC); // Only the first three bytes matter
  }
}

/**
	Convert the current CC status into English and print to the console.
*/	
void CC1101::printCCState(void)
{

  switch (currentState)
  {
    case (STATE_IDLE):              Serial.print(F("STATE_IDLE")); break;
    case (STATE_RX):                Serial.print(F("STATE_RX")); break;
    case (STATE_TX):                Serial.print(F("STATE_TX")); break;
    case (STATE_FSTXON):            Serial.print(F("STATE_FSTXON")); break;
    case (STATE_CALIBRATE):         Serial.print(F("STATE_CALIBRATE")); break;
    case (STATE_SETTLING):          Serial.print(F("STATE_SETTLING")); break;
    case (STATE_RXFIFO_OVERFLOW):   Serial.print(F("STATE_RXFIFO_OVERFLOW")); break;
    case (STATE_TXFIFO_UNDERFLOW):  Serial.print(F("STATE_TXFIFO_UNDERFLOW")); break;
    default:   Serial.print(F("UNKNOWN STATE")); break;
  }
  Serial.println("");
}

/**
	Print the CC1101'sTX and RX FIFO Status in English to Serial
	
	Useful in library development to check when and how the CC has got into an overflow
	or underflow state - which seems to be hard to recover from.
*/	
void CC1101::printCCFIFOState(void)
{

  byte rxBytes = readStatusRegSafe(CC1101_RXBYTES) & 0b01111111;
  Serial.print(rxBytes, DEC); Serial.println(F(" bytes are in RX FIFO."));

  byte txBytes = readStatusRegSafe(CC1101_TXBYTES) & 0b01111111;
  Serial.print(txBytes, DEC); Serial.println(F(" bytes are in TX FIFO."));

}


/**
	Print the CC1101's MarcState
	
	Useful in library development to check when and how the CC has got into an overflow
	or underflow state - which seems to be hard to recover from.
*/	
void CC1101::printMarcstate(void)
{

      byte marcState =  readStatusRegSafe(CC1101_MARCSTATE) & 0x1F;

      Serial.print(F("Marcstate: "));
      switch (marcState) 
      {
        case 0x00: Serial.println(F("SLEEP SLEEP                        ")); break;
        case 0x01: Serial.println(F("IDLE IDLE                          ")); break;
        case 0x02: Serial.println(F("XOFF XOFF                          ")); break;
        case 0x03: Serial.println(F("VCOON_MC MANCAL                    ")); break;
        case 0x04: Serial.println(F("REGON_MC MANCAL                    ")); break;
        case 0x05: Serial.println(F("MANCAL MANCAL                      ")); break;
        case 0x06: Serial.println(F("VCOON FS_WAKEUP                    ")); break;
        case 0x07: Serial.println(F("REGON FS_WAKEUP                    ")); break;
        case 0x08: Serial.println(F("STARTCAL CALIBRATE                 ")); break;
        case 0x09: Serial.println(F("BWBOOST SETTLING                   ")); break;
        case 0x0A: Serial.println(F("FS_LOCK SETTLING                   ")); break;
        case 0x0B: Serial.println(F("IFADCON SETTLING                   ")); break;
        case 0x0C: Serial.println(F("ENDCAL CALIBRATE                   ")); break;
        case 0x0D: Serial.println(F("RX RX                              ")); break;
        case 0x0E: Serial.println(F("RX_END RX                          ")); break;
        case 0x0F: Serial.println(F("RX_RST RX                          ")); break;
        case 0x10: Serial.println(F("TXRX_SWITCH TXRX_SETTLING          ")); break;
        case 0x11: Serial.println(F("RXFIFO_OVERFLOW RXFIFO_OVERFLOW    ")); break;
        case 0x12: Serial.println(F("FSTXON FSTXON                      ")); break;
        case 0x13: Serial.println(F("TX TX                              ")); break;
        case 0x14: Serial.println(F("TX_END TX                          ")); break;
        case 0x15: Serial.println(F("RXTX_SWITCH RXTX_SETTLING          ")); break;
        case 0x16: Serial.println(F("TXFIFO_UNDERFLOW TXFIFO_UNDERFLOW  ")); break;
      }

}



/**
   writeBurstReg

   Write multiple registers into the CC1101 IC via SPI

   BUG: Doesn't seem to work when writing to configuration registers
        Breaks the CC1101. Might be ESP SPI issue.

   'regAddr'  Register address
   'buffer' Data to be writen
   'len'  Data length
*/
/*
void CC1101::writeBurstReg(byte regAddr, byte* buffer, byte len)
{
  byte addr, i;

  if (serialDebug)
  {
    Serial.println(F("Performing writeBurstReg."));
  }

  addr = regAddr | WRITE_BURST;         // Enable burst transfer
  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  SPI.transfer(addr);                   // Send register address

  for (i = 0 ; i < len ; i++)
    SPI.transfer(buffer[i]);            // Send values

  cc1101_Deselect();                    // Deselect CC1101
}
*/


/**
   readBurstReg

   Read burst data from CC1101 via SPI

   'buffer' Buffer where to copy the result to
   'regAddr'  Register address
   'len'  Data length

   BUG: Not reliable on ESP8266. SPI timing issues.
*/
/*
void CC1101::readBurstReg(byte * buffer, byte regAddr, byte len)
{
  byte addr, i;

  if (serialDebug)
  {
    Serial.println(F("Performing readBurstReg"));
  }

  addr = regAddr | READ_BURST;
  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  SPI.transfer(addr);                   // Send register address
  for (i = 0 ; i < len ; i++)
    buffer[i] = SPI.transfer(0x00);     // Read result byte by byte
  cc1101_Deselect();                    // Deselect CC1101

  if (serialDebug)
    Serial.printf("Read %d bytes.\n", len);      
}
*/
