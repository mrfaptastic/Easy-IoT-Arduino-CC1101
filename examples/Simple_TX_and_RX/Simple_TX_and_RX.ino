#include "cc1101.h"

/*******************************************************************
 * Radio and interrupt configuration
 */

#define RADIO_CHANNEL             16
#define THIS_DEVICE_ADDRESS       22
#define DESINATION_DEVICE_ADDRESS BROADCAST_ADDRESS // Broadcast

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

// External intterupt pin for GDO0
#ifdef ESP32
  #define GDO0_INTERRUPT_PIN 13
#elif ESP8266
  #define GDO0_INTERRUPT_PIN D2
#elif __AVR__
  #define GDO0_INTERRUPT_PIN 5 // Digital D2 or D3 on the Arduino Nano allow external interrupts only
#endif

/*******************************************************************/

// Sketch output & behaviour

#define SEND_STUFF 1  // have this script send things as well
//#define SHOW_CC_STATUS 1


CC1101 radio;

unsigned long lastSend = 0;
unsigned int sendDelay = 10000;

unsigned long lastStatusDump = 0;
unsigned int statusDelay = 5000;

String rec_payload;


/*****************************************************************/
//https://create.arduino.cc/projecthub/Marcazzan_M/how-easy-is-it-to-use-a-thermistor-e39321
/*thermistor parameters:
 * RT0o: 10 000 Ω
 * B: 3977 K +- 0.75%
 * T0:  25 CA
 * +- 5%
 */

//These values are in the datasheet
#define RT0 100000   // Ω
#define B 3977      // K
//--------------------------------------


#define VCC 3.3    //Supply voltage
#define R 160000  //R=10KΩ


//Variables
float RT, VR, ln, TXX, T0o, VRT;





void setup() 
{
    T0o = 25 + 273.15;                 //Temperature T0 from datasheet, conversion from Celsius to kelvin
    
    // Start Serial
    delay (100);
    Serial.begin(115200);
    delay (100);
    Serial.println("Starting...");

    //radio.enableSerialDebug();

    // Start RADIO
    while ( !radio.begin(CFREQ_868, KBPS_250, /* channel num */ 16, /* address */ THIS_DEVICE_ADDRESS, GDO0_INTERRUPT_PIN /* Interrupt */) ) // channel 16! Whitening enabled 
    {
        yield();
    }
    
    radio.printCConfigCheck();
    Serial.println(F("CC1101 radio initialized."));
    delay(1000);     


    rec_payload.reserve(100);

}

int   counter = 0;
char  output[64] = {0};
char * return_data;


void loop() 
{
    unsigned long now = millis();
    
    if ( radio.dataAvailable() )
    { 
      
        /*
         * Data format from CC1110:
         *  V|33|D|000907|000393|000138|002047|002047|002047|001421|0
         *  V|33|D|000902|000393|000138|002047|002047|002047|001423|0
         */                               
        rec_payload  = String(radio.getChars()); // pointer to memory location of start of string
        //Serial.print("Payload size recieved: "); Serial.println(radio.getSize());
        Serial.println(rec_payload);

        byte *payload = radio.getBytes();

        float battery           =  rec_payload.substring(2,4).toInt();
        Serial.print("battery: " ); Serial.println(battery);        

        int movement           =  rec_payload.substring(8,14).toInt();
        //Serial.print("movement: " ); Serial.println(movement);
        
        int thermopile_surface_ir = rec_payload.substring(15,21).toInt();
        //Serial.print("thermopile_surface_ir: "); Serial.println(thermopile_surface_ir);  

        int thermistor_ambient_temp = rec_payload.substring(22).toInt();
        //Serial.print("thermistor_ambient_temp: "); Serial.println(thermistor_ambient_temp);  

        VRT  = thermistor_ambient_temp;     // Acquisition analog value of VRT
        VRT  = (3.30 / 1023.00) * VRT;      // Conversion to voltage
        VR   = VCC - VRT;
        RT   = VRT / (VR / R);              // Resistance of RT
      
        ln = log(RT / RT0);
        TXX = (1 / ((ln / B) + (1 / T0o))); //Temperature from thermistor
      
        TXX = TXX - 273.15;                 //Conversion to Celsius
      
        Serial.print("Temperature:");
        Serial.print("\t");
        Serial.print(TXX);
        Serial.print("C\t\t");
        Serial.print(TXX + 273.15);        //Conversion to Kelvin
        Serial.print("K\t\t");
        Serial.print((TXX * 1.8) + 32);    //Conversion to Fahrenheit
        Serial.println("F");

        // Infrared camer / Thermopile attempt
        float ir_temp = thermopile_surface_ir*(battery/33); // normalise to 3.3 volts due to the ir value going up as battery deplets
        ir_temp /= (float)4.95; // guestimate

        Serial.print("IR temperature:");
        Serial.print("\t");
        Serial.println(ir_temp);      
        
 /*
// Serial output is like:

V|33|D|000196|000104|000000|000000|000000|000000|000393|0
battery: 33
movement: 196
thermopile_surface_ir: 104
thermistor_ambient_temp: 393
Temperature:	25.04C		298.19K		77.08F
IR temperature:	21.01

*/
    }
  
#ifdef SEND_STUFF
    // Periodically send something random.
    if (now > lastSend) 
    {
        memset(output, 0x00, sizeof(output));  
        sprintf(output, "** Sending packet %d ", counter++);
        
        radio.sendChars("----------------------------------", DESINATION_DEVICE_ADDRESS);     
        lastSend = now + sendDelay;
    }
#endif

#ifdef SHOW_CC_STATUS
  
    if (now > lastStatusDump)
    {        
        radio.printCCState();        
        radio.printCCFIFOState(); 
        radio.printMarcstate();
        lastStatusDump = now + statusDelay; 
    }

#endif     


}