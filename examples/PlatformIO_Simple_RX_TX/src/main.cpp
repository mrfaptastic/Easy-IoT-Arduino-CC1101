/*************************************************************************
 * Simple Send and Recieve example.
 * 
 * Requires 2 x CC1101's. 
 * 
 * Tested with an ESP32 (sender) and ESP32-S2 Saola (reciever)
 * 
**************************************************************************/

#include "cc1101.h"

//#define ENABLE_U8X8_OLED_DISPLAY 1

#if defined (CONFIG_IDF_TARGET_ESP32S2)

  //Sample code to control the single NeoPixel on the ESP32-S2 Saola
  #include <Adafruit_NeoPixel.h>

  // On the ESP32S2 SAOLA GPIO is the NeoPixel.
  #define PIN        18 

  //Single NeoPixel
  Adafruit_NeoPixel pixels(1, PIN, NEO_GRB + NEO_KHZ800);

#endif


#if defined(ENABLE_U8X8_OLED_DISPLAY)
  
  #include <u8x8lib.h>          // Library to control the 128x64 Pixel OLED display with SH1106 chip  https://github.com/olikraus/u8x8

  // "White Blue color 128X64 OLED LCD LED Display Module For Arduino 0.96 I2C IIC Serial new original with Case"

  //u8x8_SSD1306_128X64_NONAME_F_SW_I2C u8x8(u8x8_R0, /* clock=*/ 9, /* data=*/ 8, /* reset=*/ U8X8_PIN_NONE); // MUST USE D3 for Data    
  U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* clock=*/ 9, /* data=*/ 8, /* reset=*/ U8X8_PIN_NONE); // MUST USE D3 for Data        

#endif




/*******************************************************************
 * Radio, Device ID and Interrupt Pin configuration                */


//#define RADIO_CHANNEL         16
//#define DEVICE_ADDRESS        22
//#define DEST_ADDRESS          0 

/* In your projects platformio.ini file configure the following, but only change the DEVICE_ADDRESS for each device:
build_flags = 
    -DRADIO_CHANNEL=16
	-DDEVICE_ADDRESS=20
	-DDEST_ADDRESS=0 ; THis is the broadcast channel

*/

CC1101 radio;

unsigned long lastSend = 0;
unsigned long lastRecv = 0;
unsigned int sendDelay = 3000;

unsigned int counter = 0;
String  send_payload;
String  recieve_payload;

void setup() 
{
    Serial.begin(115200);
    delay (100);

    #if defined (ENABLE_U8X8_OLED_DISPLAY)
      u8x8.begin();
    #endif


    #if defined (CONFIG_IDF_TARGET_ESP32S2)

      //This pixel is just way to bright, lower it to 10 so it does not hurt to look at.
      pixels.setBrightness(10);
      pixels.begin(); // INITIALIZE NeoPixel (REQUIRED)
      pixels.setPixelColor(0,Adafruit_NeoPixel::Color(0,0,255));
      pixels.show();
    #else
      pinMode(LED_BUILTIN, OUTPUT);    
    #endif


    // Have a nice fastled led for signal strength
    // FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);	
    // returnColor = Adafruit_NeoPixel::Color::BlueViolet; 
    // FastLED.show(); 

    delay(30); 

    Serial.println("Starting...");
    radio.setDebugLevel(5); // uncomment to see detailed cc1101 SPI and library debug info

    // Start RADIO
    while (!radio.begin(CFREQ_433, RADIO_CHANNEL, DEVICE_ADDRESS));   // channel 16! Whitening enabled 

    radio.setOutputPowerLeveldBm(10); // max power
    radio.setDeviation(5.157471); // ok
    radio.setDRate(1); //ok
    radio.setRxBW(58.035714);
    
   
#if defined(RECIEVE_ONLY)    
    radio.setRxAlways();
    radio.setRxState();        
#endif     
    delay(1000); // Try again in 5 seconds
    //radio.printCConfigCheck();     

    Serial.println(F("CC1101 radio initialized."));
    recieve_payload.reserve(512);
    
    
#if !defined(RECIEVE_ONLY)    
    Serial.printf("Sending a message every %d ms.\n", sendDelay);
#endif 

  //  radio.printPATable();

}

