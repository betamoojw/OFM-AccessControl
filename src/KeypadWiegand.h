#pragma once

#include "KeypadBase.h"
#include "Wiegand.h"

class KeypadWiegand : public KeypadBase
{
  public:
    KeypadWiegand() {};
    void init(bool testMode = false) override;
    void loop(bool testMode = false) override;
    bool getBacklight() override;
    void setInfoLed(uint32_t ledColor) override;
    void setBackgroundLed(uint8_t brightness) override {};
};
