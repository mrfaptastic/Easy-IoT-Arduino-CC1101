#include <Arduino.h>
//#include "cc1101_custom_config.h"
#include "cc1101.h"

#include <U8g2lib.h>          // Library to control the 128x64 Pixel OLED display with SH1106 chip  https://github.com/olikraus/u8g2  


// LED STUFF
#include <FastLED.h>

#define LED_PIN     D1

// Information about the LED strip itself
#define NUM_LEDS    26
#define CHIPSET     WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

#define BRIGHTNESS  128


// A color palette for the boot-up sequence.
DEFINE_GRADIENT_PALETTE( myCustom2z ) 
{
      0,   0,   0,   255,   // Black  
      80,   0,   255,   0,   // Black  
      150,   255,   128,  0,   // Black  
      200,  128,   0,   0,   // Black  
      255,  255,   0,   0   // Black  
};// and back to Red



/** 
 *  NOTE: On ESP8266 can't use D0 for interrupts on WeMos D1 mini.
 *        Using D0 with the 
 *        
 *  "All of the IO pins have interrupt/pwm/I2C/one-wire support except D0"
 * 
 * CC1101 GDO2 is connected to D2 on the ESP8266
 * CC1101 GDO0 isn't connnected to anything.
 * Both GDO2 and GDO0 are configured in the CC1101 to behave the same.
 *
 */

// Create Class Instanced
CC1101 radio;


// 128x64 OLED:     https://www.aliexpress.com/item/1PCS-White-color-0-96-inch-128X64-OLED-Display-Module-For-arduino-0-96-IIC-SPI/32767499263.html
// https://github.com/olikraus/u8g2/wiki/u8g2setupcpp
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ D3, /* data=*/ D1);   


unsigned long lastSend = 0;
unsigned int sendDelay = 10000;

unsigned long lastStatusDump = 0;
unsigned int statusDelay = 5000;

String rec_payload;

void setup() 
{
    // Start Serial
    delay (2000);
    Serial.begin(115200);
    delay (100);
    Serial.println("Starting...");

    // LEDs
    FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalSMD5050 );
    FastLED.setBrightness( BRIGHTNESS );  

    fill_solid(leds, NUM_LEDS, CRGB(0,0,0));

    FastLED.show();
    FastLED.delay(8);      

    
    // Start OLED
    // Setup and start OLED display
   // u8g2.begin();   
  //  u8g2.clearBuffer();          // clear the internal memory

   // u8g2.setFont(u8g2_font_7x14B_tr);         // choose a small font for status dispay 
  //  u8g2.drawStr(0,11,"Loading Config..."); 

    // Start RADIO
    //if ( !radio.begin(cc1101_GFSK_250_kb_testing_WIP_L0L) ) // channel 16! Whitening enabled 
    if ( !radio.begin(CFREQ_868, KBPS_250, /* channel num */ 16, /* address */ 0) ) // channel 16! Whitening enabled 
    {
    //  u8g2.setFont(u8g2_font_profont22_tf);  
    //  u8g2.drawStr(6,32, "No CC1101!"); 
    //  u8g2.sendBuffer();     

      // Pause here
      while(1) yield();
    }
    
    //radio.setDevAddress(0x40); // '@' in ascii
    radio.printCConfigCheck();

    Serial.println(F("CC1101 radio initialized."));
/*
    u8g2.setFont(u8g2_font_profont22_tf);  
    u8g2.drawStr(6,32, "CC1101 OK");  
    u8g2.setFont(u8g2_font_7x14B_tr); 
    u8g2.drawStr(0,48,"Listening."); 
    u8g2.setFont(u8g2_font_6x10_tf); 
    //u8g2.drawStr(0,64,"v1.3"); 
    u8g2.sendBuffer();     
*/
    delay(1000);     

    rec_payload.reserve(100);

}


int   counter = 0;
char  output[64] = {0};
char * return_data;

// Values from sensors
float battery_voltage; 
int device_id;
int temp_direct_raw;
int temp_ambient_raw;
int movement; 

// Constants
const float temp_direct_degree_movement  = 24;
const float temp_ambient_degree_movement = 57;

// Calculated 
float temp_direct_calculated;
float temp_ambient_calculated;

void FillLEDsFromPaletteColors( uint8_t colorIndex, uint8_t num_leds)
{
    uint8_t brightness = 128;
     CRGBPalette16 bootPalette = myCustom2z;  // For booting only.    
    for( int i = 0; i < num_leds; i++) {
        leds[i] = ColorFromPalette( bootPalette , colorIndex, brightness, LINEARBLEND);
        colorIndex += 8;
    }

    for( int i = num_leds; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
    }    

      FastLED.show();
}



void loop() 
{
    unsigned long now = millis();


    
    if ( radio.dataAvailable() )
    { 
      
        /*
         * data: D|01|1111|0518|1668|0503|33
         */                      
        rec_payload  = String(radio.receiveChars()); // pointer to memory location of start of string
        device_id    = rec_payload.substring(2,4).toInt();        
        movement     = rec_payload.substring(5,10).toInt();

        battery_voltage = (float) rec_payload.substring(25,27).toInt(); 
        temp_direct_raw     = rec_payload.substring(10,15).toInt()*(battery_voltage/33);
        temp_ambient_raw    = rec_payload.substring(15,20).toInt()*(battery_voltage/33); 


        Serial.print("The device is: ");
        Serial.println(device_id);

        Serial.print("The PIR value is: ");
        Serial.println(movement);   

        Serial.print("The IR direct temp value is: ");
        Serial.println(temp_direct_raw);        

        Serial.print("The IR ambient temp value is: ");
        Serial.println(temp_ambient_raw);        
             
        Serial.print("Battery Voltage: ");
        Serial.println(battery_voltage);   

        Serial.print("The (calculated) IR direct temp: ");      
        temp_direct_calculated =  temp_direct_raw/temp_direct_degree_movement;
        Serial.println(temp_direct_calculated);        

        // A raw value of 1270 = 22 degrees
        // as the number drops, temperature increases
        uint8_t num_leds = map(temp_direct_calculated, 18, 35, 10, 26);
    
        FillLEDsFromPaletteColors(0,  num_leds); 
       
    

        Serial.print("The (calculated) IR ambient temp: ");
        temp_ambient_calculated = 22 + ((1270-temp_ambient_raw)/temp_ambient_degree_movement);
        Serial.println(temp_ambient_calculated);  


        // Display the direct temperature        
       // fill_gradient(leds, temp_ambient_calculated-22+15, CHSV(0, 0,255), CHSV(255,0,0), LONGEST_HUES);    // up to 4 CHSV values
      //  FastLED.show();
       // FastLED.delay(8);


        /*
        // OLED Display
        u8g2.setFont(u8g2_font_6x10_tf); 
        //u8g2.drawStr(0,64,"v1.3"); 
        u8g2.drawStr(0,64,radio.receiveChars()); 
        u8g2.sendBuffer();     
            */
    }
  
    
    if (now > lastSend) 
    {
        memset(output, 0x00, sizeof(output));  
        sprintf(output, "Packet %d ", counter++);
        //radio.sendChars(output);
      
        radio.sendChars("~AGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG :-)~"); // 'B' in ascii
    
        lastSend = now + sendDelay;
    }

  
    if (now > lastStatusDump)
    {        
        //radio.printCCState();
        /*
        radio.printCCFIFOState();
        radio.printMarcstate();
        
        Serial.printf("Heap Free: %d\n", ESP.getFreeHeap());
  */
        lastStatusDump = now + statusDelay; 
    }
    
}
