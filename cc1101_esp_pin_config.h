// NOTE: CAN't USE D0 for interrupts on WeMos D1 mini!!
// "All of the IO pins have interrupt/pwm/I2C/one-wire support except D0"


// -----------------------------------------------------------------
// The pin number on the ESP device that connects to the GD0 pin on the CC1101
//
#if defined ESP32
  #define CC1101_GDO0 34   // Use GPIO PIN 34 as the interrupt pin
#elif defined ESP8266 // assume it's an ESP8266
  #define CC1101_GDO0 D2   // Use GPIO PIN D2 as the interrupt pin
#else
  #pragma message "UNKNOWN DEVICE!"
  #define CC1101_GDO0 0
#endif 
// -----------------------------------------------------------------
