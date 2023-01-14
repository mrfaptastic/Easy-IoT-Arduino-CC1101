#include "cc1101.h"

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
  memset(cc1101_rx_tx_fifo_tmp_buff, 0x00,  sizeof(cc1101_rx_tx_fifo_tmp_buff)); // flush 
  
  // for longer than one packet messages
  memset(stream_multi_pkt_buffer, 0x00, sizeof(stream_multi_pkt_buffer)); // flush

  // State of CC as told by the CC from the returned status byte from each SPI write transfer
  currentState = STATE_UNKNOWN;

  debug_level = 0;
}

/* Configure the ESP's GPIO etc */
void CC1101::configureGPIO(void)
{
  if (debug_level >= 1)
    Serial.println("Configuring GPIOs.");
	
#ifdef ESP32
  pinMode(SS, OUTPUT);                       // Make sure that the SS Pin is declared as an Output
#endif
  SPI.begin();     
  
#if defined(ESP32) || defined(ESP8266)
  // Initialize SPI interface
  SPI.setFrequency(2000000);  
#endif

  pinMode(CC1101_GDO0_interrupt_pin, INPUT);          // Config GDO0 as input

  if (debug_level >= 1) 
  {
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
bool CC1101::begin(CFREQ freq, uint8_t channr, uint8_t addr)
{
  carrierFreq = freq;
  dataRate    = KBPS_38;
  
 // CC1101_GDO0_interrupt_pin = _int_pin;

  channel       = channr;
  devAddress    = addr; 

  configureGPIO();
  hardReset();                              // Reset CC1101
  delay(100);  
  setCCregs();
  delay(100);
  
  setIdleState();       // Enter IDLE state before flushing RxFifo (don't need to do this when Overflow has occured)
  flushRxFifo();        // Flush Rx FIFO  
  flushTxFifo();     

  // Send empty packet (which won't actually send a packet, but will flush the Rx FIFO only.)
  /*
  CCPACKET packet;
  packet.payload_size = 0;
  sendPacket(packet);  
  */
  //attachGDO0Interrupt();

  if ( checkCC() == false )
  {
    // Close the SPI connection
    SPI.end();

    return false;
  }

  //setRxState();

  return true;

}

/* Begin function when we're given all the 47 configuration registers in one hit */

/*
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
  }

  attachGDO0Interrupt();

  if ( checkCC() == false )
  {
    // Close the SPI connection
    SPI.end();
    
    return false;
  }

  setRxState();

  return true;

}
*/


byte CC1101::getMarcState(void)
{
    byte marcState = (readStatusRegSafe(CC1101_MARCSTATE) & 0b11111); // only care about the lower 5 bits
    return marcState;
}


/* Check the health of the CC. i.e. The partnum response is OK */
bool CC1101::checkCC(void)
{

  // Do a check of the partnum
  uint8_t version = readReg(CC1101_VERSION, CC1101_STATUS_REGISTER);

  if ( version == 0x00 || version == 0xFF) // should return 4 or 20 (in decimal)
  {
    //detachGDO0Interrupt();
    
    setPowerDownState();

    Serial.println(F("Error: CC1101 not detected! (Invalid version)"));
    return false;

  }

  return true;

} // check CC

/* Attach the interrupt from CC1101 when packet received */
void CC1101::attachGDO0Interrupt(void)
{
  if (debug_level >= 1)
    Serial.println(F("Attaching Interrupt to GDO0"));

  attachInterrupt(CC1101_GDO0_interrupt_pin, interupt_packetReceived, FALLING);
}

void CC1101::detachGDO0Interrupt(void)
{
  if (debug_level >= 1)
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
  
  if (debug_level >= 1)
	Serial.println("CC1101 has been woken up.");
  
  //attachGDO0Interrupt(); 
  // Reload config just to be sure?
  hardReset();                              // Reset CC1101
  delay(100);  
  setCCregs();
  setIdleState();       // Enter IDLE state before flushing RxFifo (don't need to do this when Overflow has occured)
  delayMicroseconds(1);
  flushRxFifo();        // Flush Rx FIFO  
  flushTxFifo();     
  //detachGDO0Interrupt();
  
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

  // Print extra info when we're writing to CC register
  if (regAddr <= CC1101_TEST0) // for some reason when this is disable config writes don't work!!
  {
    if (debug_level >= 2)
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
    }
      // Store the configuration state we requested the CC1101 to be
      currentConfig[regAddr] = value;	
  }
  

  cc1101_Select();                      // Select CC1101

  wait_Miso();                          // Wait until MISO goes low
  
  readCCStatus(SPI.transfer(regAddr));  // Send register address
  delayMicroseconds(2);   // HACK
  readCCStatus(SPI.transfer(value));    // Send value
  delayMicroseconds(2);   // HACK

  cc1101_Deselect();                    // Deselect CC1101

}


/**
   cmdStrobe

   Send command strobe to the CC1101 IC via SPI

   'cmd'  Command strobe
*/
void CC1101::cmdStrobe(byte cmd)
{
  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  readCCStatus(SPI.transfer(cmd));                    // Send strobe command
  cc1101_Deselect();                    // Deselect CC1101


  if (debug_level >= 1)
  {
    Serial.print(F("Sent strobe: "));
    switch (cmd)
    {
      case 0x30: Serial.print(F("CC1101_SRES      ")); break;
      case 0x31: Serial.print(F("CC1101_SFSTXON   ")); break;
      case 0x32: Serial.print(F("CC1101_SXOFF     ")); break;
      case 0x33: Serial.print(F("CC1101_SCAL      ")); break;
      case 0x34: Serial.print(F("CC1101_SRX       ")); break;
      case 0x35: Serial.print(F("CC1101_STX       ")); break;
      case 0x36: Serial.print(F("CC1101_SIDLE     ")); break;
      case 0x38: Serial.print(F("CC1101_SWOR      ")); break;
      case 0x39: Serial.print(F("CC1101_SPWD      ")); break;
      case 0x3A: Serial.print(F("CC1101_SFRX      ")); break;
      case 0x3B: Serial.print(F("CC1101_SFTX      ")); break;
      case 0x3C: Serial.print(F("CC1101_SWORRST   ")); break;
      case 0x3D: Serial.print(F("CC1101_SNOP      ")); break;
    }

    Serial.print("State: "); printCCState();    
  }
  



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
  if (debug_level >= 1)
    Serial.println("Resetting CC1101.");

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

  if (debug_level >= 1)
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

  // Set default carrier frequency
  setCarrierFreq(carrierFreq);

  // Common between frequencies and bandwidths (from manual analysis)
  writeReg(CC1101_MCSM2,    CC1101_DEFVAL_MCSM2);
  writeReg(CC1101_MCSM1,    CC1101_DEFVAL_MCSM1);
  writeReg(CC1101_MCSM0,    CC1101_DEFVAL_MCSM0);

  writeReg(CC1101_FREND0,  CC1101_DEFVAL_FREND0);

  writeReg(CC1101_WOREVT1,  CC1101_DEFVAL_WOREVT1);
  writeReg(CC1101_WOREVT0,  CC1101_DEFVAL_WOREVT0);
  writeReg(CC1101_WORCTRL,  CC1101_DEFVAL_WORCTRL);

  writeReg(CC1101_RCCTRL1,  CC1101_DEFVAL_RCCTRL1);
  writeReg(CC1101_RCCTRL0,  CC1101_DEFVAL_RCCTRL0);


  // Data Rate - details extracted from SmartRF Studio
  switch (dataRate)
  {
    case KBPS_250:
    case KBPS_38: 

      writeReg(CC1101_FSCAL1,  0x00);
      writeReg(CC1101_FSCAL2,  0x1F);
      writeReg(CC1101_FSCAL3,  0xEA);

   //   writeReg(CC1101_FSCTRL0, 0x21);
   //   writeReg(CC1101_FSCTRL1, 0x0C); // Frequency Synthesizer Control (optimised for sensitivity)

      writeReg(CC1101_MDMCFG4, 0x2D); // Modem Configuration      
      writeReg(CC1101_MDMCFG3, 0x3B); // Modem Configuration

      // https://e2e.ti.com/support/wireless-connectivity/other-wireless-group/other-wireless/f/other-wireless-technologies-forum/204393/manchester-encoding-necessary-for-reliable-cc1101-reception
      writeReg(CC1101_MDMCFG2,  0x13); // dc offset + gfsk + no manchester encoding. not useful for gfsk

      //writeReg(CC1101_MDMCFG2,  0x1B); // gfsk + enable manchester encoding
      
      // Note: Can't use ASK unless the PAtable is actually populated properlly..
      //writeReg(CC1101_MDMCFG2,  0x0B); // 2FSK + enable manchester encoding      
      //writeReg(CC1101_MDMCFG1,  0x42); // fec disabled, 4 bytes preamble
      writeReg(CC1101_MDMCFG1,  0x62); // fec disabled, 16 bytes preamble
      writeReg(CC1101_MDMCFG0,  0xF8);

      writeReg(CC1101_DEVIATN, 0x62); // Modem Deviation Setting
      writeReg(CC1101_FOCCFG, 0x1D); // Frequency Offset Compensation Configuration
      writeReg(CC1101_BSCFG, 0x1C); // Bit Synchronization Configuration

      writeReg(CC1101_AGCCTRL2, 0xC7); // AGC Control
      writeReg(CC1101_AGCCTRL1, 0x00); // AGC Control
      writeReg(CC1101_AGCCTRL0, 0xB0); // AGC Control

      writeReg(CC1101_FREND1, 0xB6); // Front End RX Configuration
      
      break;


    // Low throughput (1.2kbps) transmission doesn't work.  Can't get it to work.
    // Last 5 bytes of all received payloads are garbage!!
    // RX Buffer Data: 1, 1, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, D3, 88, F1, 14, B,  

  //    case KBPS_38:
/*
 
    writeReg(CC1101_FSCAL0,  0x1F);
    writeReg(CC1101_FSCAL1,  0x00);
    writeReg(CC1101_FSCAL2,  0x2A);
    writeReg(CC1101_FSCAL3,  0xEA);

    
      writeReg(CC1101_FSCTRL0,    0x08);
      writeReg(CC1101_FSCTRL1,    0x00); // Frequency Synthesizer Control (optimised for sensitivity)

      writeReg(CC1101_MDMCFG4,  0x7B); // Modem Configuration      
      writeReg(CC1101_MDMCFG3,  0x83        ); // Modem Configuration
      writeReg(CC1101_MDMCFG2,  0x1B); // no fec, 24 byte preamble
      writeReg(CC1101_MDMCFG1,  0x72); // dc blokcing, gfsk, manchester encoing, 4 byte sync word
      writeReg(CC1101_MDMCFG0,  0xF8);    

      writeReg(CC1101_DEVIATN,  0x42        ); // Modem Deviation Setting
      writeReg(CC1101_FOCCFG,   0x16); // Frequency Offset Compensation Configuration
      writeReg(CC1101_BSCFG,    0x6C        ); // Bit Synchronization Configuration
      
      writeReg(CC1101_AGCCTRL2,  0xC7        ); // AGC Control
      writeReg(CC1101_AGCCTRL1, 0x00); // AGC Control
      writeReg(CC1101_AGCCTRL0, 0xB2); // AGC Control
      
      writeReg(CC1101_FREND0, 0x10); // Front End RX Configuration           
      writeReg(CC1101_FREND1, 0x56); // Front End RX Configuration      
*/

      break;
   
  }

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

      writeReg(CC1101_FREQ2,  CC1101_DEFVAL_FREQ2_922);
      writeReg(CC1101_FREQ1,  CC1101_DEFVAL_FREQ1_922);
      writeReg(CC1101_FREQ0,  CC1101_DEFVAL_FREQ0_922);
      break;
    case CFREQ_433:

      writeReg(CC1101_FREQ2,  CC1101_DEFVAL_FREQ2_433);
      writeReg(CC1101_FREQ1,  CC1101_DEFVAL_FREQ1_433);
      writeReg(CC1101_FREQ0,  CC1101_DEFVAL_FREQ0_433);
      break;
    default:

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
	
  //detachGDO0Interrupt(); // we don't want to get interrupted at this important moment in history
  
  //setIdleState();       // Enter IDLE state before flushing RxFifo (don't need to do this when Overflow has occurred)
  //delayMicroseconds(1);
  //flushRxFifo();        // Flush Rx FIFO

  uint16_t unsent_stream_bytes = stream_length;

  if (stream_length > MAX_STREAM_LENGTH) // from ccpacket.h
  {
    Serial.println(F("Too many bytes to send!"));
    return false;
  }

  if (debug_level >= 1) {
    Serial.print(F("Sending byte stream of ")); Serial.print(stream_length, DEC); Serial.println(F(" bytes in length."));
  }


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
    packet.cc_dest_address  		  = cc_dest_address; // probably going to broadcast this most of the time (0x00, or 0xFF)
    packet.stream_num_of_pkts  		= stream_num_of_pkts;
    packet.stream_pkt_seq_num  		= stream_pkt_seq_num++;

    // make sure we flush the contents of the packet (otherwise we might append crap from the previous packet in the stream 
    // if this packet is smaller (i.e. the last packet). 
    // Not a big issue though as the receiver should only read up until payload_size)
    memset(packet.payload, 0x00, sizeof(packet.payload));  
    
    uint16_t start_pos = stream_length - unsent_stream_bytes; // (stream_length == unsent_stream_bytes) ? 0:(stream_length-1-unsent_stream_bytes); // because array positions are x-1
	
    memcpy( (byte *)packet.payload, &data[start_pos], payload_size); // cast as char
   	
      // REMOVE - FOR VALIDATION TESTING ONLY
/*
     if (debug_level >= 2) {
      Serial.println("");
      
      Serial.print("HEX content of packet to send:");
      for (uint8_t i = 0; i < packet.payload_size; i++)
      {
        //Serial.printf("%02x", packet.payload[i]);
        Serial.print(","); Serial.print((char)packet.payload[i]);
      }	
      Serial.println("");

          Serial.print(F("stream_num_of_pkts: "));
          Serial.println(packet.stream_num_of_pkts, DEC);

          Serial.print(F("stream_pkt_seq_num: "));
          Serial.println(packet.stream_pkt_seq_num, DEC);

          Serial.print(F("Payload_size: "));
          Serial.println(packet.payload_size, DEC);
     }
 */
    // Try and send the packet stream.
    uint8_t tries = 0; 
	
    // Check that the RX state has been entered
    sendStatus = sendPacket(packet);
/*
    while (tries++ < 3 && ((sendStatus = sendPacket(packet)) != true) )
    {
      Serial.println(F("Failed to send byte packet. Retrying. "));
    }
*/
    if ( !sendStatus )
    {
      Serial.println(F("Failed to send byte stream. Existing."));
      break; // Get out of this 
    }

    unsent_stream_bytes -= payload_size;

    if (debug_level >= 1){ 
	    Serial.print(unsent_stream_bytes, DEC);
      Serial.println(F(" bytes remain unsent."));
	  }

    delay(1000); // Need a delay between packets or the reciever won't be ready for the next immediate packet!
    // Perhaps need to implement rolling buffer.

  } // end stream loop

  //attachGDO0Interrupt();
  
  if (debug_level >= 1){
	Serial.print((millis() - start_tm), DEC);
	Serial.println(F(" milliseconds to complete sendBytes()"));  
  }

  return sendStatus;
}

