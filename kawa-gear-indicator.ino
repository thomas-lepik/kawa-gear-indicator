/*
* Kawasaki gear indicator. Based on the work of Tom Mitchell (https://bitbucket.org/tomnz/kawaduino)
* and great minds on ECU hacking forum. Thanks alot! :)  http://ecuhacking.activeboard.com/t56234221/kds-protocol/
*
* Confirmed to work on KLE Versys 650, 2008 model.
*/


#define K_OUT 1                    // K Output Line
#define K_IN  0                    // K Input Line

#define BOARD_LED       13
#define REFRESH_MICROS  30000
#define MAXSENDTIME     2000       // 2 second timeout on KDS comms


const uint8_t ISORequestByteDelay = 10;
const uint8_t ISORequestDelay = 40; // Time between requests.

const uint8_t ECUaddr = 0x11;
const uint8_t myAddr = 0xF2;

const uint8_t validRegs[] = { 0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x07, 0x08,
  0x09, 0x0A, 0x0B, 0x0C, 0x20, 0x27, 0x28, 0x29, 0x2A, 0x2E, 0x31, 0x32,
  0x33, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x44, 0x54, 0x56, 0x5B, 0x5C, 0x5D,
  0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x6E,
  0x6F, 0x80, 0x9B, 0xA0, 0xB4 };

const uint8_t numValidRegs = (uint8_t)(sizeof(validRegs));

const uint8_t n[] = { 0xFC, 0x18, 0x6E, 0x3E, 0x9A, 0xB6, 0xF6, 0x1C, 0xFE, 0xBE };  // 7-seg LED, numbers. wiring from a..g is in sync with PORTD bits starting from LSB 

bool ECUconnected = false;

uint8_t gear = 0; 

void setup() {

  DDRB  = B00111111;  // two upper bits for xtal
  DDRD  = B11111110;  // two lower bits for rx/tx. Set RX to input 

  PORTB = B00000000;
  PORTD = B00000000;
}

void loop() {
  
  digitalWrite(BOARD_LED, HIGH);
  delay(200);
  digitalWrite(BOARD_LED, LOW);
  delay(100);
  digitalWrite(BOARD_LED, HIGH);
  delay(200);
  digitalWrite(BOARD_LED, LOW);
        
  uint8_t cmdSize;
  uint8_t cmdBuf[6];
  uint8_t respSize;
  uint8_t respBuf[12];
  uint8_t ect;

  if (!ECUconnected) {
    digitalWrite(BOARD_LED, LOW);
    ECUconnected = initPulse();
  }

  while (ECUconnected) {
     
    cmdSize = 2;       // each request is a 2 byte packet
    cmdBuf[0] = 0x21;  // register request cmd
    
      
      for (uint8_t i = 0; i < 5; i++) {
        respBuf[i] = 0;
      }
          
      cmdBuf[1] = 0x0B;  // get gear no
      respSize = sendRequest(cmdBuf, respBuf, cmdSize, 12);

       gear = (uint8_t) respBuf[2]; // ? vs 1
       
      if (respSize == 3) {
        led(gear);
      }
      else if (respSize == 0) {
        ECUconnected = false;
        led(9);  // err
        break;
      }
      else {
        led(8);  // another err
      }

      delayLeds(ISORequestDelay, true);
    }

     digitalWrite(BOARD_LED, LOW);  // diag

  delay(3000);  // auto-reconnect
}


// custom delay routine that updates LEDs while idle
void delayLeds(uint8_t ms, boolean safe) {
  digitalWrite(BOARD_LED, HIGH);   // diag
  delay(ms);
  digitalWrite(BOARD_LED, LOW);    // diag
}


// Init the connection to ECU. This is the ISO 14230-2 "Fast Init" sequence.
bool initPulse() {
  
  uint8_t rLen;
  uint8_t req[2];
  uint8_t resp[3];

  Serial.end();
  
  digitalWrite(K_OUT, HIGH);
  delay(300);
  digitalWrite(K_OUT, LOW);
  delay(25);
  digitalWrite(K_OUT, HIGH);
  delay(25);

  Serial.begin(10400); // ISO K-line port speed
  
  req[0] = 0x81;       // Start Communication is a single byte "0x81" packet.
  rLen = sendRequest(req, resp, 1, 3);

  delay(ISORequestDelay);

  // response should be 3 bytes: 0xC1 0xEA 0x8F
  if ((rLen == 3) && (resp[0] == 0xC1) && (resp[1] == 0xEA) && (resp[2] == 0x8F)) {
    
    // if success, send the Start Diag frame: 2 bytes: 0x10 0x80
    
    req[0] = 0x10;
    req[1] = 0x80;
    
    rLen = sendRequest(req, resp, 2, 3);
    
    if ((rLen == 2) && (resp[0] == 0x50) && (resp[1] == 0x80)) {
      return true;
    }
  }
  
  return false; // otherwise, init failed.
}


