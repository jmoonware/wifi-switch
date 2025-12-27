#include <WiFi.h>
#include <NTPClient.h>
#include <AsyncUDP_RP2040W.h>
#include <Adafruit_BME280.h>

// secret, contains definition of local_ssid, local_pass, and data_url in three lines, literally: 
// char local_ssid[] = "NNN";  //  your network SSID (name)
// char local_pass[] = "PPP";  // your network password
#include "/home/jmoon/Arduino/libraries/local/ssid_harvest.h"
IPAddress static_ip(192,168,1,11);
IPAddress static_dns(192,168,1,2);
IPAddress static_gateway(192,168,1,1);
IPAddress static_subnet(255,255,255,0);

#include <elapsedMillis.h>
#include "src/DateTimeNTP/DateTimeNTP.h"

#define VERSION_STRLEN 9
char version[] = "202512261";

// NTP time stuff
WiFiUDP ntpUDP;
NTPClient theNTPUDPClient(ntpUDP);
DateTimeNTP dtntp(&theNTPUDPClient);
int wifi_status = WL_IDLE_STATUS;     // the Wifi radio's status

// UDP stuff
AsyncUDP udp;
#define UDP_LISTEN_PORT 8225
#define INCOMING_UDP_PACKET_SZ 64
unsigned char incoming_packet_buf[INCOMING_UDP_PACKET_SZ];
#define OUTGOING_UDP_PACKET_SZ 64
unsigned char outgoing_packet_buf[OUTGOING_UDP_PACKET_SZ];
#define INCOMING_UDP_PACKET_DATA_SZ 32

elapsedMillis update_millis;
elapsedMillis bme_update_millis;

uint32_t update_delay = 1*1000; // ms, for raw sensor data
uint32_t bme_update_delay = 60*1000; // ms, for BME T/H/P sensors

enum GIZMO_STATES {
  STATE_UPDATE,
  STATE_UPDATE_TEMP,
  STATE_WAIT
};
uint8_t gizmo_state;

// error bits
enum GIZMO_ERRORS {
  ERROR_NONE = 0,
  ERROR_NTP_INIT = 1,
  ERROR_BMEA_INIT = 2,
  ERROR_BMEB_INIT = 4,
};
static uint16_t gizmo_error_reg = 0;

// i2c addr 0x76
Adafruit_BME280 theBME280A; // = Adafruit_BME280();
static float last_bme280a_temperature=0;
static float last_bme280a_humidity=0;
static float last_bme280a_pressure=0;

// i2c addr 0x77
Adafruit_BME280 theBME280B; // = Adafruit_BME280();
static float last_bme280b_temperature=0;
static float last_bme280b_humidity=0;
static float last_bme280b_pressure=0;


// GPIO to control solid state relay
#define SWITCH1_PIN D2 // physical pin 4 on Pico W board
#define SWITCH2_PIN D3 // physical pin 5 on Pico W board
#define SWITCH3_PIN D4 // physical pin 6 on Pico W board

void read_bme280() 
{
  // The BME280 will read the sensors once (storing in registers)
  // then go back to sleep in "forced" mode.
  // Otherwise, it is continuously updating the registers with t_sb delay
  // the Adafruit lib defaults to 250 ms delay, which is enough to heat 
  // the sensor
  if(theBME280A.takeForcedMeasurement()) {
    last_bme280a_temperature = theBME280A.readTemperature();
    last_bme280a_humidity = theBME280A.readHumidity();
    last_bme280a_pressure = theBME280A.readPressure();
  }
  if(theBME280B.takeForcedMeasurement()) {
    last_bme280b_temperature = theBME280B.readTemperature();
    last_bme280b_humidity = theBME280B.readHumidity();
    last_bme280b_pressure = theBME280B.readPressure();
  }

}

static float last_board_T=-9999;

void read_board_T() {
  last_board_T = analogReadTemp();
}

enum PACKET_COMMANDS {
  PCOMMAND_RESERVED,
  PCOMMAND_STATUS,
  PCOMMAND_UPTIME,
  PCOMMAND_READ_SWITCH_STATE,
  PCOMMAND_READ_BMEA_VALS,
  PCOMMAND_READ_BMEB_VALS,
  PCOMMAND_READ_BOARD_T, 
  PCOMMAND_NUM_READ_COMMANDS,
  PCOMMAND_SET_SWITCH_STATE,
  PCOMMAND_SET_THP_UPDATE_TIME,
};

enum PACKET_ERRORS {
  PERR_NONE,
  PERR_UNK_COMMAND,
  PERR_CHECKSUM,
  PERR_NO_ACK,
  PERR_COUNT_RANGE
};

