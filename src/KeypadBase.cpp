#include <utility>
#include "KeypadBase.h"
#include "Feedback.h"

KeypadBase::KeypadBase(){}

std::string KeypadBase::logPrefix()
{
    return "Keypad";
}

void KeypadBase::registerCallback(KeyPressedCallback callback)
{
    _callback = std::move(callback);
}

void KeypadBase::runTestMode() {}

void KeypadBase::init(bool testMode) 
{
    if (ParamACC_BacklightState == PT_BacklightState::alwaysOn)
    {
        setBacklight(true);
        _backlightTimer = 0; // backlight will not be turned off
    }
    else
        setBacklight(false);
}

void KeypadBase::loop(bool testMode) 
{
    // handle feedback led effects
    if (_ledTimer > 0 && delayCheck(_ledTimer, _ledDuration))
        setFeedback(FeedbackType::Off);
    else if (_ledEffect == LedEffect::Blink)
    {
        uint32_t durationDelta = millis() - _ledTimer;
        // pusle - pause blinking
        uint32_t durationPart = durationDelta % (_ledPulse + _ledPause); 
        if ((durationPart <= _ledPulse) != _ledBlinkState)
        {
            _ledBlinkState = !_ledBlinkState;
            if (_ledBlinkState)
                setInfoLed(_ledOnColor);
            else
                setInfoLed(_ledOffColor);
        }
    }

    // handle backlight led
    if (_backlightTimer > 0 && ParamACC_BacklightDelayTimeMS > 0 && delayCheck(_backlightTimer, ParamACC_BacklightDelayTimeMS))
        setBacklight(false);
}

void KeypadBase::setFeedback(FeedbackType feedbackType)
{
    // provide optical feedback if enabled
    if (ParamACC_FeedbackLed & 1) 
    {
        switch (feedbackType)
        {
            case FeedbackType::Kepress:
                setInfoLed(0x0000FF); // blue
                _ledTimer = delayTimerInit();
                _ledDuration = 200;
                break;
            case FeedbackType::Ok:
            case FeedbackType::ActionOk:
                setInfoLed(0x00FF00); // green
                _ledTimer = delayTimerInit();
                _ledDuration = 1000;
                break;
            case FeedbackType::ActionNotFound:
                setInfoLed(0xBB2200); // yellow
                _ledTimer = delayTimerInit();
                _ledDuration = 1000;
                break;
            case FeedbackType::Failed:
            case FeedbackType::CodeUnknown:
            case FeedbackType::PauseExceeded:
                setInfoLed(0xFF0000); // red
                _ledTimer = delayTimerInit();
                _ledDuration = 1000;
                break;
            case FeedbackType::CodeDeleted:
                _ledTimer = delayTimerInit();
                _ledDuration = 1000;
                _ledEffect = LedEffect::Blink;
                _ledOnColor = 0xFF0000;
                // _ledOffColor = 0x00FF00;
                _ledPulse = 50;
                _ledPause = 150;
                setInfoLed(_ledOnColor); // blue
                break;
            case FeedbackType::WaitForCode:
                _ledTimer = 0;
                _ledEffect = LedEffect::Blink;
                _ledOnColor = 0x0000FF;
                // _ledOffColor = 0x00FF00;
                _ledPulse = 50;
                _ledPause = 150;
                setInfoLed(_ledOnColor); // blue
                break;
            case FeedbackType::ButtonPress:
                setInfoLed(0xFFFFFF); // white
                _ledTimer = 0;
                _ledDuration = 0;
                break;
            default:
                setInfoLed(0x000000);
                _ledTimer = 0;
                _ledDuration = 0;
                _ledEffect = LedEffect::NoEffect;
                _ledBlinkState = false;
                _ledOnColor = 0;
                _ledOffColor = 0;
                _ledPulse = 0;
                _ledPause = 0;
                break;
        }
    }

    
    if (ParamACC_FeedbackBuzzer & 1)
    {
        switch (feedbackType)
        {
            case FeedbackType::ButtonPress:
            case FeedbackType::Kepress:
                // signal feedback for the deteceted keypress "beep"
                openknxFeedback.setBuzzer(true, 100UL);
                break;
            case FeedbackType::Failed:
            case FeedbackType::CodeUnknown:
            case FeedbackType::CodeDeleted:
            case FeedbackType::PauseExceeded:
            case FeedbackType::ActionNotFound:
                // signal error state: "beep", "beep", "beep", "beep"
                openknxFeedback.setBuzzer({{1500, 200, 200}, {1500, 200, 200}, {1500, 200, 200}, {1500, 200, 200}});
                break;
            case FeedbackType::Ok:
            case FeedbackType::ActionOk:
                // signal success "beeeeeeeeeeeep"
                openknxFeedback.setBuzzer(true, 1000UL);
                break;
            case FeedbackType::WaitForCode:
            case FeedbackType::Off:
                // do Nothing
                break;
        }
    }
}

void KeypadBase::setBacklight(bool on) {

    uint8_t intensity = 0;
    _backlight = on;
    if (on) 
    {
        switch (ParamACC_BacklightIntensity)
        {
            case PT_BacklightIntensity::high:
                intensity = 0xFF;
                break;
            case PT_BacklightIntensity::medium:
                intensity = 0x7F;
                break;
            case PT_BacklightIntensity::low:
                intensity = 0x3F;
                break;
            case PT_BacklightIntensity::byKO:
                intensity = KoACC_KeypadBacklight.value(DPT_DecimalFactor);
                break;            
            default:
                intensity = 0x00;
                break;
        }
        _backlightTimer = delayTimerInit();
    }
    else
        _backlightTimer = 0;
    
    setBackgroundLed(intensity);
}

// keypads without backlight support should 
// overrride this and ALWAYS return true!!!
bool KeypadBase::getBacklight()
{
    return _backlight;
}