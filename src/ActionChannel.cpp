#include "AccessControl.h"

ActionChannel::ActionChannel(uint8_t index, Fingerprint *finger)
{
    _channelIndex = index;
    _finger = finger;
}

const std::string ActionChannel::name()
{
    return "Action";
}

void ActionChannel::loop()
{
    if (_actionCallResetTime > 0 && delayCheck(_actionCallResetTime, ParamACC_AuthDelayTimeMS))
        resetActionCall();

    if (_stairLightTime > 0 && delayCheck(_stairLightTime, ParamACC_ActDelayTimeMS))
    {
        KoACC_ActSwitch.value(false, DPT_Switch);
        _stairLightTime = 0;
    }
    processReadRequests();
}

void ActionChannel::processInputKo(GroupObject &ko)
{
    switch (ACC_KoCalcIndex(ko.asap()))
    {
        case ACC_KoActCallLock:
            if (ParamACC_ActAuthenticate && ko.value(DPT_Switch))
            {
                _authenticateActive = true;
                _actionCallResetTime = delayTimerInit();
                _finger->setLed(Fingerprint::State::WaitForFinger);
            }
            break;
    }
}

bool ActionChannel::processScan(uint16_t location)
{
    // here are 3 cases relevant:
    // authentication is active and the action is without auth-flag => skip processing
    // authentication is inactive and the action has the auth-flag => skip processing
    // authentication is inactive and the action is locked
    if (_authenticateActive != ParamACC_ActAuthenticate ||
        (!ParamACC_ActAuthenticate && KoACC_ActCallLock.value(DPT_Switch)))
        return false;

    // execute only if this ia an action without authentifiaction or it is already authenticated
    if (!ParamACC_ActAuthenticate || KoACC_ActCallLock.value(DPT_Switch))
    {
        switch (ParamACC_ActActionType)
        {
            case 0: // action deactivated
                break;
            case 1: // switch
                KoACC_ActSwitch.value(ParamACC_ActOnOff, DPT_Switch);
                break;
            case 2: // toggle
                KoACC_ActSwitch.value(!KoACC_ActState.value(DPT_Switch), DPT_Switch);
                KoACC_ActState.value(KoACC_ActSwitch.value(DPT_Switch), DPT_Switch);
                break;
            case 3: // stair light
                KoACC_ActSwitch.value(true, DPT_Switch);
                _stairLightTime = delayTimerInit();
                break;
        }

        if (KoACC_ActCallLock.value(DPT_Switch))
        {
            KoACC_ActCallLock.value(false, DPT_Switch);
            _actionCallResetTime = 0;
            _authenticateActive = false;
        }

        return true;
    }

    return false;
}

void ActionChannel::processReadRequests()
{
    // we send a read request just once per channel
    if (_readRequestSent) return;

    // is there a state field to read?
    if (ParamACC_ActActionType == 2) // toggle
        _readRequestSent = openknxAccessControl.sendReadRequest(KoACC_ActState);
    else
        _readRequestSent = true;
}

void ActionChannel::resetActionCall()
{
    _actionCallResetTime = 0;
    if (!_authenticateActive)
        return;

    KoACC_ActCallLock.value(false, DPT_Switch);
    _finger->setLed(Fingerprint::State::None);
    _authenticateActive = false;
}