#define ACK_BYTE 0x06
#define NACK_BYTE 0x15

static uint32_t last_packet_length = 0;
static uint16_t last_remote_port = 0;
static uint8_t  last_packet_error = PERR_NONE;
static uint16_t outgoing_data_len = 0;
static uint32_t received_packet_count = 0;

void checksum_packet(unsigned char *buf, uint16_t buflen) {
  uint16_t checksum=0;
  for (int i=0; i < buflen-2; ++i) {
    checksum += buf[i];
  }
  // last two bytes are checksum
  buf[buflen-2]=(uint8_t)(checksum&255);
  buf[buflen-1]=(uint8_t)(checksum>>8);
}

void parsePacket(AsyncUDPPacket packet) {

    received_packet_count+=1;
    IPAddress ip = packet.remoteIP();
    last_remote_port = packet.remotePort();
    last_packet_length = packet.length();
    last_packet_error = PERR_NONE;

    memcpy((uint8_t *)incoming_packet_buf, (const uint8_t *)packet.data(), packet.length());

    outgoing_data_len = 0;
    // first byte = 0x06 (ACK); last two bytes are checksum
    if (incoming_packet_buf[0]==0x06 && last_packet_length > 2) { 
      outgoing_packet_buf[0]=ACK_BYTE;
      outgoing_packet_buf[1]=incoming_packet_buf[1];
      switch(incoming_packet_buf[1]) { // command byte
        case PCOMMAND_STATUS:
        {
          uint32_t uptime_secs = dtntp.last_secs-dtntp.init_secs;
          outgoing_packet_buf[2]=uptime_secs&255;
          outgoing_packet_buf[3]=(uptime_secs>>8)&255;
          outgoing_packet_buf[4]=(uptime_secs>>16)&255;
          outgoing_packet_buf[5]=(uptime_secs>>24)&255;
          int16_t scaled_T = (int16_t)(last_board_T*10);
          outgoing_packet_buf[6]=(uint8_t)(scaled_T&255);
          outgoing_packet_buf[7]=(uint8_t)((scaled_T>>8)&255);
          outgoing_packet_buf[8]=(uint8_t)((received_packet_count)&255);
          outgoing_packet_buf[9]=(uint8_t)((received_packet_count>>8)&255);
          outgoing_packet_buf[10]=(uint8_t)((received_packet_count>>16)&255);
          outgoing_packet_buf[11]=(uint8_t)((received_packet_count>>24)&255);
          outgoing_packet_buf[12]=(uint8_t)(bme_update_delay&255);
          outgoing_packet_buf[13]=(uint8_t)((bme_update_delay>>8)&255);
          outgoing_packet_buf[14]=(uint8_t)(gizmo_error_reg&255);
          outgoing_packet_buf[15]=(uint8_t)((gizmo_error_reg>>8)&255);
          for (int i=0; i < VERSION_STRLEN; ++i) {
            outgoing_packet_buf[i+16]=version[i];
          }
          outgoing_data_len=18+VERSION_STRLEN;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_UPTIME:
        {
          uint32_t uptime_secs = dtntp.last_secs-dtntp.init_secs;
          outgoing_packet_buf[2]=(uint8_t)(uptime_secs&255);
          outgoing_packet_buf[3]=(uint8_t)((uptime_secs>>8)&255);
          outgoing_packet_buf[4]=(uint8_t)((uptime_secs>>16)&255);
          outgoing_packet_buf[5]=(uint8_t)((uptime_secs>>24)&255);
          outgoing_data_len=8;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_SWITCH_STATE:
        {
          PinStatus state = digitalRead(SWITCH1_PIN);
          outgoing_packet_buf[2]=(uint8_t)(state);
          state = digitalRead(SWITCH2_PIN);
          outgoing_packet_buf[3]=(uint8_t)(state);
          state = digitalRead(SWITCH3_PIN);
          outgoing_packet_buf[4]=(uint8_t)(state);
          outgoing_data_len=7;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_BOARD_T:
        {
          int16_t scaled_T = (int16_t)(last_board_T*10);
          outgoing_packet_buf[2]=(uint8_t)(scaled_T&255);
          outgoing_packet_buf[3]=(uint8_t)((scaled_T>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_BMEA_VALS:
        {
          int16_t scaled_T = (int16_t)(last_bme280a_temperature*10);
          outgoing_packet_buf[2]=(uint8_t)(scaled_T&255);
          outgoing_packet_buf[3]=(uint8_t)((scaled_T>>8)&255);
          int16_t scaled_H = (int16_t)(last_bme280a_humidity*10);
          outgoing_packet_buf[4]=(uint8_t)(scaled_H&255);
          outgoing_packet_buf[5]=(uint8_t)((scaled_H>>8)&255);
          int16_t scaled_P = (int16_t)(0.1*last_bme280a_pressure/3.387); // 100x inHg vals
          outgoing_packet_buf[6]=(uint8_t)(scaled_P&255);
          outgoing_packet_buf[7]=(uint8_t)((scaled_P>>8)&255);
          outgoing_data_len=10;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_READ_BMEB_VALS:
        {
          int16_t scaled_T = (int16_t)(last_bme280b_temperature*10);
          outgoing_packet_buf[2]=(uint8_t)(scaled_T&255);
          outgoing_packet_buf[3]=(uint8_t)((scaled_T>>8)&255);
          int16_t scaled_H = (int16_t)(last_bme280b_humidity*10);
          outgoing_packet_buf[4]=(uint8_t)(scaled_H&255);
          outgoing_packet_buf[5]=(uint8_t)((scaled_H>>8)&255);
          int16_t scaled_P = (int16_t)(0.1*last_bme280b_pressure/3.387); // 100x inHg vals
          outgoing_packet_buf[6]=(uint8_t)(scaled_P&255);
          outgoing_packet_buf[7]=(uint8_t)((scaled_P>>8)&255);
          outgoing_data_len=10;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          break;
        }
        case PCOMMAND_SET_SWITCH_STATE:
        {
          uint8_t sw1 = (uint8_t)incoming_packet_buf[2]; 
          uint8_t sw2 = (uint8_t)incoming_packet_buf[3]; 
          uint8_t sw3 = (uint8_t)incoming_packet_buf[4];           
          outgoing_packet_buf[2]=sw1; 
          outgoing_packet_buf[3]=sw2; 
          outgoing_packet_buf[4]=sw3; 
          outgoing_data_len=7;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          if (sw1 == 1) { 
            digitalWrite(SWITCH1_PIN, HIGH);
          }
          else if (sw1 == 0) {
            digitalWrite(SWITCH1_PIN, LOW);
          }
          else last_packet_error = PERR_COUNT_RANGE; 

          if (sw2 == 1) { 
            digitalWrite(SWITCH2_PIN, HIGH);
          }
          else if (sw2 == 0) {
            digitalWrite(SWITCH2_PIN, LOW);
          }
          else last_packet_error = PERR_COUNT_RANGE; 

          if (sw3 == 1) { 
            digitalWrite(SWITCH3_PIN, HIGH);
          }
          else if (sw3 == 0) {
            digitalWrite(SWITCH3_PIN, LOW);
          }
          else last_packet_error = PERR_COUNT_RANGE; 

          break;
        }
        case PCOMMAND_SET_THP_UPDATE_TIME:
        {
          uint16_t tcount = (uint16_t)incoming_packet_buf[2]+(((uint16_t)incoming_packet_buf[3])<<8);
          outgoing_packet_buf[2]=(uint8_t)(tcount&255);
          outgoing_packet_buf[3]=(uint8_t)((tcount>>8)&255);
          outgoing_data_len=6;
          checksum_packet(outgoing_packet_buf, outgoing_data_len);
          if (tcount < 3600) { // max 1 hour
            bme_update_delay = tcount*1000; // ms 
          }
          else last_packet_error = PERR_COUNT_RANGE; 
          break;
        }
        default:
        {
          last_packet_error = PERR_UNK_COMMAND;
          break;
        }
      }

    }
    else {
      last_packet_error = PERR_NO_ACK;
    }

    // there was an error so send a NACK 
    if (last_packet_error!=PERR_NONE) {
      outgoing_packet_buf[0]=NACK_BYTE;  
      outgoing_packet_buf[1]=last_packet_error;
      outgoing_packet_buf[2]=0; // reserved
      outgoing_packet_buf[3]=0; // reserved
      outgoing_data_len = 4;
    }

    // alsways send response packet
    packet.write((uint8_t*) outgoing_packet_buf, outgoing_data_len);
}



// initialize I2C
// SDA = D0, physical pin 1
// SCL = D1, physical pin 2
TwoWire theWire(i2c0,D0,D1);

/////////////////////////////////////////
// SETUP
/////////////////////////////////////////

void setup() {
  // put your setup code here, to run once:
  digitalWrite(PIN_LED, HIGH);
  delay(1000);
  digitalWrite(PIN_LED, LOW);
  delay(1000);

  // make sure switch is off...
  pinMode(SWITCH1_PIN,OUTPUT);
  digitalWrite(SWITCH1_PIN, LOW);

  pinMode(SWITCH2_PIN,OUTPUT);
  digitalWrite(SWITCH2_PIN, LOW);

  pinMode(SWITCH3_PIN,OUTPUT);
  digitalWrite(SWITCH3_PIN, LOW);

  while (wifi_status != WL_CONNECTED) {
    WiFi.config(static_ip,static_dns, static_gateway,static_subnet);
    wifi_status = WiFi.begin(local_ssid,local_pass);
    digitalWrite(PIN_LED, HIGH);
    delay(100);
    digitalWrite(PIN_LED, LOW);
    // wait for connection:
    delay(1000);
  }

  // start the date time NTP updates
  #define NTP_RETRIES 3 
  uint8_t retries = 0;
  while (!dtntp.start() && retries < NTP_RETRIES) {
    wifi_status = WiFi.disconnect();
    while (wifi_status != WL_CONNECTED) {
      WiFi.config(static_ip,static_dns, static_gateway,static_subnet);
      wifi_status = WiFi.begin(local_ssid,local_pass);
      // wait for connection:
      delay(1000);
    }
    delay(1000);
    retries+=1;
  }
  if (retries==NTP_RETRIES) {
    gizmo_error_reg|=ERROR_NTP_INIT;
    // quick blinks for BME failure
    for (int i=0; i < 2; ++i) {
      digitalWrite(PIN_LED, HIGH);
      delay(200);
      digitalWrite(PIN_LED, LOW);
      delay(200);
    }
    delay(1000);
  }

  // reset loop update clock
  update_millis = 0;
  bme_update_millis = bme_update_delay;

  // set up UDP listen and callback 
  if(udp.listen(UDP_LISTEN_PORT)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      parsePacket(packet);
    });
  }

  // set up BME280 temp/hum/press sensor
  unsigned bmea_status = theBME280A.begin(0x76,&theWire);
  int bme_retries = 2;
  for (int i=0; i < bme_retries; ++i) {
    if (bmea_status) {
      break;
    }
    bmea_status = theBME280A.begin(0x76,&theWire);
    delay(1000);
  }

  if (!bmea_status) {
    gizmo_error_reg|=ERROR_BMEA_INIT;
    // quick blinks for BME failure
    for (int i=0; i < 3; ++i) {
      digitalWrite(PIN_LED, HIGH);
      delay(100);
      digitalWrite(PIN_LED, LOW);
      delay(100);
    }
    delay(1000);
  }

  // set up BME280 temp/hum/press sensor
  unsigned bmeb_status = theBME280B.begin(0x77,&theWire);
  for (int i=0; i < bme_retries; ++i) {
    if (bmeb_status) {
      break;
    }
    bmeb_status = theBME280B.begin(0x77,&theWire);
    delay(1000);
  }


  if (!bmeb_status) {
    gizmo_error_reg|=ERROR_BMEB_INIT;
    // quick blinks for BME failure
    for (int i=0; i < 4; ++i) {
      digitalWrite(PIN_LED, HIGH);
      delay(100);
      digitalWrite(PIN_LED, LOW);
      delay(100);
    }
    delay(1000);
  }

  // set to forced mode
  if (bmea_status) {
    theBME280A.setSampling(Adafruit_BME280::sensor_mode::MODE_FORCED,Adafruit_BME280::SAMPLING_X1,Adafruit_BME280::SAMPLING_X1,Adafruit_BME280::SAMPLING_X1);
  }
  if (bmeb_status) {
    theBME280B.setSampling(Adafruit_BME280::sensor_mode::MODE_FORCED,Adafruit_BME280::SAMPLING_X1,Adafruit_BME280::SAMPLING_X1,Adafruit_BME280::SAMPLING_X1);
  }

  // two long blinks for init finish
  for (int i=0; i < 2; ++i) {
    digitalWrite(PIN_LED, HIGH);
    delay(500);
    digitalWrite(PIN_LED, LOW);
    delay(500);
  }


}

//////////////////////////////////
//         LOOP
//////////////////////////////////

void loop() {

  if (update_millis > update_delay) {
      gizmo_state = STATE_UPDATE;
  }
  else if (bme_update_millis >= bme_update_delay) {
    gizmo_state = STATE_UPDATE_TEMP;
  }
  else {
    gizmo_state = STATE_WAIT;
  }


  switch (gizmo_state) {
    case STATE_UPDATE:
    {
      update_millis = 0;
      // update sensor values
      read_board_T();
      dtntp.get_date();
      break;
    }
    case STATE_UPDATE_TEMP:
    {
      read_bme280();
      bme_update_millis=0;
      break;
    }
    case STATE_WAIT:
    {
      break;
    }
    default:
    {
      break;
    }
  }


}