uint8_t CC1101::getRxFIFOBytes()
{
    byte rxBytes = readStatusRegSafe(CC1101_RXBYTES); // Any left over bytes in the TX FIFO?

    return (uint8_t) (rxBytes & BYTES_IN_FIFO);
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
  byte rxBytes, rxUnderflow;    
  bool res = false;

  /* For whatever reason if we are NOT already in IDLE state before trying to send a packet,
     at data rates < 200kbps, the last two bytes message is corrupt at reciever. What the FUCK!??
   */
  setIdleState();       // Enter IDLE state

  if (debug_level >= 1)
    Serial.println(F("sendPacket()"));  

  // Check 1: TX/RX FIFO check
  txBytes = readStatusRegSafe(CC1101_TXBYTES); // Any left over bytes in the TX FIFO?
  rxBytes = readStatusRegSafe(CC1101_RXBYTES); // Any left over bytes in the TX FIFO?

  // Repurpose these variables
  txBytes     = txBytes & BYTES_IN_FIFO;
  txOverflow  = txBytes & OVERFLOW_IN_FIFO;

  // Repurpose these variables
  rxBytes     = rxBytes & BYTES_IN_FIFO;
  rxUnderflow = rxBytes & OVERFLOW_IN_FIFO;  

  if (txOverflow != 0x0)
  {
      if (debug_level >= 1)
        Serial.println(F("TX FIFO is in overflow. Flushing. "));
      flushTxFifo();
  }

  if (rxUnderflow != 0x0)
  {
      if (debug_level >= 1)    
        Serial.println(F("RX FIFO is in overflow. Flushing. "));
      flushRxFifo();
  }  

  if (txBytes != 0) // only do this stuff if it's empty already
  {
      if (debug_level >= 1)    
        Serial.println(F("TX FIFO contains garbage. Flushing. "));

    flushTxFifo();        // Flush Tx FIFO
  }


 /**
  * STEP 0: Build the radio packet of stuff to send
  */

  // Flush the RX/TX Buffer
  memset(cc1101_rx_tx_fifo_tmp_buff, 0x00, sizeof(cc1101_rx_tx_fifo_tmp_buff));  
  
  // Set dest device address as first position of TX FIFO
  //writeReg(CC1101_TXFIFO,  packet.cc_dest_address); // byte 1
  cc1101_rx_tx_fifo_tmp_buff[0] = packet.cc_dest_address;
  
  // Set payload size as the second position of the TX FIFO
  //writeReg(CC1101_TXFIFO,  packet.payload_size); // byte 2
  cc1101_rx_tx_fifo_tmp_buff[1] = packet.payload_size;
  
  // Stream stuff
  //writeReg(CC1101_TXFIFO,  packet.stream_num_of_pkts); // byte 3
  //writeReg(CC1101_TXFIFO,  packet.stream_pkt_seq_num); // byte 4
  cc1101_rx_tx_fifo_tmp_buff[2] = packet.stream_num_of_pkts;
  cc1101_rx_tx_fifo_tmp_buff[3] = packet.stream_pkt_seq_num;
  
  // Copy the payload
  memcpy( &cc1101_rx_tx_fifo_tmp_buff[4], packet.payload, packet.payload_size);

       if (debug_level >= 2)
      {    
        Serial.print("Payload Data: ");
        for (int i = 0; i < CCPACKET_MAX_SIZE; i++)
        {
          Serial.print(cc1101_rx_tx_fifo_tmp_buff[i], HEX);
          Serial.print(", ");
        }
        Serial.println("");
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


  writeBurstReg(CC1101_TXFIFO, cc1101_rx_tx_fifo_tmp_buff, CCPACKET_MAX_SIZE);
  

  // Send the contents of th RX/TX buffer to the CC1101, one byte at a time
  // the receiving CC1101 will append two bytes for the LQI and RSSI
  /*
  setIdleState();
  flushTxFifo();

  // Send crap
  for (int i = 0; i< CCPACKET_MAX_SIZE; i++)
     writeReg(CC1101_TXFIFO,  255);

*/
  /*
	for (uint8_t len = 0 ; len < packet.payload_size; len++)
	  writeReg(CC1101_TXFIFO,  packet.payload[len]);
  // We're using fixed packet size, so need to fill with null until end
  for (uint8_t len = packet.payload_size ; len < STREAM_PKT_MAX_PAYLOAD_SIZE; len++)
    writeReg(CC1101_TXFIFO,  0x00); 
  */

	// I assume the CC1101 sends the two extra CRC bytes here somewhere.
	if (debug_level >= 1)
	{
		Serial.print(F(">> Number of bytes to send: "));
		Serial.println(readStatusReg(CC1101_TXBYTES) & 0x7F, DEC);
	}

	// CCA enabled: will enter TX state only if the channel is clear
 
  while (currentState != STATE_TX)
  {
	  setTxState();
    delay(2);
  }
  

 // setTxState(); 


  // Wait until it has been sent before failing
  while ((readStatusRegSafe(CC1101_TXBYTES) & 0x7F) != 0) { }

  if (debug_level >= 1)
  {
    Serial.print(F("Bytes remaining in TX FIFO (should be zero):"));
    Serial.println((readStatusRegSafe(CC1101_TXBYTES) & 0x7F), DEC);		
  }    

	// Check that the TX FIFO is empty
	if ((readStatusRegSafe(CC1101_TXBYTES) & 0x7F) == 0)
		res = true;


  if (debug_level >= 1)
  {
    if (res == true) {
      Serial.println(F(">> TX SUCCESS."));
    } else
    {
      Serial.println(F(">> TX FAILED."));
    }
  }	

 // setRxState();

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
  bool _streamReceived = false;


  // Only allow this to happen ever 100ms or so.
  if ( !((millis() - cc1101_last_check) > 100) ) { return false; }
  cc1101_last_check = millis();

  //Serial.println("Test");


  // WE COULD BE STILL reciving bytes into the RF interface whilst this number is read, so it might not be accurate!
  byte rxBytes = readStatusRegSafe(CC1101_RXBYTES); // Unread bytes in RX FIFO according to CC1101. TODO: Need to do this safely

  // Repurpose these variables
  rxBytes     = rxBytes & BYTES_IN_FIFO;
  byte rxOverflow  = rxBytes & OVERFLOW_IN_FIFO;
/*
  Serial.print("Bytes: ");
  Serial.println(rxBytes, DEC);

cmdStrobe(CC1101_SNOP);
  printCCState();
  */

  if (rxOverflow != 0x0)
  {
    if (debug_level >= 1)
      Serial.println("RX FIFO Overflow in receivePacket(). Returning 0.");

    setIdleState();
    flushRxFifo();
    setRxState();
    return false;
  }


  if (rxBytes > 0)
  {
    packetReceived = true;
  }

  
  /*
  // Get status, check for overflow
  cmdStrobe(CC1101_SNOP);
  if (currentState == STATE_RXFIFO_OVERFLOW)
  {
        flushRxFifo();
        packetReceived = false;  
        return false;   
  }
  */

  // We got something
  //detachGDO0Interrupt(); // we don't want to get interrupted at this important moment in history
  //packetReceived = false;

  while (packetReceived) // could recieve another packet whilst recieving a packet....
  {
    delay(500);

    if (debug_level >= 1) {
      Serial.println(F("---------- START: RX Interrupt Request  -------------"));
    }    

    packetReceived = false;

    CCPACKET packet;
    if (receivePacket(&packet) > 0)
    {
      receivedRSSI = decodeCCRSSI(packet.rssi);

      if (debug_level >= 1)
      {
        Serial.println(F("Processing packet in dataAvailable()..."));

        if (!packet.crc_ok)
          Serial.println(F("CRC not ok!"));
      
        Serial.print(F("lqi: ")); 	Serial.println(decodeCCLQI(packet.lqi));
        Serial.print(F("rssi: ")); 	Serial.print(decodeCCRSSI(packet.rssi)); Serial.println(F("dBm"));
      }
    
      if (packet.crc_ok && packet.payload_size > 0)
      {
          if (debug_level >= 2)
          {
          
              Serial.print(F("stream_num_of_pkts: "));
              Serial.println(packet.stream_num_of_pkts, DEC);

              Serial.print(F("stream_pkt_seq_num: "));
              Serial.println(packet.stream_pkt_seq_num, DEC);

              Serial.print(F("Payload_size: "));
              Serial.println(packet.payload_size, DEC);
              Serial.print(F("Data: "));
              Serial.println((const char *) packet.payload);
              
          }

        // This data stream was only one packets length (i.e. < 57 bytes or so)
        // therefore we just copy to the buffer and we're all good!
      
        // Note: Packets from rougue devices / multiple transmitting at the same time can easily break this code.
        if (packet.stream_num_of_pkts == 1 && packet.stream_pkt_seq_num == 1 ) // It's a one packet wonder
        {
          // We got the first packet in the stream so flush the buffer...
          memset(stream_multi_pkt_buffer, 0, sizeof(stream_multi_pkt_buffer)); // flush
          memcpy(stream_multi_pkt_buffer, packet.payload, packet.payload_size);
        
          if (debug_level >= 1)
          {
              Serial.println(F("Single packet stream has been received."));
          }

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
          if (debug_level >= 3)
          {
            Serial.print("HEX content of packet:");
            for (int i = 0; i < packet.payload_size; i++)
            {
              Serial.print(packet.payload[i], HEX); Serial.print(" ");
            }
        
            Serial.print(F("Received stream packet "));
			Serial.print((int)packet.stream_pkt_seq_num);
			Serial.print(F(" of "));
			Serial.print((int)packet.stream_num_of_pkts);
			Serial.print(F(". Buffer start position: "));
			Serial.print((int)buff_start_pos);
			Serial.print(F(", end position "));
			Serial.print((int)buff_end_pos);
			Serial.print(F(", payload size: "));
			Serial.println((int)packet.payload_size);
          }

          if ( buff_end_pos > MAX_STREAM_LENGTH)
          {
            if (debug_level >= 1)
              Serial.println(F("Received a stream bigger than allowed. Dropping."));				
          }
          else
          {
            memcpy( &stream_multi_pkt_buffer[buff_start_pos], packet.payload, packet.payload_size); // copy packet payload  
            
            // Are we at the last packet?
            if (packet.stream_num_of_pkts ==  packet.stream_pkt_seq_num)
            {

                if (debug_level >= 1) {
                  Serial.print(F("Multi-packet stream of "));
				  Serial.print(buff_end_pos);
				  Serial.println(F(" bytes has been received in full!"));
                }

              receivedStreamSize = buff_end_pos;
              _streamReceived = true;
            }
            
          } // end buffer size check


        } // end single packet stream check or not.

      } // end packet length & crc check
    
    } // packet isn't just 0's check

      
    if (debug_level >= 1)
      Serial.println(F("---------- END: RX Interrupt Request  -------------"));


    }

/*
  // WE COULD BE STILL reciving bytes into the RF interface whilst this number is read, so it might not be accurate!
  rxBytes = readStatusRegSafe(CC1101_RXBYTES); // Unread bytes in RX FIFO according to CC1101. TODO: Need to do this safely

  // Repurpose these variables
  rxBytes     = rxBytes & BYTES_IN_FIFO;

  Serial.print("Left oVer bytes: ");
  Serial.println(rxBytes, DEC);

*/

  // attachGDO0Interrupt();
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

  start_tm = millis();

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

    /**
      * There is NO guarantee that the full packet size will be in the FIFO buffer if we're using a slow throughput
      * so we must get what we can get, and once we CCPACKET_REC_SIZE, process the packet. 
      */

    // Copy contents of FIFO in the buffer from CC1101 
    memset(cc1101_rx_tx_fifo_tmp_buff, 0x00, sizeof(cc1101_rx_tx_fifo_tmp_buff)); // flush the temporary array.

    while (getRxFIFOBytes() < CCPACKET_REC_SIZE) { }

    readBurstReg(cc1101_rx_tx_fifo_tmp_buff, CC1101_RXFIFO, CCPACKET_REC_SIZE);


    /*
    int     deadlock_watch = 0;
    uint8_t counter = 0;
    uint8_t avail_bytes = 0;
    bool loop = true;
    while (loop)
    {
        avail_bytes = getRxFIFOBytes();
        Serial.println("Got stuff from rx Fifo.");
        for (uint8_t i = 0; i < avail_bytes; i++) {
            cc1101_rx_tx_fifo_tmp_buff[counter] = readConfigReg(CC1101_RXFIFO);        


            if (++counter == CCPACKET_REC_SIZE) 
            { loop = false; break; }

        }

      //  delay(5);
        if (deadlock_watch++ > 100) return 0; // get outta here.
    }
    */

  //  Serial.print("Counter is:"); Serial.println(counter, DEC);

  //  if (counter < CCPACKET_REC_SIZE) return 0;

    if (debug_level >= 2)
    {
      Serial.print("Packet Data: ");
      for (int i = 0; i < CCPACKET_REC_SIZE; i++)
      {
        Serial.print(cc1101_rx_tx_fifo_tmp_buff[i], BIN);
        Serial.print(", ");
      }
      Serial.println("");

      Serial.print("Packet Data (char): ");
      for (int i = 0; i < CCPACKET_REC_SIZE; i++)
      {
        Serial.print((char)cc1101_rx_tx_fifo_tmp_buff[i]);
        Serial.print(", ");
      }
      Serial.println("");
    }

      packet->cc_dest_address      = cc1101_rx_tx_fifo_tmp_buff[0]; // byte 1     
      packet->payload_size         = cc1101_rx_tx_fifo_tmp_buff[1]; // byte 2

      if (debug_level >= 2)
      {

        if (packet->cc_dest_address == BROADCAST_ADDRESS) {
          Serial.println(F("* Received broadcast packet"));
		}

        Serial.print(F("Payload size is: "));
        Serial.println(packet->payload_size, DEC);
      }

      // TESTING ONLY
     // packet->payload_size = 6;

      // The payload size contained within this radio packet is too big?
      if ( packet->payload_size > STREAM_PKT_MAX_PAYLOAD_SIZE ) 
      {
        packet->payload_size = 0;   // Discard packet

        if (debug_level >= 1)
          Serial.println(F("Error: Receieved packet too big or payload length non sensical!"));
      }
      else
      {

        packet->stream_num_of_pkts      = cc1101_rx_tx_fifo_tmp_buff[2]; // byte 3
        packet->stream_pkt_seq_num      = cc1101_rx_tx_fifo_tmp_buff[3]; // byte 4


        // Want to reduce the 'length' field for high-order functions like dataAvailable(), otherwise we end up reading the RSSI & LQI as characters!
        memcpy( packet->payload, &cc1101_rx_tx_fifo_tmp_buff[4], packet->payload_size);
        //readBurstReg(packet->payload, CC1101_RXFIFO, rxBytes-4);

      if (debug_level >= 2)
      {     
          Serial.print("RX Buffer Data: ");
          for (int i = 0; i < CCPACKET_REC_SIZE; i++) // include lqi and rssi bytes
          {
            Serial.print(cc1101_rx_tx_fifo_tmp_buff[i], HEX);
            Serial.print(", ");
          }
          Serial.println("");
      }

      
      }

       // Read RSSI
        packet->rssi = cc1101_rx_tx_fifo_tmp_buff[CCPACKET_REC_SIZE-2];

        // Read LQI and CRC_OK
        val = cc1101_rx_tx_fifo_tmp_buff[CCPACKET_REC_SIZE-1]; // 62 in array speak (which is 63 in real life)
        

        // Read RSSI
        // packet->rssi = readConfigReg(CC1101_RXFIFO); //byte 5 + n + 1

        // Read LQI and CRC_OK
        // val = readConfigReg(CC1101_RXFIFO); // byte 5 + n + 2

        packet->lqi = val & 0x7F;
        packet->crc_ok = bitRead(val, 7);      


      /* 
      *  STEP 3: We're done, so flush the RxFIFO.
      */
      // Note: do NOT do this as it will cause a calibration, and if the other side sends a packet quickly thereafter, it will be missed.
      /* 
      State: STATE_RX
      State: STATE_IDLE
      State: STATE_IDLE
      State: STATE_CALIBRATE
      State: STATE_CALIBRATE
      State: STATE_CALIBRATE
      State: STATE_CALIBRATE
      State: STATE_RX
      State: STATE_RX
      */
 
 /*
      // Read what's left, this should be zero?
      rxBytes = readStatusRegSafe(CC1101_RXBYTES); // Unread bytes in RX FIFO according to CC1101. TODO: Need to do this safely
      
      // Repurpose these variables
      rxBytes     = rxBytes & BYTES_IN_FIFO;
      rxOverflow  = rxBytes & OVERFLOW_IN_FIFO;

      if ( rxBytes != 0 )
      {
        Serial.print(F("Warning: Bytes left over in RX FIFO: "));
        Serial.println(rxBytes, DEC);  

        for (int i = 0; i<rxBytes; i++)
        {
           readConfigReg(CC1101_RXFIFO); // read out the garbage.
        }
      }
      
   */ 
    // Back to RX state
    //setRxState();

    if (debug_level >= 1) {
      Serial.print(F("Took "));
	  Serial.print(millis() - start_tm);
	  Serial.println(F(" milliseconds to complete recievePacket()"));
	}   

    return packet->payload_size;
}

/**
   setRxState

   Enter Rx state
*/
void CC1101::setRxState(void)
{
    cmdStrobe(CC1101_SRX);
  
    // Enter back into RX state
    while (currentState != STATE_RX) { 
      cmdStrobe(CC1101_SNOP); 
      setRxState();
      delay(5);
    }
	  


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
    else pa_table_index = 0x07;
	
	// now pick the right PA table values array to 
	switch (carrierFreq)
	{
		case CFREQ_922:

		  pa_value = patable_power_9XX[pa_table_index];
		  break;
		  
		case CFREQ_433:


		  pa_value = patable_power_433[pa_table_index];
		  break;
		  
		default:


		  pa_value = patable_power_868[pa_table_index];
		  break;
		  
	  }
	  

  if (debug_level == 1) { 
	  Serial.print(F("Setting PATABLE0 value to: "));
	  Serial.println(pa_value, HEX);
  }
	  
	// Now write the value
	for (uint8_t i = 0; i < 8; i++)
		writeReg(CC1101_PATABLE,  pa_value); // Set all the PA tables to the same value


} // end of setOutputPowerLevel
	
void CC1101::Split_MDMCFG1(void) {

  int calc = readReg(CC1101_MDMCFG1, CC1101_CONFIG_REGISTER);
  m1FEC = 0;
  m1PRE = 0;
  m1CHSP = 0;
  int s2 = 0;
  for (bool i = 0; i == 0;) {
    if (calc >= 128) {
      calc -= 128;
      m1FEC += 128;
    } else if (calc >= 16) {
      calc -= 16;
      m1PRE += 16;
    } else {
      m1CHSP = calc;
      i = 1;
    }
  }
}

/****************************************************************
 *FUNCTION NAME:Split MDMCFG4
 *FUNCTION     :none
 *INPUT        :none
 *OUTPUT       :none
 ****************************************************************/
void CC1101::Split_MDMCFG4(void) {
  int calc = readReg(CC1101_MDMCFG4, CC1101_CONFIG_REGISTER);
  m4RxBw = 0;
  m4DaRa = 0;
  for (bool i = 0; i == 0;) {
    if (calc >= 64) {
      calc -= 64;
      m4RxBw += 64;
    } else if (calc >= 16) {
      calc -= 16;
      m4RxBw += 16;
    } else {
      m4DaRa = calc;
      i = 1;
    }
  }
}

/****************************************************************
 *FUNCTION NAME:Set Channel spacing
 *FUNCTION     :none
 *INPUT        :none
 *OUTPUT       :none
 ****************************************************************/
void CC1101::setChsp(float f) {

  if (getMarcState() != MARCSTATE_IDLE) {
    Serial.println("Error: Can't change channel spacing when not IDLE");
    return;
  }

  Split_MDMCFG1();
  byte MDMCFG0 = 0;
  m1CHSP = 0;
  if (f > 405.456543) {
    f = 405.456543;
  }
  if (f < 25.390625) {
    f = 25.390625;
  }
  for (int i = 0; i < 5; i++) {
    if (f <= 50.682068) {
      f -= 25.390625;
      f /= 0.0991825;
      MDMCFG0 = f;
      float s1 = (f - MDMCFG0) * 10;
      if (s1 >= 5) {
        MDMCFG0++;
      }
      i = 5;
    } else {
      m1CHSP++;
      f /= 2;
    }
  }
  writeReg(CC1101_MDMCFG1, m1CHSP + m1FEC + m1PRE);
  writeReg(CC1101_MDMCFG0, MDMCFG0);
}
/****************************************************************
 *FUNCTION NAME:Set Receive bandwidth
 *FUNCTION     :none
 *INPUT        :none
 *OUTPUT       :none
 ****************************************************************/
void CC1101::setRxBW(float f) {

  if (getMarcState() != MARCSTATE_IDLE)
  {
    Serial.println("Error: Can't change bandwidth when not IDLE");
    return;
  }

  Split_MDMCFG4();
  int s1 = 3;
  int s2 = 3;
  for (int i = 0; i < 3; i++) {
    if (f > 101.5625) {
      f /= 2;
      s1--;
    } else {
      i = 3;
    }
  }
  for (int i = 0; i < 3; i++) {
    if (f > 58.1) {
      f /= 1.25;
      s2--;
    } else {
      i = 3;
    }
  }
  s1 *= 64;
  s2 *= 16;
  m4RxBw = s1 + s2;
  writeReg(CC1101_MDMCFG4, m4RxBw + m4DaRa);
}
/****************************************************************
 *FUNCTION NAME:Set Data Rate
 *FUNCTION     :none
 *INPUT        :none
 *OUTPUT       :none
 ****************************************************************/
void CC1101::setDRate(float d) {

  if (getMarcState() != MARCSTATE_IDLE)
  {
    Serial.println("Error: Can't change data rate when not IDLE");
    return;
  }

  Split_MDMCFG4();
  float c = d;
  byte MDMCFG3 = 0;
  if (c > 1621.83) {
    c = 1621.83;
  }
  if (c < 0.0247955) {
    c = 0.0247955;
  }
  m4DaRa = 0;
  for (int i = 0; i < 20; i++) {
    if (c <= 0.0494942) {
      c = c - 0.0247955;
      c = c / 0.00009685;
      MDMCFG3 = c;
      float s1 = (c - MDMCFG3) * 10;
      if (s1 >= 5) {
        MDMCFG3++;
      }
      i = 20;
    } else {
      m4DaRa++;
      c = c / 2;
    }
  }
  writeReg(CC1101_MDMCFG4, m4RxBw + m4DaRa);
  writeReg(CC1101_MDMCFG3, MDMCFG3);
}
/****************************************************************
 *FUNCTION NAME:Set Devitation
 *FUNCTION     :none
 *INPUT        :none
 *OUTPUT       :none
 ****************************************************************/
void CC1101::setDeviation(float d) {

  if (getMarcState() != MARCSTATE_IDLE)
  {
    Serial.println("Error: Can't change deviation  when not IDLE");
    return;
  }

  float f = 1.586914;
  float v = 0.19836425;
  int c = 0;
  if (d > 380.859375) {
    d = 380.859375;
  }
  if (d < 1.586914) {
    d = 1.586914;
  }
  for (int i = 0; i < 255; i++) {
    f += v;
    if (c == 7) {
      v *= 2;
      c = -1;
      i += 8;
    }
    if (f >= d) {
      c = i;
      i = 255;
    }
    c++;
  }
  writeReg(CC1101_DEVIATN, c);
}

/* Set the modem state to always be in RX */
void CC1101::setRxAlways() {
  writeReg(CC1101_MCSM1, CC1101_DEFVAL_MCSM1_RXALWAYS);
  //setRxState();
}
	
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
  
  // Refer to page 31 of cc1101.pdf
  // Bit 7    = CHIP_RDY
  // Bit 6:4  = STATE[2:0]
  // Bit 3:0  = FIFO_BYTES_AVAILABLE[3:0]
  if ( !(0b01000000 & status) == 0x00) // is bit 7 0 (low)
  {
    if (debug_level >= 1)
    {
      Serial.print(F("SPI Result: FAIL: CHIP_RDY is LOW! The CC1101 is in state: "));
      printCCState();
      printCCFIFOState();
    }
  }

  if (debug_level >= 2)
  {
      printCCState();
  
      //Serial.print(F("SPI Result: Bytes free in FIFO: "));
      //Serial.println( (0b00001111 & status), DEC); // Only the first three bytes matter
  }
}