// Send a request to the ECU and wait for the response. Returns number of bytes of response returned.
uint8_t sendRequest(const uint8_t *request, uint8_t *response, uint8_t reqLen, uint8_t maxLen) {
  
  uint8_t buf[16], rbuf[16];
  uint8_t bytesToSend;
  uint8_t bytesSent = 0;
  uint8_t bytesToRcv = 0;
  uint8_t bytesRcvd = 0;
  uint8_t rCnt = 0;
  uint8_t c, z;
  bool forMe = false;
  char radioBuf[32];
  uint32_t startTime;
  
  for (uint8_t i = 0; i < 16; i++) {
    buf[i] = 0;
  }
  
  for (uint8_t i = 0; i < maxLen; i++) {
    response[i] = 0;  // zero the response buffer up to maxLen
  }

  if (reqLen == 1) {
    buf[0] = 0x81;
  }
  else {
    buf[0] = 0x80;
  }
  
  buf[1] = ECUaddr;
  buf[2] = myAddr;

  if (reqLen == 1) {
    buf[3] = request[0];
    buf[4] = calcChecksum(buf, 4);
    bytesToSend = 5;
  }
  else {
    buf[3] = reqLen;
    for (z = 0; z < reqLen; z++) {
      buf[4 + z] = request[z];
    }
    buf[4 + z] = calcChecksum(buf, 4 + z);
    bytesToSend = 5 + z;
  }
  
  for (uint8_t i = 0; i < bytesToSend; i++) {
    bytesSent += Serial.write(buf[i]);    // send the command
    delay(ISORequestByteDelay);
  }
  
  delayLeds(ISORequestDelay, false); // Wait required time for response.
  
  startTime = millis();

  while ((bytesRcvd <= maxLen) && ((millis() - startTime) < MAXSENDTIME)) {    // wait and deal with the reply
    
    if (Serial.available()) {
    
      c = Serial.read();
      startTime = millis(); // reset the timer on each byte received

      delayLeds(ISORequestByteDelay, true);

      rbuf[rCnt] = c;
      switch (rCnt) {
      
      case 0:
        
        if (c == 0x81) { // should be an addr packet either 0x80 or 0x81
          bytesToRcv = 1;
        }
        else if (c == 0x80) {
          bytesToRcv = 0;
        }
        rCnt++;
        break;
        
      case 1:
        if (c == myAddr) { // should be the target address
          forMe = true;
        }
        rCnt++;
        break;
        
      case 2:
        if (c == ECUaddr) { // should be the sender address
          forMe = true;
        } else if (c == myAddr) {
          forMe = false; // ignore the packet if it came from us!
        }
        rCnt++;
        break;
        
      case 3:
        if (bytesToRcv == 1) { // should be the number of bytes, or the response if its a single byte packet.
          bytesRcvd++;
          if (forMe) {
            response[0] = c; // single byte response so store it.
          }
        } else {
          bytesToRcv = c;  // number of bytes of data in the packet.
        }
        rCnt++;
        break;
        
      default:
        if (bytesToRcv == bytesRcvd) {
          if (forMe) { // Only check the checksum if it was for us - don't care otherwise!
            
            if (calcChecksum(rbuf, rCnt) == rbuf[rCnt]) { // Checksum OK
              return(bytesRcvd);
            } else {
              return(0);   // Checksum Error.
            }
          }
          
          // Reset the counters
          rCnt = 0;
          bytesRcvd = 0;
          
          delayLeds(ISORequestDelay, true);  // ISO 14230 specifies a delay between ECU responses.
        }
        else { // must be data, so put it in the response buffer. rCnt must be >= 4 to be here.
          
          if (forMe) {
            response[bytesRcvd] = c;
          }
          
          bytesRcvd++;
          rCnt++;
        }
        break;
      }
    }
  }

  return false;
}

// checksum is the sum of all data bytes modulo 0xFF
uint8_t calcChecksum(uint8_t *data, uint8_t len) {
  uint8_t crc = 0;

  for (uint8_t i = 0; i < len; i++) {
    crc = crc + data[i];
  }
  return crc;
}

// common cathode 7-seg led directly connected to PORTD and PORTB
void led(uint8_t i) {

  if (i >= 0 && i <= 9) {
    uint8_t z = n[i];

    if (bitRead(z, 1)) {
      PORTB = B00000001;
    }
    else {
      PORTB = B00000000;
    }

    z ^= 0x2; // toggle second least bit 0
    PORTD = z;
  }
  else {
    PORTD != 0x2;
  } 
}
