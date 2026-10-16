#pragma once

#include "OpenKNX.h"
#include "hardware.h"

// wiegand pins are defined in hardware config, hardware without wiegand interface has no pins
#ifndef Wiegand_D0
  #define Wiegand_D0 -1
#endif
#ifndef Wiegand_D1
  #define Wiegand_D1 -1
#endif
#ifndef Wiegand_LED
  #define Wiegand_LED -1
#endif
#ifndef Wiegand_W34
  #define Wiegand_W34 -1
#endif

#define WIEGAND_MAX_BITS 64
#define WIEGAND_MAX_DATA_BYTES 10        // same size as NFC tag UID in flash storage
#define WIEGAND_FRAME_TIMEOUT 25         // ms without pulse marks end of frame
#define WIEGAND_CARD_REPEAT_DELAY 1500   // ms, same card read again within this time is ignored
#define WIEGAND_KEY_BUFFER_SIZE 16

#ifndef WIEGAND_OUTPUT_ACTIVE_ON
  #define WIEGAND_OUTPUT_ACTIVE_ON HIGH
#endif

/*
Wiegand reader interface (RFID/NFC cards and keypad):
- D0/D1 are low active (idle high), each pulse is one bit (D0 = 0, D1 = 1)
- 4 bit frame:  one key (0-9, 10 = '*', 11 = '#')
- 8 bit frame:  one key, upper nibble is inverted lower nibble
- 26/34 bit:    card with even parity (first half) and odd parity (second half)
- other >= 26:  card, first and last bit are treated as parity bits (not checked)
Card data (without parity) is provided big endian, padded with zeros to WIEGAND_MAX_DATA_BYTES.
*/
class Wiegand
{
  public:
    std::string logPrefix();
    void init();
    void loop();
    uint8_t getCard(uint8_t *data); // returns data length (0 = no card), data must hold WIEGAND_MAX_DATA_BYTES
    char getKey();                   // returns '\0' if no key pressed
    void setLed(bool green);

  private:
    static void interruptD0();
    static void interruptD1();
    static void addBit(uint8_t bit);
    void processFrame(uint64_t bits, uint8_t bitCount);
    void processCard(uint64_t bits, uint8_t bitCount);
    bool checkParity(uint64_t bits, uint8_t bitCount);
    char mapKey(uint8_t value);

    inline volatile static uint64_t _bits = 0;
    inline volatile static uint8_t _bitCount = 0;
    inline volatile static uint32_t _lastPulse = 0;

    bool _initialized = false;

    uint8_t _card[WIEGAND_MAX_DATA_BYTES] = {};
    uint8_t _cardLength = 0;
    uint8_t _lastCard[WIEGAND_MAX_DATA_BYTES] = {};
    uint8_t _lastCardLength = 0;
    uint32_t _lastCardTimer = 0;

    char _keys[WIEGAND_KEY_BUFFER_SIZE] = {};
    uint8_t _keyHead = 0;
    uint8_t _keyTail = 0;
};

extern Wiegand wiegand;