//void loop() { }
void loop() 
{
    unsigned long now = millis();

    #if defined (CONFIG_IDF_TARGET_ESP32S2)  
      if ((now - lastRecv) > 5000) 
      {
            pixels.setPixelColor(0, 0);
            pixels.show();  
      }
    #endif
    
    if ( radio.dataAvailable() ) // Got something!
    {       
        Serial.println("Data available.");    

        recieve_payload  = String(radio.getChars()); // pointer to memory location of start of string

        Serial.print("Payload received: ");
        Serial.println(recieve_payload);


        Serial.print("Rssi: "); Serial.print(radio.getLastRSSI()); Serial.println("dBm");     
                        

     #if defined (CONFIG_IDF_TARGET_ESP32S2)       

        uint8_t rssi = abs(radio.getLastRSSI());
        uint32_t returnColor = 0;
        if (rssi < 45)
          returnColor = Adafruit_NeoPixel::Color(255,255,255); // white
        else if (rssi < 50)
          returnColor = Adafruit_NeoPixel::Color(0,255,0); // bright green
        else if (rssi < 60)
          returnColor = Adafruit_NeoPixel::Color(0,128,0);          
        else if (rssi < 70)
          returnColor = Adafruit_NeoPixel::Color(0,96,0);
        else if (rssi < 80)
          returnColor = Adafruit_NeoPixel::Color(128,128,0); // amber
        else if (rssi < 90)
          returnColor = Adafruit_NeoPixel::Color(100,0,0); // faint red
        else if (rssi < 120)
          returnColor = Adafruit_NeoPixel::Color(200,0,0); // red

        //Set the new color on the pixel.
        pixels.setPixelColor(0, returnColor);

        // Send the updated pixel colors to the hardware.
        pixels.show();            
        //Serial.println(radio.getLastRSSI());

      #else
        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)   
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW             
      #endif


      #if defined(ENABLE_U8X8_OLED_DISPLAY)

      String linedata1 = recieve_payload.substring(0,14);
      String linedata2 = recieve_payload.substring(14,28);
      String rssiStr = String(radio.getLastRSSI());

        u8x8.clearDisplay();

        // https://github.com/olikraus/u8g2/wiki/u8x8reference#drawstring
        u8x8.setFont(u8x8_font_7x14_1x2_f);
        u8x8.drawString(0, 0, linedata1.c_str());
        u8x8.drawString(0, 2, linedata2.c_str());        
         
        u8x8.setFont(u8x8_font_courB18_2x3_n);
        //u8x8.drawString(0, 5, rssiStr.c_str());        
        u8x8.drawString(0, 5, rssiStr.c_str());       
      #endif

       lastRecv = now;
    }

    // Periodically send something random.
    if (now > lastSend) 
    {
       // radio.setRxAlways();
        // radio.printMarcstate();


#if !defined(RECIEVE_ONLY)

        Serial.println("Sending message.");        
        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  
        //send_payload = "0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789";
        // send_payload = "0123456789------------------------------------------012345";

        //send_payload = "Messages " + String (counter) + " from device " + String(DEVICE_ADDRESS) + ".";
        send_payload = "Test message 12345678900";
        radio.sendChars(send_payload.c_str(), DEST_ADDRESS);     

        Serial.print("Payload sent: ");
        Serial.println(send_payload); 
  
        counter++;

        // IMPORTANT: Kick the radio into receive mode, otherwise it will sit IDLE and be TX only.
        radio.setRxState();        

        delay(100);
        digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW        

#endif     

        lastSend = now + sendDelay;
    }



}