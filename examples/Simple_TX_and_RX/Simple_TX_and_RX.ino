/* Two devices are going to be required to test this obviously. */

#include "cc1101.h"

/*******************************************************************
 * Radio, Device ID and Interrupt Pin configuration                */


#define RADIO_CHANNEL         16
#define DEVICE_ADDRESS        20 // this device
#define DEST_ADDRESS          0  // broadcast
#define RECIEVE_ONLY          1  // on the transmitting station - change this.

/* In your projects platformio.ini file configure the following, but only change the DEVICE_ADDRESS for each device:
build_flags = 
    -DRADIO_CHANNEL=16
	-DDEVICE_ADDRESS=20
	-DDEST_ADDRESS=0 ; THis is the broadcast channel

*/

/** 
 *  NOTE: On ESP8266 can't use D0 for interrupts on WeMos D1 mini.
 *        Using D0 with the 
 *        
 *  "All of the IO pins have interrupt/pwm/I2C/one-wire support except D0"
 * 
 * CC1101 GDO2 is connected to D2 on the ESP8266
 * CC1101 GDO0 isn't connected to anything.
 * Both GDO2 and GDO0 are configured in the CC1101 to behave the same.
 *
 */

// External interrupt pin for GDO0
#ifdef ESP32
  #define GDO0_INTERRUPT_PIN 13 
  #pragma message "esp32"
#elif ESP8266
  #define GDO0_INTERRUPT_PIN D2
#elif __AVR__
  #define GDO0_INTERRUPT_PIN 5 // Digital D2 or D3 on the Arduino Nano allow external interrupts only
#endif



CC1101 radio;

unsigned long lastSend = 0;
unsigned int sendDelay = 0;

int counter = 0;
String  send_payload;
String  recieve_payload;

void setup() 
{
    Serial.begin(115200);
    delay (100);

    pinMode(LED_BUILTIN, OUTPUT);


    Serial.println("Starting...");

    //radio.set_debug_level(1);

    // Start RADIO
    while (!radio.begin(CFREQ_433, RADIO_CHANNEL, DEVICE_ADDRESS, GDO0_INTERRUPT_PIN /* Interrupt */));   // channel 16! Whitening enabled 

    radio.setOutputPowerLeveldBm(10); // max power
     
    delay(1000); // Try again in 5 seconds
    //radio.printCConfigCheck();     

    Serial.println(F("CC1101 radio initialized."));
    recieve_payload.reserve(512);

    // IMPORTANT: Kick the radio into receive mode, otherwise it will sit IDLE and be TX only.
    radio.setRxState();

    sendDelay = 6000; //random(1000, 3000);
#if !defined(RECIEVE_ONLY)    
    Serial.printf("Sending a message every %d ms.\n", sendDelay);
#endif 

  //  radio.printPATable();

}

//void loop() { }
void loop() 
{
    unsigned long now = millis();
    
    if ( radio.dataAvailable() )
    {       
        Serial.println("Data available.");
        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)           

        recieve_payload  = String(radio.getChars()); // pointer to memory location of start of string
        //Serial.print("Payload size recieved: "); Serial.println(radio.getSize());
        Serial.print("Payload received: ");
        Serial.println(recieve_payload);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW   

    }

    // Periodically send something random.
    if (now > lastSend) 
    {

        radio.printMarcstate();
#if !defined(RECIEVE_ONLY)

        Serial.println("Sending message.");        
        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
        //send_payload = "Sending a large and long messages " + String (counter) + " from device " + String(DEVICE_ADDRESS) + ". Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book.";
        //send_payload = "0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789-0123456789";
        send_payload = "0123456789------------------------------------------012345";
     //   send_payload = "0123456789---------------------------------------------XX";
        //    send_payload = "0123456789";    
        //radio.sendChars("Testing 123", DEST_ADDRESS);     
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