void CC1101::setDebugLevel(uint8_t level)
{
    debug_level = level;        //set debug level of CC1101 outputs
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

void CC1101::writeBurstReg(byte regAddr, byte* buffer, byte len)
{
  byte addr, i;
  if (debug_level >= 2)
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


/**
   readBurstReg
   Read burst data from CC1101 via SPI
   'buffer' Buffer where to copy the result to
   'regAddr'  Register address
   'len'  Data length
   BUG: Not reliable on ESP8266. SPI timing issues.
*/

void CC1101::readBurstReg(byte * buffer, byte regAddr, byte len)
{
  byte addr, i;
  if (debug_level >= 2) {
    Serial.println(F("Performing readBurstReg"));
  }
  addr = regAddr | READ_BURST;
  cc1101_Select();                      // Select CC1101
  wait_Miso();                          // Wait until MISO goes low
  SPI.transfer(addr);                   // Send register address
  for (i = 0 ; i < len ; i++)
    buffer[i] = SPI.transfer(0x00);     // Read result byte by byte
  cc1101_Deselect();                    // Deselect CC1101

  if (debug_level >= 1) {
    Serial.print(F("Read "));
	Serial.print(len);
	Serial.println(F(" bytes."));
  }
}








/******************************* Debug / Printing Stufff *********************************/

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
  }
  
} // end printPATable

