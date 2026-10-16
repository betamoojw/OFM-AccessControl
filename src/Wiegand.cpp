#include "Wiegand.h"

Wiegand wiegand;

std::string Wiegand::logPrefix()
{
    return "Wiegand";
}

void Wiegand::init()
{
    // called from NFC and keypad init, so it has to be done just once
    if (_initialized)
        return;

    if (Wiegand_D0 < 0 || Wiegand_D1 < 0)
    {
        logInfoP("No wiegand interface on this hardware.");
        return;
    }

    // switch reader to 34 bit format
    if (Wiegand_W34 >= 0)
    {
        pinMode((pin_size_t)Wiegand_W34, OUTPUT);
        digitalWrite((pin_size_t)Wiegand_W34, WIEGAND_OUTPUT_ACTIVE_ON);
    }

    pinMode((pin_size_t)Wiegand_D0, INPUT_PULLUP);
    pinMode((pin_size_t)Wiegand_D1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt((pin_size_t)Wiegand_D0), Wiegand::interruptD0, FALLING);
    attachInterrupt(digitalPinToInterrupt((pin_size_t)Wiegand_D1), Wiegand::interruptD1, FALLING);

    if (Wiegand_LED >= 0)
        pinMode((pin_size_t)Wiegand_LED, OUTPUT);
    _initialized = true;
    setLed(false);
    logInfoP("Initialized Wiegand reader.");
}

void Wiegand::interruptD0()
{
    addBit(0);
}

void Wiegand::interruptD1()
{
    addBit(1);
}

void Wiegand::addBit(uint8_t bit)
{
    // bits exceeding the buffer are just counted, frame will be discarded
    if (_bitCount < WIEGAND_MAX_BITS)
        _bits = (_bits << 1) | bit;
    if (_bitCount < 255)
        _bitCount++;
    _lastPulse = millis();
}

void Wiegand::loop()
{
    if (!_initialized || _bitCount == 0)
        return;

    noInterrupts();
    if (millis() - _lastPulse < WIEGAND_FRAME_TIMEOUT)
    {
        // frame still in progress
        interrupts();
        return;
    }

    uint64_t bits = _bits;
    uint8_t bitCount = _bitCount;
    _bits = 0;
    _bitCount = 0;
    interrupts();

    processFrame(bits, bitCount);
}

void Wiegand::processFrame(uint64_t bits, uint8_t bitCount)
{
    logDebugP("Frame received (bits=%u)", bitCount);

    if (bitCount == 4)
    {
        char key = mapKey(bits & 0x0F);
        if (key != '\0')
            _keys[_keyHead] = key;
        else
            return;
    }
    else if (bitCount == 8)
    {
        uint8_t value = bits & 0x0F;
        if (((bits >> 4) & 0x0F) != (~value & 0x0F))
        {
            logInfoP("Invalid 8 bit key frame: 0x%02X", (uint8_t)bits);
            return;
        }
        char key = mapKey(value);
        if (key != '\0')
            _keys[_keyHead] = key;
        else
            return;
    }
    else if (bitCount >= 26 && bitCount <= WIEGAND_MAX_BITS)
    {
        processCard(bits, bitCount);
        return;
    }
    else
    {
        logInfoP("Unsupported frame length: %u bits", bitCount);
        return;
    }

    // key was stored in buffer, oldest key is dropped on overflow
    _keyHead = (_keyHead + 1) % WIEGAND_KEY_BUFFER_SIZE;
    if (_keyHead == _keyTail)
        _keyTail = (_keyTail + 1) % WIEGAND_KEY_BUFFER_SIZE;
}

void Wiegand::processCard(uint64_t bits, uint8_t bitCount)
{
    if ((bitCount == 26 || bitCount == 34) && !checkParity(bits, bitCount))
    {
        logInfoP("Parity error in %u bit card frame", bitCount);
        return;
    }

    // remove parity bits and store big endian
    uint8_t dataBits = bitCount - 2;
    uint64_t data = (bits >> 1) & ((1ULL << dataBits) - 1);
    uint8_t dataLength = (dataBits + 7) / 8;

    uint8_t card[WIEGAND_MAX_DATA_BYTES] = {};
    for (uint8_t i = 0; i < dataLength; i++)
        card[i] = (data >> ((dataLength - 1 - i) * 8)) & 0xFF;

    // reader may repeat the card as long as it is present
    if (_lastCardTimer > 0 && !delayCheck(_lastCardTimer, WIEGAND_CARD_REPEAT_DELAY) &&
        _lastCardLength == dataLength && !memcmp(_lastCard, card, WIEGAND_MAX_DATA_BYTES))
    {
        _lastCardTimer = delayTimerInit();
        logDebugP("Same card read again and ignored");
        return;
    }

    memcpy(_lastCard, card, WIEGAND_MAX_DATA_BYTES);
    _lastCardLength = dataLength;
    _lastCardTimer = delayTimerInit();

    memcpy(_card, card, WIEGAND_MAX_DATA_BYTES);
    _cardLength = dataLength;
}

bool Wiegand::checkParity(uint64_t bits, uint8_t bitCount)
{
    // first bit: even parity over first half, last bit: odd parity over second half
    uint8_t half = bitCount / 2;
    uint8_t evenCount = 0;
    uint8_t oddCount = 0;
    for (uint8_t i = 0; i < bitCount; i++)
    {
        bool bit = (bits >> (bitCount - 1 - i)) & 1;
        if (i < half)
            evenCount += bit;
        else
            oddCount += bit;
    }
    return (evenCount % 2 == 0) && (oddCount % 2 == 1);
}

char Wiegand::mapKey(uint8_t value)
{
    if (value <= 9)
        return '0' + value;
    if (value == 10)
        return '*';
    if (value == 11)
        return '#';

    logInfoP("Unsupported key value: %u", value);
    return '\0';
}

uint8_t Wiegand::getCard(uint8_t *data)
{
    loop();

    uint8_t length = _cardLength;
    if (length > 0)
    {
        memcpy(data, _card, WIEGAND_MAX_DATA_BYTES);
        _cardLength = 0;
    }
    return length;
}

char Wiegand::getKey()
{
    loop();

    if (_keyHead == _keyTail)
        return '\0';

    char key = _keys[_keyTail];
    _keyTail = (_keyTail + 1) % WIEGAND_KEY_BUFFER_SIZE;
    return key;
}

void Wiegand::setLed(bool green)
{
    if (!_initialized || Wiegand_LED < 0)
        return;

    // reader LED is red by default and green while LED line is active
    digitalWrite((pin_size_t)Wiegand_LED, green == (WIEGAND_OUTPUT_ACTIVE_ON == HIGH) ? HIGH : LOW);
}
