#include <Arduino.h>
#include "cc1101.h"


const int THIS_DEVICE_ADDRESS      =  32; // flip these around when compiling for different devices
const int RECEIVING_DEVICE_ADDRESS =  64;

// Create CC1101 Class Instance
CC1101 radio;

unsigned long lastSend = 0;
unsigned int sendDelay = 10000;

unsigned long lastStatusDump = 0;
unsigned int statusDelay = 5000;

String rx_payload;



void setup() 
{
    // Allocate memory
    rx_payload.reserve(200);  
    
    // Start Serial
    delay (1000);
    Serial.begin(115200);
    delay (100);
    Serial.println("Starting...");

    // Start RADIO
    if ( !radio.begin(CFREQ_868, KBPS_250, /* channel num */ 16, /* address */ THIS_DEVICE_ADDRESS) ) // Channel 16. This device has an address of 64
    {
      // Pause here
      while(1)
      {
        Serial.println(F("Something is wrong with the CC1101 setup."));
        delay(2000);
      }
    }
    
    
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
      
        /*
         * data: D|01|1111|0518|1668|0503|33
         */       
                        
        rx_payload  = String(radio.receiveChars()); // pointer to memory location of start of string

        Serial.print("Recieved Message: ");
        Serial.println(rx_payload);
        
    }
  

    // Send periodically
    if (now > lastSend) 
    {
        char tx_payload[200];
        memset(tx_payload, 0x00, sizeof(tx_payload)); // flush
        sprintf(tx_payload, "Message #%d from device %d. Hello %d, I hope this message gets to you well and good. This message is approx 130 characters in length.", tx_counter++, THIS_DEVICE_ADDRESS, RECEIVING_DEVICE_ADDRESS);

        Serial.print("Sending message: ");
        Serial.println(tx_payload);
        radio.sendChars(tx_payload, RECEIVING_DEVICE_ADDRESS); // Send the above Arduino String class as a c char array
    
        lastSend = now + sendDelay;
    }
   
} // end loop
