#include <Arduino.h>
#include "cc1101.h"
#include <U8g2lib.h>          // Library to control the 128x64 Pixel OLED display with SH1106 chip  https://github.com/olikraus/u8g2
#include "AES.h"

// Send an encrypted packet with 'Testing 123...' as the text every two seconds. 
//#define TESTING_MODE 1

/* AES Encryption Library Stuff */
AES aes ;
static uint8_t bits = 128;
byte iv [N_BLOCK] ;
byte *key = (unsigned char*)"0123456789010123";
unsigned long long int my_iv = 36753562;

/* OLED Display Config */
// Item: https://www.aliexpress.com/item/32767499263.html?spm=a2g0s.9042311.0.0.27424c4dBKzhaz
// "White Blue color 128X64 OLED LCD LED Display Module For Arduino 0.96 I2C IIC Serial new original with Case"
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ D1, /* data=*/ D3, /* reset=*/ U8X8_PIN_NONE); // MUST USE D3 for Data

/* CC1101 Address & Trasmission Config */
const int THIS_DEVICE_ADDRESS      =  64; // flip these around when compiling for different devices
const int RECEIVING_DEVICE_ADDRESS =  0x00; // 0x00 = Broadcast

/* Create CC1101 Class Instance */
CC1101 radio;

/* Input output variable */
uint8_t availableBytes = 0;
char    plainText[200];


uint16_t receivedStreamSize = 0;
byte   *recieveBuffer;
byte    cipherText[200]; 
char    message_number[16] = {0};
String  rx_payload;

unsigned long sendDelay = 2000;
unsigned long lastSend = 0;

uint8_t tx_count = 0;

// Screen Graphics
void u8g2_prepare(void) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setFontRefHeightExtendedText();
    u8g2.setDrawColor(1);
    u8g2.setFontPosTop();
    u8g2.setFontDirection(0);
}


void setup()
{
    // Allocate memory
    rx_payload.reserve(200);
    
    // Start Serial
    delay (500);
    Serial.begin(115200);
    delay (100);
    Serial.println("Starting...");
    
    // Start SScreen
    u8g2.begin();
    
    // Start RADIO
    if ( !radio.begin(CFREQ_868, KBPS_250, /* channel num */ 16, /* address */ THIS_DEVICE_ADDRESS) ) // Channel 16. This device has an address of 64
    {
      // Pause here
      while (1)
      {
        Serial.println(F("Something is wrong with the CC1101 setup."));
        delay(2000);
      }
    }
    
    radio.setOutputPowerLeveldBm(10); // maximum output power
    radio.printPATable();
    
    //radio.setDevAddress(0x40); // '@' in ascii
    radio.printCConfigCheck();
    
    Serial.println(F("CC1101 radio initialized."));
    delay(1000);
      
}