/**
	Print the Configuration Values and Check Them
*/
bool CC1101::printCConfigCheck(void)
{
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
    Serial.print (reg_value, HEX);

    if (( currentConfig[i] != reg_value) &&  (i <= CC1101_RCCTRL0) ) // ignore the TEST registers beyond CC1101_RCCTRL0 (i.e. register 41 onwards)
    {
      reg_check_pass = false;
      Serial.print(F(" ERROR: Register does not match expected value: "));
      Serial.print(currentConfig[i], HEX);
    }
    Serial.println("");

  }

  if (reg_check_pass) {
    Serial.println(F("PASS: Config reg values are as expected."));
  } else {
    Serial.println(F("*** WARNING: Config reg values NOT as expected. Check these! ***"));
  }

  return reg_check_pass;
  
} // end printCConfigCheck


/**
	Convert the current CC status into English and print to the console.
*/	
void CC1101::printCCState(void)
{

  switch (currentState)
  {
    case (STATE_IDLE):              Serial.println(F("STATE_IDLE")); break;
    case (STATE_RX):                Serial.println(F("STATE_RX")); break;
    case (STATE_TX):                Serial.println(F("STATE_TX")); break;
    case (STATE_FSTXON):            Serial.println(F("STATE_FSTXON")); break;
    case (STATE_CALIBRATE):         Serial.println(F("STATE_CALIBRATE")); break;
    case (STATE_SETTLING):          Serial.println(F("STATE_SETTLING")); break;
    case (STATE_RXFIFO_OVERFLOW):   Serial.println(F("STATE_RXFIFO_OVERFLOW")); break;
    case (STATE_TXFIFO_UNDERFLOW):  Serial.println(F("STATE_TXFIFO_UNDERFLOW")); break;
    default:   Serial.println(F("UNKNOWN STATE")); break;
  }

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
      byte marcState =  getMarcState();

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
