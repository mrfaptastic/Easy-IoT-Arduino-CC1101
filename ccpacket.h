/* Defines the underlying CC1101 radio packet structure relevant to the configuration
 *  being used in this library.
 *  
 * Also defined the concept of a 'stream' which is essentially a poor mans version
 * of a higher-level stream of data, which can sit atop many CC1101 radio packets 
 * which is best efforts only!
 */
#ifndef _CCPACKET_H
#define _CCPACKET_H

#define MAX_STREAM_LENGTH        200 // Impacts the amount of memory that will get used & how much we can send and receive in one hit. 
                                     // This avoids dynamic memory allocation which is bad on embedded devices.   


/**
 * Buffer and data lengths
 */
#define CCPACKET_MAX_SIZE                   61 			// MUST ONLY BE 61 based on library's CC1101_PKTLEN (61 to accommodate CC address and 2 bytes for lqi & rssi)
#define STREAM_PKT_OVERHEAD                 4  			// 1 byte destination address (CC hardware requirement!), 1 byte payload_size, 1 byte num_of_packets, 1 byte seq_num (forms part of payload)
#define STREAM_PKT_MAX_PAYLOAD_SIZE         CCPACKET_MAX_SIZE-STREAM_PKT_OVERHEAD // Remaining payload space available = 57

#define CCPACKET_REC_SIZE               CCPACKET_MAX_SIZE+2    // Payload + 2 bytes for lqi and rssi (63 bytes)

// HACK: Memory hog this is. Not sure if it's needed except for received streams.
static unsigned char stream_multi_pkt_buffer[MAX_STREAM_LENGTH];     // unsigned char = byte                   


struct CCPACKET
{
  public:

    /* CC recipient device ID (receiving CC1101 will filter, unless it's 0x00 or 0xFF which is a broadcast message) */
    unsigned char cc_dest_address;  // 1 byte

    /* Our  Stuff  */	
    unsigned char payload_size;             // 1 byte - This is NOT a Packet Length field as we are using fixed packet size config for both the sender and receiver.	
    unsigned char stream_num_of_pkts;      // 1 byte       
    unsigned char stream_pkt_seq_num;      // 1 byte       

    /* Usable user data buffer */
    unsigned char payload[STREAM_PKT_MAX_PAYLOAD_SIZE]; // 60 bytes minus 3 bytes for src_address, stream_length, and stream_seq_num


    /**
     * CRC OK flag
     */    
    bool crc_ok;
    
    /**
     * CHIPCON Received Strength Signal Indication
     */
    unsigned char rssi;
    
    /**
     * CHIPCON Link Quality Index
     */
    unsigned char lqi;
};


#endif