int tx_counter = 0;
void loop()
{
  unsigned long now = millis();

  if ( radio.dataAvailable() )
  {
    unsigned long ms = micros ();    
  
    recieveBuffer       = radio.getBytes();
    receivedStreamSize  = radio.getSize();

    Serial.print("We received a byte stream and it was "); Serial.print(receivedStreamSize, DEC); Serial.println(" bytes in size."); 
    //rx_payload  = String(radio.receiveBytes()); // pointer to memory location of start of string

    Serial.printf("\nRECEIVED CIPHER:");
    aes.printArray((byte *)recieveBuffer, receivedStreamSize);
    
    aes.set_IV(my_iv);
    aes.get_IV(iv);

    byte decrypted [receivedStreamSize] ;
    aes.do_aes_decrypt((byte *)recieveBuffer,receivedStreamSize,decrypted,key,bits,iv);
    Serial.print("Decryption from radio message took: ");
    Serial.println(micros() - ms);    

/*
    Serial.print("Recieved Message: ");
    Serial.println(rx_payload);
*/
    u8g2.clearBuffer();
    u8g2_prepare();

    memset(message_number, 0x00, sizeof(message_number)); // flush  
    sprintf(message_number, "Message #%d recv'd!", ++tx_count);

    uint8_t x_frame_start = 12;
    u8g2.setDrawColor(1);    
    u8g2.drawBox(0,0, 128, x_frame_start);
    u8g2.setDrawColor(0);        
    u8g2.drawStr(10, 1, message_number);
    u8g2.setDrawColor(1);   
    u8g2.drawFrame(0,x_frame_start, u8g2.getDisplayWidth(), u8g2.getDisplayHeight()-x_frame_start);

    rx_payload = String( (char *)decrypted);

    Serial.println("Message Received:");
    Serial.println(rx_payload);
    
    x_frame_start += 2;
    u8g2.drawStr(2, x_frame_start, rx_payload.substring(0, 21).c_str());
    u8g2.drawStr(3, x_frame_start+(8*1), rx_payload.substring(21, 41).c_str());
    u8g2.drawStr(3, x_frame_start+(8*2), rx_payload.substring(41, 61).c_str());
    u8g2.drawStr(3, x_frame_start+(8*3), rx_payload.substring(61, 81).c_str());
    u8g2.drawStr(3, x_frame_start+(8*4), rx_payload.substring(81, 101).c_str());    
      
    u8g2.sendBuffer();
    
  }


  // Got a message to send?
  availableBytes = Serial.available();
  if (availableBytes > 0) // we have some serial string.
  {
      u8g2.clearBuffer();
      u8g2_prepare();
      u8g2.drawStr(16, 0, "Sending message");
      u8g2.sendBuffer();
          
      memset(plainText, 0x00, sizeof(plainText)); // flush  
      
      for(int i=0; i<availableBytes && (i < sizeof(plainText)-1); i++) 
      {
         plainText[i] = Serial.read();
         plainText[i+1] = '\0'; // Append a null
         // potential overflow here if availableBytes > rx_payload...
      } 

      Serial.flush();    


      /* Now do the encryption and send */
      int plainLength = strlen(plainText);  // don't count the trailing /0 of the string !
      Serial.printf("Plaintext is %d chars in length.\n", plainLength);
      int paddedLength = plainLength + N_BLOCK - plainLength % N_BLOCK;      
      Serial.printf("Padded ciphertext is %d bytes in length.\n", paddedLength);      
      aes.iv_inc();
      byte plain_p[paddedLength];
      byte cipherText [paddedLength] ;
      byte decrypted [paddedLength] ;
      unsigned long ms = micros ();
      aes.set_IV(my_iv);
      aes.get_IV(iv);
      aes.do_aes_encrypt( (byte *) plainText,plainLength,cipherText,key,bits,iv);
      Serial.print("Encryption took: ");
      Serial.println(micros() - ms);
      ms = micros ();
      aes.set_IV(my_iv);
      aes.get_IV(iv);
      aes.do_aes_decrypt(cipherText,paddedLength,decrypted,key,bits,iv);
      Serial.print("Decryption took: ");
      Serial.println(micros() - ms);
      Serial.printf("\n\nPLAIN :");
      aes.printArray((byte *)plainText,(bool)true);
      Serial.printf("\nCIPHER:");
      aes.printArray(cipherText,(bool)false);
      Serial.printf("\nCHECK :");
      aes.printArray(decrypted,(bool)true);
      Serial.printf("\nIV    :");
      aes.printArray(iv,16);
      Serial.printf("\n============================================================\n");

    //  Serial.print("Sending message: ");
     // Serial.println(tx_payload);
      radio.sendBytes((byte * )cipherText, paddedLength, RECEIVING_DEVICE_ADDRESS); // Send the above Arduino String class as a c char array


      u8g2.clearBuffer();
      u8g2_prepare();
      u8g2.drawStr(16, 32, "Message sent!");
      u8g2.sendBuffer();    

      
      
  
  }

#ifdef TESTING_MODE
  if (now > lastSend)
  {
    /*
    char tx_payload[200];
    memset(tx_payload, 0x00, sizeof(tx_payload)); // flush
    sprintf(tx_payload, "Message #%d from device %d. Hello %d, I hope this message gets to you well.", tx_counter++, THIS_DEVICE_ADDRESS, RECEIVING_DEVICE_ADDRESS);

    Serial.print("Sending message: ");
    Serial.println(tx_payload);
    radio.sendChars(tx_payload, RECEIVING_DEVICE_ADDRESS); // Send the above Arduino String class as a c char array

    lastSend = now + sendDelay;

    u8g2.clearBuffer();
    u8g2_prepare();
    u8g2.drawStr(16, 0, "Sending message");
    u8g2.sendBuffer();
    */

      memset(plainText, 0x00, 200); // flush  
      
      strcpy(plainText, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz012");

      /* Now do the encryption and send */
      int plainLength = strlen(plainText);  // don't count the trailing /0 of the string !
      Serial.printf("Plaintext is %d chars in length.\n", plainLength);
      int paddedLength = plainLength + N_BLOCK - plainLength % N_BLOCK;      
      Serial.printf("Padded ciphertext is %d bytes in length.\n", paddedLength);      
      aes.iv_inc();
      byte plain_p[paddedLength];
      byte cipherText [paddedLength] ;
      byte decrypted [paddedLength] ;
      unsigned long ms = micros ();
      aes.set_IV(my_iv);
      aes.get_IV(iv);
      aes.do_aes_encrypt( (byte *) plainText,plainLength,cipherText,key,bits,iv);
      Serial.print("Encryption took: ");
      Serial.println(micros() - ms);

      //radio.sendChars(plainText, RECEIVING_DEVICE_ADDRESS); // Send the above Arduino String class as a c char array
      
      radio.sendBytes((byte * )cipherText, paddedLength, RECEIVING_DEVICE_ADDRESS); // Send the above Arduino String class as a c char array

      Serial.printf("PLAIN :");
      aes.printArray((byte *)plainText,(bool)true);
      Serial.printf("CIPHER:");
      aes.printArray(cipherText, paddedLength);    

      aes.set_IV(my_iv);
      aes.get_IV(iv);
      aes.do_aes_decrypt(cipherText,paddedLength,decrypted,key,bits,iv);
      Serial.printf("DECRYPTED :");
      aes.printArray(decrypted,(bool)true);        

      u8g2.clearBuffer();
      u8g2_prepare();
      u8g2.drawStr(0, 32, "Test Message sent!");
      u8g2.sendBuffer();    

       lastSend = now + sendDelay;
  }

#endif
  

} // end loop
