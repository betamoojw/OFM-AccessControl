#include "KeypadWiegand.h"

void KeypadWiegand::init(bool testMode)
{
    wiegand.init();
    KeypadBase::init(testMode);
    _initialized = true;
}

void KeypadWiegand::loop(bool testMode)
{
    if (!_initialized)
        return;

    // first call base (necessary for correct effect evaluation)
    KeypadBase::loop(testMode);

    // wiegand reader sends just key press, so key up is sent directly afterwards
    char key;
    while ((key = wiegand.getKey()) != '\0')
    {
        if (_callback)
        {
            _callback(key);
            _callback('\0');
        }
    }
}

// wiegand reader has no backlight
bool KeypadWiegand::getBacklight()
{
    return true;
}

void KeypadWiegand::setInfoLed(uint32_t ledColor)
{
    // reader has just a red/green LED: red stays red, any other color is shown as green
    uint8_t red = (ledColor >> 16) & 0xFF;
    uint8_t green = (ledColor >> 8) & 0xFF;
    uint8_t blue = ledColor & 0xFF;
    wiegand.setLed(ledColor != 0 && (red <= green || red <= blue));
}
