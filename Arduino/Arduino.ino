/**
 * Multiboot-Loader for the GBA by Andre Taulien (2018)
 * Original file: https://github.com/ataulien/elm-gba-multiboot/tree/master/gameboy-spi
 * Modified by ministergoose (2024)
 * 
 * This was quickly thrown together for university (we've all been there, right?), so sorry for the lack of documentation.
 * Besides, this project was about using the "elm"-language, so who cares about c?
 */

#include <SPI.h>


// http://problemkaputt.de/gbatek.htm (Multiboot Transfer Protocol)
//
//  Pin    SPI    GBA
//  -----------------
//  12     miso   SO
//  11     mosi   SI
//  13     sck    SC

#define SPI_DELAY 36

const uint8_t COMMAND_STARTUP = 0x01;
const uint8_t COMMAND_WRITE_DONE = 0x02;
const uint8_t COMMAND_EXIT = 0x03;

void setup() {
  Serial.begin(57600);

  Serial.setTimeout(-1);

  SPI.begin();
  SPI.beginTransaction (SPISettings (256000, MSBFIRST, SPI_MODE3));

  Serial.write(COMMAND_STARTUP);

  upload();
}


void loop() {

}

uint32_t ReadSerial32(void) {
  uint32_t rx;

  Serial.readBytes((byte *)&rx, 4);

  return rx;
}

uint32_t WriteSPI32NoDebug(uint32_t w) {
  uint32_t result;
  uint8_t *rx = (uint8_t*)&result;

  rx[3] = SPI.transfer((w >> 24) & 0xFF);
  rx[2] = SPI.transfer((w >> 16) & 0xFF);
  rx[1] = SPI.transfer((w >> 8) & 0xFF);
  rx[0] = SPI.transfer(w & 0xFF);

  delayMicroseconds(SPI_DELAY);

  return result;
}

uint32_t WriteSPI32(uint32_t w, const char* msg) {
  uint32_t r = WriteSPI32NoDebug(w);

  Serial.print(F("0x"));
  Serial.print(r, HEX);
  Serial.print(F(" 0x"));
  Serial.print(w, HEX);
  Serial.print(F("  ; "));
  Serial.println(msg);
  
  return  r;
}


void WaitSPI32(uint32_t w, uint32_t comp, const char* msg) {
  Serial.print(msg);
  Serial.println(comp, HEX);
  
  uint32_t r;

  do
  {
    r = WriteSPI32NoDebug(w);

  } while(r != comp);
}

/**
 * Mostly taken from https://github.com/akkera102/gba_01_multiboot
 * Honestly, it's the best implementation I could find. Straight to the point, no bullshit, no crappy code.
 */
void upload(void) {

  uint32_t fsize = ReadSerial32();
  Serial.print(F("Received ROM-Size: ")); Serial.println(fsize);

  uint8_t header[0xC0];
  size_t rb = Serial.readBytes(header, 0xC0);

  if(fsize > 0x40000)
  {
    Serial.println(F("Romfile too large!"));
    return;
  }
   
  long fcnt = 0;

  uint32_t r, w, w2;
  uint32_t i, bit;


  WaitSPI32(0x00006202, 0x72026202, "Looking for GBA 0x");

  r = WriteSPI32(0x00006202, "Found GBA");
  r = WriteSPI32(0x00006102, "Recognition OK");
  
  Serial.println(F("Send Header(NoDebug)"));
  
  for(i=0; i<=0x5f; i++)
  {
    w = header[2*i];
    w = header[2*i+1] << 8 | w;
    fcnt += 2;

    r = WriteSPI32NoDebug(w);
  }

  r = WriteSPI32(0x00006200, "Transfer of header data complete");
  r = WriteSPI32(0x00006202, "Exchange master/slave info again");

  r = WriteSPI32(0x000063d1, "Send palette data");
  r = WriteSPI32(0x000063d1, "Send palette data, receive 0x73hh****");  

  uint32_t m = ((r & 0x00ff0000) >>  8) + 0xffff00d1;
  uint32_t h = ((r & 0x00ff0000) >> 16) + 0xf;

  r = WriteSPI32((((r >> 16) + 0xf) & 0xff) | 0x00006400, "Send handshake data");
  r = WriteSPI32((fsize - 0x190) / 4, "Send length info, receive seed 0x**cc****");

  uint32_t f = (((r & 0x00ff0000) >> 8) + h) | 0xffff0000;
  uint32_t c = 0x0000c387;

  Serial.println(F("Send encrypted data(NoDebug)"));

  Serial.write(COMMAND_WRITE_DONE);
  
  uint32_t bytes_received = 0;
  while(fcnt < fsize) {
    
    if(bytes_received == 32) {
      Serial.write(COMMAND_WRITE_DONE);
      bytes_received = 0;
    }

    w = ReadSerial32();
    bytes_received += 4;
    
    if(fcnt % 0x800 == 0) {
      Serial.print(fcnt); Serial.print(F("./.")); Serial.println(fsize);
    }


    w2 = w;

    for(bit=0; bit<32; bit++) {
      if((c ^ w) & 0x01) {
        c = (c >> 1) ^ 0x0000c37b;
      } else {
        c = c >> 1;
      }

      w = w >> 1;
    }

    
    m = (0x6f646573 * m) + 1;
    WriteSPI32NoDebug(w2 ^ ((~(0x02000000 + fcnt)) + 1) ^m ^0x43202f2f);

    fcnt = fcnt + 4;
  }

  Serial.print(fcnt); Serial.print(F("./.")); Serial.println(fsize);
  Serial.println(F("ROM sent! Doing checksum now..."));

  for(bit=0; bit<32; bit++) {
    if((c ^ f) & 0x01) {
      c =( c >> 1) ^ 0x0000c37b;
    } else {
      c = c >> 1;
    }

    f = f >> 1;
  }

  WaitSPI32(0x00000065, 0x00750065, "Wait for GBA to respond with CRC 0x");

  
  Serial.print(F("CRC: 0x")); Serial.println(c, HEX);

  r = WriteSPI32(0x00000066, "GBA ready with CRC");
  r = WriteSPI32(c,          "Let's exchange CRC!");

  Serial.println(F("All done, let's hope this worked!"));

  Serial.write(COMMAND_EXIT);
}
