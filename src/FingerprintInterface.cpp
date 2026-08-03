#include "FingerprintInterface.h"

#include <cstring>

#define FP_BAUD 57600
#define FP_FIFO_SIZE 1024
#define FP_RX_BUDGET_BYTES 96
#define FP_TX_BUDGET_BYTES 64
#define FP_CMD_RETRIES 2
#define FP_RETRY_GAP_MS 20
#define FP_POST_DATA_GUARD_MS 200
#define FP_INTER_PACKET_MS 200
#define FP_POWER_OFF_DRAIN_MS 50
#define FP_POWER_STABILIZE_MS 10
#define FP_BOOT_WAIT_MS 500
#define FP_VFYPWD_GAP_MS 200
#define FP_VFYPWD_ATTEMPTS 3
#define FP_MULTIACK_INTERACK_MS 20000
#define FP_AUTOENROLL_TOTAL_MS 90000
#define FP_AUTOIDENTIFY_TOTAL_MS 30000
#define FP_FINGER_POLL_MS 75
#define FP_FINGER_WAIT_MS 10000
#define FP_REMOVE_SETTLE_MS 500
#define FP_MODEL_LED_MS 1000

#ifdef SCANNER_PWR_PIN
    #define FP_PWR_ON (SCANNER_PWR_PIN_ACTIVE_ON == HIGH ? HIGH : LOW)
    #define FP_PWR_OFF (SCANNER_PWR_PIN_ACTIVE_ON == HIGH ? LOW : HIGH)
#endif

namespace
{
    enum SimpleStep : uint8_t
    {
        SimpleSend = 0,
        SimpleAck
    };

    // operations that show the busy LED first need one extra step, otherwise the LED latch
    // would never reach the wire (it is only flushed while no command is pending)
    enum LatchedStep : uint8_t
    {
        LatchedLed = 0,
        LatchedSend,
        LatchedAck
    };

    enum HealthStep : uint8_t
    {
        HealthVerify = 0,
        HealthVerifyAck,
        HealthProbe,
        HealthProbeAck
    };

    enum SearchStep : uint8_t
    {
        SearchCapture = 0,
        SearchCaptureAck,
        SearchFeature,
        SearchFeatureAck,
        SearchLookup,
        SearchLookupAck
    };

    enum EnrollStep : uint8_t
    {
        EnrollStart = 0,
        EnrollCapture,
        EnrollCaptureAck,
        EnrollFeature,
        EnrollFeatureAck,
        EnrollRemoveSettle,
        EnrollRemovePoll,
        EnrollRemoveAck,
        EnrollModelWait,
        EnrollModel,
        EnrollModelAck,
        EnrollStore,
        EnrollStoreAck
    };

    enum TemplateStep : uint8_t
    {
        TemplateFirst = 0,
        TemplateFirstAck,
        TemplateSecond,
        TemplateSecondAck
    };

    enum IndexStep : uint8_t
    {
        IndexCount = 0,
        IndexCountAck,
        IndexRead,
        IndexReadAck
    };
} // namespace

std::string FingerprintInterface::logPrefix()
{
    // deliberately not the class name: this prefix appears in front of every scanner log line and is
    // the one the application documentation refers to
    return "Fingerprint";
}

// ---------------------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------------------

void FingerprintInterface::init()
{
#ifdef SCANNER_PWR_PIN
    pinMode(SCANNER_PWR_PIN, OUTPUT);
#endif
}

void FingerprintInterface::setPassword(uint32_t password)
{
    _password = password;
}

void FingerprintInterface::setModel(FpModel model)
{
    _model = model;
}

bool FingerprintInterface::powerOn()
{
    if (_poweredOn)
    {
        // a failed bring-up is retried without power cycling the module
        if (_bringUp == BringUp::Fault)
        {
            logInfoP("Retrying bring-up");
            flushReceive();
            resetDataBindings();
            // dropped exactly like the cold path below does it: the latch is only flushed once the
            // bring-up reached Ready, so a state which was set while the scanner was down (an LED
            // group object, a "not found" answer) would otherwise surface as the first thing a later
            // successful bring-up shows
            _ledPending = false;
            _ledInFlight = false;
            _bootByteSeen = false;
            _bringUpRetries = 0;
            _bringUp = BringUp::BootWait;
            _bringUpTimer = delayTimerInit();
            _op = Op::BringUp;
            _opStep = 0;
        }
        return true;
    }

    logInfoP("Power on scanner (model %s, template %u bytes, library %u)",
             fpModelText(_model), templateSize(), libraryCapacity());

#ifdef SCANNER_PWR_PIN
    pinMode(SCANNER_PWR_PIN, OUTPUT);
    digitalWrite(SCANNER_PWR_PIN, FP_PWR_ON);
#endif

    // 1 KB software FIFO buys about 178 ms of line rate slack against loop stalls
    SCANNER_SERIAL.setFIFOSize(FP_FIFO_SIZE);
    SCANNER_SERIAL.setRX(SCANNER_SERIAL_RX_PIN);
    SCANNER_SERIAL.setTX(SCANNER_SERIAL_TX_PIN);
    SCANNER_SERIAL.begin(FP_BAUD);

    _link.setAddress(FpProto::DefaultAddress);
    _link.reset();

    resetDataBindings();
    _poweredOn = true;
    _powerOffPending = false;
    _bootByteSeen = false;
    _sensorMaybeRebooted = false;
    _phase = Phase::Idle;
    _retryPending = false;
    _cmdDone = false;
    _ledPending = false;
    _ledInFlight = false;
    _txLength = 0;
    _txSent = 0;
    _bringUpRetries = 0;
    _bringUp = BringUp::PowerStabilize;
    _bringUpTimer = delayTimerInit();
    _op = Op::BringUp;
    _opStep = 0;
    return true;
}

void FingerprintInterface::powerOff()
{
    if (!_poweredOn)
        return;

    // an active operation is aborted from loop() so that its callback never fires re-entrantly;
    // pending transport work is deferred as well so that serial.end() cannot cut a frame mid TX
    if (_op != Op::None || _phase != Phase::Idle || _retryPending || _ledInFlight)
    {
        _powerOffPending = true;
        _powerOffTimer = delayTimerInit();
        return;
    }

    finishPowerOff();
}

void FingerprintInterface::finishPowerOff()
{
    // a bring-up that never reached Ready has to report the failure, somebody may wait for it
    bool bringUpAborted = _op == Op::BringUp;

    SCANNER_SERIAL.end();
    // release the TX line so the module cannot be back-powered through it
    pinMode(SCANNER_SERIAL_TX_PIN, INPUT);

#ifdef SCANNER_PWR_PIN
    digitalWrite(SCANNER_PWR_PIN, FP_PWR_OFF);
#endif

    _poweredOn = false;
    _powerOffPending = false;
    _bringUp = BringUp::Off;
    _phase = Phase::Idle;
    _retryPending = false;
    _cmdDone = false;
    _ledPending = false;
    _ledInFlight = false;
    _op = Op::None;
    _opStep = 0;
    _txLength = 0;
    _txSent = 0;
    resetDataBindings();
    logInfoP("Scanner powered off");

    if (bringUpAborted && _readyCallback != nullptr)
        _readyCallback(false);
}

bool FingerprintInterface::isBusy() const
{
    return _op != Op::None || _phase != Phase::Idle || _retryPending;
}

void FingerprintInterface::cancelOperation()
{
    if (_op == Op::None)
        return;

    // a raw command that already went to the wire cannot be recalled, the module answers it in
    // any case; for composite operations the request takes effect at the next step boundary
    _cancelPending = true;
}

void FingerprintInterface::loop()
{
    if (!_poweredOn)
        return;

    if (_powerOffPending)
    {
        // a frame that is still shifting out is drained first (bounded), afterwards whatever is
        // left of the operation is aborted right away
        processTransport();
        if (_txSent < _txLength && !delayCheck(_powerOffTimer, FP_POWER_OFF_DRAIN_MS))
            return;

        if (_op != Op::None && _op != Op::BringUp)
            finishOperation(FpStatus::ErrPoweredOff);
        finishPowerOff();
        return;
    }

    processTransport();
    processCommand();
    processOperation();
    flushLedLatch();
}

// ---------------------------------------------------------------------------------------
// transport
// ---------------------------------------------------------------------------------------

void FingerprintInterface::processTransport()
{
    // SerialUART::write() busy-waits once the 32 byte hardware FIFO is full, so only push
    // bytes while the UART reports free space
    uint8_t txBudget = FP_TX_BUDGET_BYTES;
    while (_txSent < _txLength && txBudget > 0 && SCANNER_SERIAL.availableForWrite() > 0)
    {
        SCANNER_SERIAL.write(_txBuf[_txSent++]);
        txBudget--;
    }

    uint16_t rxBudget = FP_RX_BUDGET_BYTES;
    while (rxBudget > 0 && SCANNER_SERIAL.available())
    {
        rxBudget--;
        FingerprintLink::Event event = _link.feed((uint8_t)SCANNER_SERIAL.read(), millis());
        if (event != FingerprintLink::Event::None)
            handleLinkEvent(event);
    }

    // only a silent line is a real inter byte gap, a drained backlog is not
    if (!SCANNER_SERIAL.available() && _link.checkInterByteTimeout(millis()))
    {
        if (_phase == Phase::WaitAck || _phase == Phase::RxData || _phase == Phase::WaitMoreAcks)
            retryOrFail(FpStatus::ErrInterByte);
    }
}

void FingerprintInterface::flushReceive()
{
    // bounded by one full software FIFO plus the hardware FIFO so a babbling line can never
    // park the loop in here
    uint16_t budget = FP_FIFO_SIZE + 32;
    while (budget > 0 && SCANNER_SERIAL.available())
    {
        SCANNER_SERIAL.read();
        budget--;
    }
    _link.reset();
}

// the data phase bindings point into caller owned memory, so they must never survive the
// operation they belong to
void FingerprintInterface::resetDataBindings()
{
    _dataDst = nullptr;
    _dataDstSize = 0;
    _dataReceived = 0;
    _dataSrc = nullptr;
    _dataSrcLength = 0;
    _dataSrcSent = 0;
    _dataSink = nullptr;
    _dataSource = nullptr;
    _dataOverflow = false;
}

void FingerprintInterface::handleLinkEvent(FingerprintLink::Event event)
{
    switch (event)
    {
        case FingerprintLink::Event::FrameAck:
            onAckFrame();
            break;
        case FingerprintLink::Event::FrameData:
            onDataFrame(false);
            break;
        case FingerprintLink::Event::FrameDataEnd:
            onDataFrame(true);
            break;
        case FingerprintLink::Event::BootByte:
            if (_bringUp == BringUp::BootWait)
                _bootByteSeen = true;
            else if (_bringUp == BringUp::Ready)
            {
                _sensorMaybeRebooted = true;
                logDebugP("Sensor announced a reboot");
            }
            break;
        case FingerprintLink::Event::ErrChecksum:
            if (_phase == Phase::WaitAck || _phase == Phase::RxData || _phase == Phase::WaitMoreAcks)
                retryOrFail(FpStatus::ErrChecksum);
            break;
        case FingerprintLink::Event::ErrAddress:
            if (_phase == Phase::WaitAck || _phase == Phase::RxData || _phase == Phase::WaitMoreAcks)
                retryOrFail(FpStatus::ErrAddress);
            break;
        case FingerprintLink::Event::ErrFraming:
            if (_phase == Phase::WaitAck || _phase == Phase::RxData || _phase == Phase::WaitMoreAcks)
                retryOrFail(FpStatus::ErrFraming);
            break;
        default:
            break;
    }
}

void FingerprintInterface::onAckFrame()
{
    if (_phase != Phase::WaitAck && _phase != Phase::WaitMoreAcks)
        return; // unsolicited or late acknowledge

    if (_link.payloadLength() < 1)
    {
        retryOrFail(FpStatus::ErrFraming);
        return;
    }

    uint8_t confirmation = _link.payload()[0];
    _cmdResult.rawConfirmation = confirmation;
    _cmdResult.status = (FpStatus)confirmation;
    parseAckPayload();

    // the module only demands a password again after it lost its session, so it must have
    // restarted; the next health check re-verifies before it probes the sensor
    if (confirmation == (uint8_t)FpStatus::PasswordRequired)
    {
        _sensorMaybeRebooted = true;
        logDebugP("Sensor demands password verification, session was lost");
    }

    // GenImg polling would otherwise flood the debug log with "no finger"
    if (isTraced(_cmd))
    {
        logDebugP("<- %s ack 0x%02X (%s)", fpCommandText(_cmd), confirmation, fpStatusText((FpStatus)confirmation));
        logHexDebugP(_link.payload(), _link.payloadLength());
    }
    else if (confirmation != 0x00 && confirmation != (uint8_t)FpStatus::NoFinger)
        logDebugP("<- %s ack 0x%02X (%s)", fpCommandText(_cmd), confirmation, fpStatusText((FpStatus)confirmation));

    if (_cancelSent)
    {
        completeCommand(FpStatus::ErrCancelled);
        return;
    }

    if (confirmation != 0x00)
    {
        completeCommand((FpStatus)confirmation);
        return;
    }

    if (isMultiAckCommand(_cmd))
    {
        _multiAckStepPending = true;
        uint8_t finalStep = _cmd == FpCommand::AutoEnroll ? 0x0F : 0x03;
        if (_multiAckStep >= finalStep)
            completeCommand(FpStatus::Ok);
        else
            _cmdTimer = millis();
        return;
    }

    if (isRxDataCommand(_cmd))
    {
        _phase = Phase::RxData;
        _cmdTimer = millis();
        _cmdTimeout = dataTimeoutFor(_cmd);
        return;
    }

    if (isTxDataCommand(_cmd))
    {
        _phase = Phase::TxData;
        _cmdTimer = millis();
        // sending is paced by the loop rate, not by the line rate, so allow more headroom
        _cmdTimeout = dataTimeoutFor(_cmd) * 3;
        if (_dataSrcLength == 0 || !buildNextTxDataPacket())
            completeCommand(FpStatus::ErrInvalidParam);
        return;
    }

    completeCommand(FpStatus::Ok);
}

void FingerprintInterface::onDataFrame(bool last)
{
    if (_phase != Phase::RxData)
        return;

    const uint8_t *payload = _link.payload();
    uint16_t length = _link.payloadLength();

    if (_dataSink != nullptr)
        _dataSink(payload, length);
    else if (_dataDst != nullptr)
    {
        if ((uint32_t)_dataReceived + length <= _dataDstSize)
            memcpy(_dataDst + _dataReceived, payload, length);
        else
            _dataOverflow = true;
    }

    _dataReceived += length;

    // the whole phase timeout only guards the wait for the first packet, every following one
    // has to arrive within the inter packet window
    _cmdTimer = millis();
    _cmdTimeout = FP_INTER_PACKET_MS;

    if (!last)
        return;

    logDebugP("<- %s data phase complete, %u bytes", fpCommandText(_cmd), _dataReceived);
    _cmdResult.length = _dataReceived;
    completeCommand(_dataOverflow ? FpStatus::ErrBufferTooSmall : FpStatus::Ok);
}

void FingerprintInterface::parseAckPayload()
{
    const uint8_t *p = _link.payload();
    uint16_t length = _link.payloadLength();

    switch (_cmd)
    {
        case FpCommand::Search:
            if (length >= 5)
            {
                _cmdResult.location = ((uint16_t)p[1] << 8) | p[2];
                _cmdResult.score = ((uint16_t)p[3] << 8) | p[4];
            }
            break;
        case FpCommand::Match:
            if (length >= 3)
                _cmdResult.score = ((uint16_t)p[1] << 8) | p[2];
            break;
        case FpCommand::TempleteNum:
            if (length >= 3)
                _cmdResult.length = ((uint16_t)p[1] << 8) | p[2];
            break;
        case FpCommand::ReadSysPara:
            parseSysPara(p, length);
            break;
        case FpCommand::ReadProdInfo:
            parseProdInfo(p, length);
            break;
        case FpCommand::ReadIndexTable:
            if (length >= 1 + FP_NOTEPAD_PAGE_SIZE)
            {
                memcpy(_indexScratch, p + 1, FP_NOTEPAD_PAGE_SIZE);
                if (_dataDst != nullptr && _dataDstSize >= FP_NOTEPAD_PAGE_SIZE)
                    memcpy(_dataDst, p + 1, FP_NOTEPAD_PAGE_SIZE);
            }
            break;
        case FpCommand::ReadNotepad:
            if (length >= 1 + FP_NOTEPAD_PAGE_SIZE && _dataDst != nullptr && _dataDstSize >= FP_NOTEPAD_PAGE_SIZE)
                memcpy(_dataDst, p + 1, FP_NOTEPAD_PAGE_SIZE);
            break;
        case FpCommand::GetAlgVer:
            if (length >= 1 + FP_VERSION_STRING_SIZE)
            {
                memcpy(_algVersion, p + 1, FP_VERSION_STRING_SIZE);
                _algVersion[FP_VERSION_STRING_SIZE] = 0;
                if (_dataDst != nullptr && _dataDstSize >= FP_VERSION_STRING_SIZE)
                    memcpy(_dataDst, p + 1, FP_VERSION_STRING_SIZE);
            }
            break;
        case FpCommand::GetFwVer:
            if (length >= 1 + FP_VERSION_STRING_SIZE)
            {
                memcpy(_fwVersion, p + 1, FP_VERSION_STRING_SIZE);
                _fwVersion[FP_VERSION_STRING_SIZE] = 0;
                if (_dataDst != nullptr && _dataDstSize >= FP_VERSION_STRING_SIZE)
                    memcpy(_dataDst, p + 1, FP_VERSION_STRING_SIZE);
            }
            break;
        case FpCommand::GetRandomCode:
            if (length >= 5 && _dataDst != nullptr && _dataDstSize >= 4)
                memcpy(_dataDst, p + 1, 4);
            break;
        case FpCommand::AutoEnroll:
            // the R503 reports the model id in one byte, the R503Pro in two
            if (_model == FpModel::R503Pro)
            {
                if (length >= 4)
                {
                    _multiAckStep = p[1];
                    _cmdResult.location = ((uint16_t)p[2] << 8) | p[3];
                }
            }
            else if (length >= 3)
            {
                _multiAckStep = p[1];
                _cmdResult.location = p[2];
            }
            break;
        case FpCommand::AutoIdentify:
            if (length >= 6)
            {
                _multiAckStep = p[1];
                _cmdResult.location = ((uint16_t)p[2] << 8) | p[3];
                _cmdResult.score = ((uint16_t)p[4] << 8) | p[5];
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------
// command layer
// ---------------------------------------------------------------------------------------

void FingerprintInterface::stageParams(const uint8_t *params, uint8_t paramLength)
{
    if (paramLength > sizeof(_paramBuf))
        paramLength = sizeof(_paramBuf);

    if (params != nullptr && paramLength > 0 && params != _paramBuf)
        memcpy(_paramBuf, params, paramLength);

    _paramLength = paramLength;
}

bool FingerprintInterface::issueCommand(FpCommand command, const uint8_t *params, uint8_t paramLength)
{
    if (_phase != Phase::Idle || _ledInFlight || _retryPending || _cmdDone)
        return false;

    _stagedCmd = command;
    stageParams(params, paramLength);
    return issueStagedCommand();
}

bool FingerprintInterface::issueStagedCommand()
{
    if (_phase != Phase::Idle || _ledInFlight || _retryPending || _cmdDone)
        return false;

    _cmd = _stagedCmd;
    _cmdRetries = 0;
    return sendStagedCommand();
}

bool FingerprintInterface::sendStagedCommand()
{
    _txLength = _link.buildCommand((uint8_t)_cmd, _paramBuf, _paramLength, _txBuf, sizeof(_txBuf));
    _txSent = 0;

    if (_txLength == 0)
    {
        logErrorP("Cannot build frame for %s", fpCommandText(_cmd));
        completeCommand(FpStatus::ErrInvalidParam);
        return true;
    }

    flushReceive();
    _cmdResult = {};
    _dataReceived = 0;
    _dataSrcSent = 0;
    _dataOverflow = false;
    _multiAckStep = 0;
    _multiAckStepPending = false;
    _cancelSent = false;
    _phase = Phase::TxFrame;

    if (isTraced(_cmd))
    {
        logDebugP("-> %s", fpCommandText(_cmd));
        logHexDebugP(_txBuf, _txLength);
    }
    return true;
}

bool FingerprintInterface::commandTaken()
{
    if (!_cmdDone)
        return false;

    _cmdDone = false;
    return true;
}

void FingerprintInterface::completeCommand(FpStatus status)
{
    _phase = Phase::Idle;
    _retryPending = false;
    _txLength = 0;
    _txSent = 0;
    _cmdResult.status = status;
    if ((uint8_t)status >= 0xE0 && (uint8_t)status <= 0xEF)
        _cmdResult.rawConfirmation = 0;

    if (_ledInFlight)
    {
        _ledInFlight = false;
        return; // LED results are never handed to the operation layer
    }

    _cmdDone = true;
}

void FingerprintInterface::retryOrFail(FpStatus status)
{
    flushReceive();

    if (_cmd == FpCommand::SetAdder)
        _link.setAddress(_previousAddress);

    // a command must never be re-sent while a data phase is already running, only composite
    // operations may repeat a whole chain
    if (!_ledInFlight && _phase != Phase::TxData && _phase != Phase::RxData &&
        isRetryable(_cmd) && _cmdRetries < FP_CMD_RETRIES)
    {
        _cmdRetries++;
        logDebugP("%s: %s, retry %u", fpCommandText(_cmd), fpStatusText(status), _cmdRetries);
        _phase = Phase::Idle;
        _retryPending = true;
        _cmdTimer = millis();
        return;
    }

    logDebugP("%s failed: %s", fpCommandText(_cmd), fpStatusText(status));
    completeCommand(status);
}

uint16_t FingerprintInterface::packetSize() const
{
    return _sysPara.packetSize > 0 ? _sysPara.packetSize : 128;
}

bool FingerprintInterface::buildNextTxDataPacket()
{
    uint16_t chunk = packetSize();
    if (chunk > FpProto::MaxPayload)
        chunk = FpProto::MaxPayload;

    uint8_t payload[FpProto::MaxPayload];
    uint16_t length = 0;
    uint32_t remaining = _dataSrcLength - _dataSrcSent;
    uint16_t want = remaining < chunk ? (uint16_t)remaining : chunk;

    if (_dataSource != nullptr)
    {
        length = _dataSource(payload, want);
        if (length == 0 || length > want)
            return false;
    }
    else if (_dataSrc != nullptr)
    {
        length = want;
        memcpy(payload, _dataSrc + _dataSrcSent, length);
    }
    else
        return false;

    _dataSrcSent += length;
    bool last = _dataSrcSent >= _dataSrcLength;
    _txLength = _link.buildFrame(last ? FpProto::PidDataEnd : FpProto::PidData, payload, length, _txBuf, sizeof(_txBuf));
    _txSent = 0;
    return _txLength > 0;
}

void FingerprintInterface::processCommand()
{
    if (_retryPending)
    {
        if (delayCheck(_cmdTimer, FP_RETRY_GAP_MS))
        {
            _retryPending = false;
            sendStagedCommand();
        }
        return;
    }

    switch (_phase)
    {
        case Phase::Idle:
            return;

        case Phase::TxFrame:
            if (_txSent < _txLength)
                return;

            if (_cmd == FpCommand::SetAdder)
            {
                // the acknowledge of SetAdder already arrives under the new address
                _previousAddress = _link.address();
                _link.setAddress(_pendingAddress);
            }

            _cmdTimer = millis();
            if (isMultiAckCommand(_cmd))
            {
                _phase = Phase::WaitMoreAcks;
                _multiAckStart = _cmdTimer;
                _cmdTimeout = FP_MULTIACK_INTERACK_MS;
            }
            else
            {
                _phase = Phase::WaitAck;
                _cmdTimeout = ackTimeoutFor(_cmd);
            }
            return;

        case Phase::WaitAck:
            if (delayCheck(_cmdTimer, _cmdTimeout))
                retryOrFail(FpStatus::ErrTimeoutAck);
            return;

        case Phase::WaitMoreAcks:
        {
            if (_cancelPending && !_cancelSent && _txSent >= _txLength)
            {
                _txLength = _link.buildCommand((uint8_t)FpCommand::Cancel, nullptr, 0, _txBuf, sizeof(_txBuf));
                _txSent = 0;
                _cancelSent = true;
                logDebugP("-> Cancel");
                return;
            }

            uint32_t overall = _cmd == FpCommand::AutoEnroll ? FP_AUTOENROLL_TOTAL_MS : FP_AUTOIDENTIFY_TOTAL_MS;
            if (delayCheck(_multiAckStart, overall) || delayCheck(_cmdTimer, _cmdTimeout))
                retryOrFail(FpStatus::ErrTimeoutAck);
            return;
        }

        case Phase::RxData:
            if (delayCheck(_cmdTimer, _cmdTimeout))
                retryOrFail(FpStatus::ErrTimeoutData);
            return;

        case Phase::TxData:
            if (delayCheck(_cmdTimer, _cmdTimeout))
            {
                // a stalled UART must not park the driver in the data phase forever
                completeCommand(FpStatus::ErrTimeoutData);
                return;
            }

            if (_txSent < _txLength)
                return;

            if (_dataSrcSent >= _dataSrcLength)
            {
                _cmdResult.length = (uint16_t)_dataSrcSent;
                _phase = Phase::PostDataGuard;
                _cmdTimer = millis();
                return;
            }

            if (!buildNextTxDataPacket())
                completeCommand(FpStatus::ErrInvalidParam);
            return;

        case Phase::PostDataGuard:
            // the module needs a short settle time after a data phase before it accepts the Store
            // which follows it
            if (delayCheck(_cmdTimer, FP_POST_DATA_GUARD_MS))
            {
                logDebugP("-> %s data phase complete, %u bytes", fpCommandText(_cmd), (uint16_t)_dataSrcSent);
                completeCommand(FpStatus::Ok);
            }
            return;
    }
}

uint32_t FingerprintInterface::ackTimeoutFor(FpCommand command) const
{
    switch (command)
    {
        case FpCommand::VfyPwd:
        case FpCommand::HandShake:
            // while the bring-up probes for a sensor, a missing answer is a normal outcome and
            // must not stall the start-up; once the sensor is known to be there these two use
            // the default budget like every other quick command
            if (_bringUp != BringUp::Ready)
                return 500;
            return 2000;
        case FpCommand::Store:
        case FpCommand::DeletChar:
        case FpCommand::WriteNotepad:
        case FpCommand::SetSysPara:
        case FpCommand::SetPwd:
        case FpCommand::SetAdder:
        case FpCommand::CheckSensor:
            return 2000;
        case FpCommand::RegModel:
            // creating the model out of six feature sets takes by far the longest of all commands
            return 10000;
        case FpCommand::Empty:
            // wiping all 1500 slots of an R503Pro is the worst case here
            return 10000;
        case FpCommand::Search:
            return 5000;
        default:
            // the data sheet promises "less than 500 ms" for the feature extraction without any
            // margin, and a freshly placed finger regularly needs longer than that; a tight
            // value buys nothing (a timeout only ever means something is really wrong) but it
            // does cost a retransmission while the sensor is still computing, so all quick
            // commands share this generous budget
            return 2000;
    }
}

uint32_t FingerprintInterface::dataTimeoutFor(FpCommand command) const
{
    switch (command)
    {
        case FpCommand::UpChar:
        case FpCommand::DownChar:
            return _model == FpModel::R503Pro ? 1000 : 1500;
        case FpCommand::UpImage:
        case FpCommand::DownImage:
            return 15000;
        default:
            return 1500;
    }
}

bool FingerprintInterface::isRetryable(FpCommand command)
{
    switch (command)
    {
        case FpCommand::SetAdder:
        case FpCommand::SoftRst:
        case FpCommand::Cancel:
        case FpCommand::GetRandomCode:
        case FpCommand::DownChar:
        case FpCommand::DownImage:
        case FpCommand::AutoEnroll:
        case FpCommand::AutoIdentify:
            return false;
        default:
            return true;
    }
}

bool FingerprintInterface::isRxDataCommand(FpCommand command)
{
    return command == FpCommand::UpChar || command == FpCommand::UpImage || command == FpCommand::ReadInfPage;
}

bool FingerprintInterface::isTxDataCommand(FpCommand command)
{
    return command == FpCommand::DownChar || command == FpCommand::DownImage;
}

bool FingerprintInterface::isMultiAckCommand(FpCommand command)
{
    return command == FpCommand::AutoEnroll || command == FpCommand::AutoIdentify;
}

bool FingerprintInterface::isTraced(FpCommand command)
{
    // polled and fire-and-forget commands stay out of the frame level trace
    switch (command)
    {
        case FpCommand::GenImg:
        case FpCommand::GetImageEx:
        case FpCommand::AuraLedConfig:
            return false;
        default:
            return true;
    }
}

// ---------------------------------------------------------------------------------------
// LED latch
// ---------------------------------------------------------------------------------------

void FingerprintInterface::ledParams(LedState state, uint8_t &control, uint8_t &speed, uint8_t &color, uint8_t &count) const
{
    control = (uint8_t)FpLedControl::On;
    speed = 0;
    color = (uint8_t)FpLedColor::White;
    count = 0;

    switch (state)
    {
        case None:
            control = (uint8_t)FpLedControl::Off;
            break;
        case ScanFinger:
            color = (uint8_t)FpLedColor::Blue;
            break;
        case ScanMatch:
        case Success:
            color = (uint8_t)FpLedColor::Green;
            break;
        case ScanMatchNoAction:
        case DeleteNotFound:
            color = (uint8_t)FpLedColor::Yellow;
            break;
        case ScanNoMatch:
        case Failed:
        case Locked:
            color = (uint8_t)FpLedColor::Red;
            break;
        case WaitForFinger:
            control = (uint8_t)FpLedControl::Flashing;
            speed = 20;
            color = (uint8_t)FpLedColor::Blue;
            break;
        case RemoveFinger:
            control = (uint8_t)FpLedControl::Flashing;
            speed = 20;
            break;
        case EnrollCreateModel:
        case Busy:
            control = (uint8_t)FpLedControl::Flashing;
            speed = 20;
            color = (uint8_t)FpLedColor::Green;
            break;
        default:
            break;
    }
}

void FingerprintInterface::setLed(LedState state)
{
    _ledState = state;
    ledParams(state, _ledControl, _ledSpeed, _ledColor, _ledCount);
    _ledPending = true;
}

void FingerprintInterface::setLedRaw(uint8_t control, uint8_t speed, uint8_t color, uint8_t count)
{
    if (!supportsLedColor(color))
    {
        logDebugP("LED color 0x%02X is not supported by %s, ignored", color, fpModelText(_model));
        return;
    }

    _ledControl = control;
    _ledSpeed = speed;
    _ledColor = color;
    _ledCount = count;
    _ledPending = true;
}

void FingerprintInterface::flushLedLatch()
{
    if (!_ledPending || _bringUp != BringUp::Ready)
        return;
    if (_phase != Phase::Idle || _retryPending || _cmdDone || _ledInFlight)
        return;

    // built directly so the latch never touches the staged parameters of an operation
    uint8_t params[4] = {_ledControl, _ledSpeed, _ledColor, _ledCount};
    _txLength = _link.buildCommand((uint8_t)FpCommand::AuraLedConfig, params, sizeof(params), _txBuf, sizeof(_txBuf));
    _txSent = 0;
    if (_txLength == 0)
        return;

    flushReceive();
    _cmd = FpCommand::AuraLedConfig;
    _cmdRetries = FP_CMD_RETRIES; // the LED is never retried
    _cmdResult = {};
    _ledInFlight = true;
    _ledPending = false;
    _phase = Phase::TxFrame;
}

// ---------------------------------------------------------------------------------------
// model profile and capabilities
// ---------------------------------------------------------------------------------------

uint16_t FingerprintInterface::templateSize() const
{
    return _model == FpModel::R503Pro ? 512 : 1536;
}

uint16_t FingerprintInterface::libraryCapacity() const
{
    return _model == FpModel::R503Pro ? 1500 : 200;
}

bool FingerprintInterface::supportsCommand(FpCommand command) const
{
    switch (command)
    {
        case FpCommand::GetImageEx:
        case FpCommand::CheckSensor:
        case FpCommand::ReadProdInfo:
        case FpCommand::SoftRst:
            return _model != FpModel::R503Pro;
        default:
            return true;
    }
}

bool FingerprintInterface::supportsLedColor(uint8_t color) const
{
    if (color >= (uint8_t)FpLedColor::Red && color <= (uint8_t)FpLedColor::White)
        return true;

    return _model == FpModel::R503Pro &&
           (color == (uint8_t)FpLedColor::Rgb3Color || color == (uint8_t)FpLedColor::Rgb7Color);
}

void FingerprintInterface::parseSysPara(const uint8_t *payload, uint16_t length)
{
    if (length < 17)
        return;

    _sysPara.statusRegister = ((uint16_t)payload[1] << 8) | payload[2];
    _sysPara.systemId = ((uint16_t)payload[3] << 8) | payload[4];
    _sysPara.librarySize = ((uint16_t)payload[5] << 8) | payload[6];
    _sysPara.securityLevel = ((uint16_t)payload[7] << 8) | payload[8];
    _sysPara.deviceAddress = ((uint32_t)payload[9] << 24) | ((uint32_t)payload[10] << 16) |
                             ((uint32_t)payload[11] << 8) | (uint32_t)payload[12];
    _sysPara.packetSizeCode = ((uint16_t)payload[13] << 8) | payload[14];
    _sysPara.packetSize = (uint16_t)(32u << (_sysPara.packetSizeCode & 0x03));
    _sysPara.baudFactor = ((uint16_t)payload[15] << 8) | payload[16];
    _sysPara.baudRate = (uint32_t)_sysPara.baudFactor * 9600;
}

void FingerprintInterface::parseProdInfo(const uint8_t *payload, uint16_t length)
{
    // the manual declares LENGTH 0x0031 while the field table sums to 50 bytes, so parse
    // strictly against the received length
    uint16_t available = length > 0 ? (uint16_t)(length - 1) : 0;
    const uint8_t *p = payload + 1;
    uint16_t offset = 0;
    _prodInfo = {};

    auto copyString = [&](char *destination, uint16_t size) {
        if (offset + size > available)
            return false;
        memcpy(destination, p + offset, size);
        destination[size] = 0;
        offset += size;
        return true;
    };
    auto readWord = [&](uint16_t &destination) {
        if (offset + 2 > available)
            return false;
        destination = ((uint16_t)p[offset] << 8) | p[offset + 1];
        offset += 2;
        return true;
    };

    if (!copyString(_prodInfo.moduleType, 16)) return;
    if (!copyString(_prodInfo.batchNumber, 4)) return;
    if (!copyString(_prodInfo.serialNumber, 8)) return;
    if (offset + 2 > available) return;
    _prodInfo.hardwareVersionMajor = p[offset];
    _prodInfo.hardwareVersionMinor = p[offset + 1];
    offset += 2;
    if (!copyString(_prodInfo.sensorType, 8)) return;
    if (!readWord(_prodInfo.sensorWidth)) return;
    if (!readWord(_prodInfo.sensorHeight)) return;
    if (!readWord(_prodInfo.templateSize)) return;
    if (!readWord(_prodInfo.templateTotal)) return;
    _prodInfo.valid = true;
}

void FingerprintInterface::logSystemParameters()
{
    logInfoP("System parameters:");
    logIndentUp();
    logInfoP("Configured model: %s", fpModelText(_model));
    logInfoP("Status register: %u", _sysPara.statusRegister);
    logInfoP("System identifier code: %u", _sysPara.systemId);
    logInfoP("Finger library size: %u", _sysPara.librarySize);
    logInfoP("Security level: %u", _sysPara.securityLevel);
    logInfoP("Device address: %u", _sysPara.deviceAddress);
    logInfoP("Data packet size: %u", _sysPara.packetSize);
    logInfoP("Baud settings: %u", _sysPara.baudRate);
    logInfoP("Template size (profile): %u", templateSize());
    if (_algVersion[0] != 0)
        logInfoP("Algorithm version: %s", _algVersion);
    if (_fwVersion[0] != 0)
        logInfoP("Firmware version: %s", _fwVersion);
    if (_prodInfo.valid)
    {
        logInfoP("Product: %s (sensor %s, %ux%u)", _prodInfo.moduleType, _prodInfo.sensorType,
                 _prodInfo.sensorWidth, _prodInfo.sensorHeight);
        logInfoP("Batch %s, serial %s, hardware %u.%u", _prodInfo.batchNumber, _prodInfo.serialNumber,
                 _prodInfo.hardwareVersionMajor, _prodInfo.hardwareVersionMinor);
        logInfoP("Reported template size %u, library %u", _prodInfo.templateSize, _prodInfo.templateTotal);
    }
    logInfoP("Stored templates: %u (index valid up to %u)", getTemplateCount(), _indexValidUpTo);
    logIndentDown();
}

// ---------------------------------------------------------------------------------------
// index cache
// ---------------------------------------------------------------------------------------

void FingerprintInterface::clearIndex()
{
    memset(_index, 0, sizeof(_index));
    _indexValidUpTo = 0;
}

uint8_t FingerprintInterface::indexPageCount() const
{
    return (uint8_t)((libraryCapacity() + 255) / 256);
}

void FingerprintInterface::markLocation(uint16_t location, bool used)
{
    if (location >= libraryCapacity())
        return;

    uint16_t byteIndex = location >> 3;
    if (byteIndex >= FP_INDEX_BITMAP_SIZE)
        return;

    if (used)
        _index[byteIndex] |= (uint8_t)(1 << (location & 0x07));
    else
        _index[byteIndex] &= (uint8_t)~(1 << (location & 0x07));
}

void FingerprintInterface::applyIndexPage(uint8_t page, const uint8_t *bitmap)
{
    for (uint8_t i = 0; i < FP_NOTEPAD_PAGE_SIZE; i++)
    {
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (bitmap[i] & (1 << bit))
                markLocation((uint16_t)page * 256 + (uint16_t)i * 8 + bit, true);
        }
    }
}

uint16_t FingerprintInterface::getTemplateCount() const
{
    return isReady() ? _templateCount : 0;
}

bool FingerprintInterface::hasLocation(uint16_t location) const
{
    if (!isReady() || location >= libraryCapacity())
        return false;

    uint16_t byteIndex = location >> 3;
    if (byteIndex >= FP_INDEX_BITMAP_SIZE)
        return false;

    return (_index[byteIndex] & (1 << (location & 0x07))) != 0;
}

uint16_t FingerprintInterface::getNextFreeLocation() const
{
    if (!isReady())
        return FP_LOCATION_NONE;

    uint16_t capacity = libraryCapacity();
    for (uint16_t location = 0; location < capacity; location++)
    {
        uint16_t byteIndex = location >> 3;
        if (byteIndex >= FP_INDEX_BITMAP_SIZE)
            break;
        if ((_index[byteIndex] & (1 << (location & 0x07))) == 0)
            return location;
    }

    return FP_LOCATION_NONE;
}

void FingerprintInterface::forEachLocation(std::function<void(uint16_t location)> visitor) const
{
    if (!isReady() || visitor == nullptr)
        return;

    uint16_t capacity = libraryCapacity();
    for (uint16_t location = 0; location < capacity; location++)
    {
        uint16_t byteIndex = location >> 3;
        if (byteIndex >= FP_INDEX_BITMAP_SIZE)
            break;
        if (_index[byteIndex] & (1 << (location & 0x07)))
            visitor(location);
    }
}

// ---------------------------------------------------------------------------------------
// operation layer
// ---------------------------------------------------------------------------------------

bool FingerprintInterface::beginOperation(Op op, FpCallback callback)
{
    if (!_poweredOn)
    {
        _startError = FpStatus::ErrPoweredOff;
        return false;
    }
    if (_bringUp != BringUp::Ready)
    {
        _startError = FpStatus::ErrNotReady;
        return false;
    }
    if (_op != Op::None)
    {
        _startError = FpStatus::ErrBusy;
        return false;
    }

    _startError = FpStatus::Ok;
    _op = op;
    _opStep = 0;
    _opRetries = 0;
    _opCapture = 0;
    _opTimer = 0;
    _pollTimer = 0;
    _opLocation = 0;
    _opCallback = callback;
    _opProgressCallback = nullptr;
    _opStepCallback = nullptr;
    _cancelPending = false;
    _cmdDone = false;
    _cmdResult = {};
    resetDataBindings();
    return true;
}

bool FingerprintInterface::beginRawCommand(FpCommand command, const uint8_t *params, uint8_t paramLength, FpCallback callback)
{
    if (!supportsCommand(command))
    {
        _startError = FpStatus::ErrUnsupportedModel;
        return false;
    }
    if (!beginOperation(Op::Raw, callback))
        return false;

    _stagedCmd = command;
    stageParams(params, paramLength);
    return true;
}

void FingerprintInterface::finishOperation(FpStatus status)
{
    _lastResult = _cmdResult;
    _lastResult.status = status;
    if ((uint8_t)status >= 0xE0 && (uint8_t)status <= 0xEF)
        _lastResult.rawConfirmation = 0;

    // an operation may end while its command is still in flight (timeout, cancel, power off);
    // abandon it so no orphaned result blocks the next command
    if (!_ledInFlight && (_phase != Phase::Idle || _retryPending))
    {
        _phase = Phase::Idle;
        _retryPending = false;
        _txLength = 0;
        _txSent = 0;
        flushReceive();
    }
    _cmdDone = false;

    FpCallback callback = _opCallback;
    _op = Op::None;
    _opStep = 0;
    _opCallback = nullptr;
    _opProgressCallback = nullptr;
    _opStepCallback = nullptr;
    _cancelPending = false;

    if (callback != nullptr)
        callback(_lastResult);
}

void FingerprintInterface::reportProgress(uint8_t progress)
{
    // only the enrollment composite reports progress; it is mirrored here so a consumer which has
    // to answer synchronously (the ETS enroll poll) can read it without its own callback state
    _enrollProgress = progress;

    if (_opProgressCallback != nullptr)
        _opProgressCallback(progress);
}

void FingerprintInterface::processOperation()
{
    switch (_op)
    {
        case Op::None:
            return;
        case Op::BringUp:
            processBringUp();
            return;
        case Op::Raw:
            processRaw();
            return;
        case Op::SearchFinger:
            processSearchFinger();
            return;
        case Op::Enroll:
            processEnroll();
            return;
        case Op::RetrieveTemplate:
            processRetrieveTemplate();
            return;
        case Op::StoreTemplate:
            processStoreTemplate();
            return;
        case Op::DeleteTemplate:
            processDeleteTemplate();
            return;
        case Op::EmptyDatabase:
            processEmptyDatabase();
            return;
        case Op::SetPassword:
            processSetPassword();
            return;
        case Op::HealthCheck:
            processHealthCheck();
            return;
        case Op::RefreshIndex:
            processRefreshIndex();
            return;
        case Op::MultiAck:
            processMultiAck();
            return;
    }
}

void FingerprintInterface::processRaw()
{
    switch (_opStep)
    {
        case SimpleSend:
            // the only cancellable moment of a raw command: once it is on the wire the module
            // answers it in any case and the acknowledge has to be consumed
            if (_cancelPending)
            {
                finishOperation(FpStatus::ErrCancelled);
                return;
            }
            if (!issueStagedCommand())
                return;
            _opStep = SimpleAck;
            return;

        case SimpleAck:
            if (!commandTaken())
                return;
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processSearchFinger()
{
    if (_cancelPending && _phase == Phase::Idle)
    {
        finishOperation(FpStatus::ErrCancelled);
        return;
    }

    switch (_opStep)
    {
        case SearchCapture:
            if (!issueCommand(FpCommand::GenImg, nullptr, 0))
                return;
            _opStep = SearchCaptureAck;
            return;

        case SearchCaptureAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                finishOperation(_cmdResult.status);
                return;
            }
            setLed(ScanFinger);
            _opStep = SearchFeature;
            return;

        case SearchFeature:
        {
            uint8_t bufferId = 1;
            if (!issueCommand(FpCommand::GenChar, &bufferId, 1))
                return;
            _opStep = SearchFeatureAck;
            return;
        }

        case SearchFeatureAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                // an unusable image is reported as "no match" (this is what the scan pipeline and
                // the group objects above it expect), the original sensor code stays available in
                // rawConfirmation for diagnostics
                if (_cmdResult.status == FpStatus::ImageMessy ||
                    _cmdResult.status == FpStatus::FeatureFail ||
                    _cmdResult.status == FpStatus::InvalidImage)
                {
                    finishOperation(FpStatus::NoMatch);
                    return;
                }
                finishOperation(_cmdResult.status);
                return;
            }
            _opStep = SearchLookup;
            return;

        case SearchLookup:
        {
            uint16_t capacity = libraryCapacity();
            uint8_t params[5] = {1, 0, 0, (uint8_t)(capacity >> 8), (uint8_t)(capacity & 0xFF)};
            if (!issueCommand(FpCommand::Search, params, sizeof(params)))
                return;
            _opStep = SearchLookupAck;
            return;
        }

        case SearchLookupAck:
            if (!commandTaken())
                return;
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processEnroll()
{
    if (_cancelPending && _phase == Phase::Idle)
    {
        setLed(Failed);
        finishOperation(FpStatus::ErrCancelled);
        return;
    }

    switch (_opStep)
    {
        case EnrollStart:
            _opCapture = 1;
            _opStep = EnrollCapture;
            return;

        case EnrollCapture:
            reportProgress(_opCapture);
            setLed(WaitForFinger);
            _opTimer = delayTimerInit();
            _pollTimer = 0;
            _opStep = EnrollCaptureAck;
            return;

        case EnrollCaptureAck:
            if (commandTaken())
            {
                if (_cmdResult.ok())
                {
                    _opStep = EnrollFeature;
                    return;
                }
                if (_cmdResult.status != FpStatus::NoFinger)
                {
                    setLed(Failed);
                    finishOperation(_cmdResult.status);
                    return;
                }
            }
            if (delayCheck(_opTimer, FP_FINGER_WAIT_MS))
            {
                logDebugP("Enroll capture %u timed out", _opCapture);
                setLed(Failed);
                finishOperation(FpStatus::SensorTimeout);
                return;
            }
            if (_pollTimer != 0 && !delayCheck(_pollTimer, FP_FINGER_POLL_MS))
                return;
            if (issueCommand(FpCommand::GenImg, nullptr, 0))
                _pollTimer = delayTimerInit();
            return;

        case EnrollFeature:
            if (!issueCommand(FpCommand::GenChar, &_opCapture, 1))
                return;
            _opStep = EnrollFeatureAck;
            return;

        case EnrollFeatureAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                setLed(Failed);
                finishOperation(_cmdResult.status);
                return;
            }
            if (_opCapture < 6)
            {
                setLed(RemoveFinger);
                _opTimer = delayTimerInit();
                _opStep = EnrollRemoveSettle;
            }
            else
            {
                setLed(EnrollCreateModel);
                _opTimer = delayTimerInit();
                _opStep = EnrollModelWait;
            }
            return;

        case EnrollRemoveSettle:
            if (!delayCheck(_opTimer, FP_REMOVE_SETTLE_MS))
                return;
            _opTimer = delayTimerInit();
            _pollTimer = 0;
            _opStep = EnrollRemovePoll;
            return;

        case EnrollRemovePoll:
            if (delayCheck(_opTimer, FP_FINGER_WAIT_MS))
            {
                // bounded on purpose: a finger which is never taken off the sensor must not hold the
                // scanner (and the enrollment) forever
                logDebugP("Finger was not removed in time");
                setLed(Failed);
                finishOperation(FpStatus::SensorTimeout);
                return;
            }
            if (_pollTimer != 0 && !delayCheck(_pollTimer, FP_FINGER_POLL_MS))
                return;
            if (issueCommand(FpCommand::GenImg, nullptr, 0))
            {
                _pollTimer = delayTimerInit();
                _opStep = EnrollRemoveAck;
            }
            return;

        case EnrollRemoveAck:
            if (!commandTaken())
                return;
            if (_cmdResult.status == FpStatus::NoFinger)
            {
                _opCapture++;
                _opStep = EnrollCapture;
                return;
            }
            _opStep = EnrollRemovePoll;
            return;

        case EnrollModelWait:
            if (!delayCheck(_opTimer, FP_MODEL_LED_MS))
                return;
            reportProgress(7);
            _opStep = EnrollModel;
            return;

        case EnrollModel:
            if (!issueCommand(FpCommand::RegModel, nullptr, 0))
                return;
            _opStep = EnrollModelAck;
            return;

        case EnrollModelAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                setLed(Failed);
                finishOperation(_cmdResult.status);
                return;
            }
            reportProgress(8);
            _opStep = EnrollStore;
            return;

        case EnrollStore:
        {
            uint8_t params[3] = {1, (uint8_t)(_opLocation >> 8), (uint8_t)(_opLocation & 0xFF)};
            if (!issueCommand(FpCommand::Store, params, sizeof(params)))
                return;
            _opStep = EnrollStoreAck;
            return;
        }

        case EnrollStoreAck:
            if (!commandTaken())
                return;
            if (_cmdResult.ok())
            {
                if (!hasLocation(_opLocation))
                    _templateCount++;
                markLocation(_opLocation, true);
                // Store has no location in its acknowledge, so the result is made self describing
                _cmdResult.location = _opLocation;
                setLed(Success);
            }
            else
                setLed(Failed);
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processRetrieveTemplate()
{
    switch (_opStep)
    {
        case TemplateFirst:
        {
            uint8_t params[3] = {1, (uint8_t)(_opLocation >> 8), (uint8_t)(_opLocation & 0xFF)};
            if (!issueCommand(FpCommand::LoadChar, params, sizeof(params)))
                return;
            _opStep = TemplateFirstAck;
            return;
        }

        case TemplateFirstAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                finishOperation(_cmdResult.status);
                return;
            }
            _opStep = TemplateSecond;
            return;

        case TemplateSecond:
        {
            uint8_t bufferId = 1;
            if (!issueCommand(FpCommand::UpChar, &bufferId, 1))
                return;
            _opStep = TemplateSecondAck;
            return;
        }

        case TemplateSecondAck:
            if (!commandTaken())
                return;
            if (_cmdResult.ok() && _cmdResult.length != templateSize())
            {
                logErrorP("Template size mismatch: %u received, %u expected", _cmdResult.length, templateSize());
                finishOperation(FpStatus::ErrFraming);
                return;
            }
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processStoreTemplate()
{
    switch (_opStep)
    {
        case TemplateFirst:
        {
            uint8_t bufferId = 1;
            if (!issueCommand(FpCommand::DownChar, &bufferId, 1))
                return;
            _opStep = TemplateFirstAck;
            return;
        }

        case TemplateFirstAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                finishOperation(_cmdResult.status);
                return;
            }
            _opStep = TemplateSecond;
            return;

        case TemplateSecond:
        {
            uint8_t params[3] = {1, (uint8_t)(_opLocation >> 8), (uint8_t)(_opLocation & 0xFF)};
            if (!issueCommand(FpCommand::Store, params, sizeof(params)))
                return;
            _opStep = TemplateSecondAck;
            return;
        }

        case TemplateSecondAck:
            if (!commandTaken())
                return;
            if (_cmdResult.ok())
            {
                if (!hasLocation(_opLocation))
                    _templateCount++;
                markLocation(_opLocation, true);
                finishOperation(FpStatus::Ok);
                return;
            }
            // one extra attempt, the module can still be busy right after a template download
            if ((uint8_t)_cmdResult.status >= 0xE0 && _opRetries == 0)
            {
                _opRetries = 1;
                _opStep = TemplateSecond;
                return;
            }
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processDeleteTemplate()
{
    switch (_opStep)
    {
        case LatchedLed:
            if (!hasLocation(_opLocation))
            {
                // no wire traffic for unknown locations, the index cache answers them
                setLed(DeleteNotFound);
                finishOperation(FpStatus::NotFound);
                return;
            }
            // the busy LED goes out first, the command follows once the latch was flushed
            setLed(Busy);
            _opStep = LatchedSend;
            return;

        case LatchedSend:
        {
            uint8_t params[4] = {(uint8_t)(_opLocation >> 8), (uint8_t)(_opLocation & 0xFF), 0x00, 0x01};
            if (!issueCommand(FpCommand::DeletChar, params, sizeof(params)))
                return;
            _opStep = LatchedAck;
            return;
        }

        case LatchedAck:
            if (!commandTaken())
                return;
            if (_cmdResult.ok())
            {
                if (hasLocation(_opLocation) && _templateCount > 0)
                    _templateCount--;
                markLocation(_opLocation, false);
                setLed(Success);
            }
            else
                setLed(Failed);
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processEmptyDatabase()
{
    switch (_opStep)
    {
        case LatchedLed:
            // the busy LED goes out first, the command follows once the latch was flushed
            setLed(Busy);
            _opStep = LatchedSend;
            return;

        case LatchedSend:
            if (!issueCommand(FpCommand::Empty, nullptr, 0))
                return;
            _opStep = LatchedAck;
            return;

        case LatchedAck:
            if (!commandTaken())
                return;
            if (_cmdResult.ok())
            {
                clearIndex();
                _templateCount = 0;
                _indexValidUpTo = libraryCapacity();
                setLed(Success);
            }
            else
                setLed(Failed);
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processSetPassword()
{
    switch (_opStep)
    {
        case LatchedLed:
            // the busy LED goes out first, the command follows once the latch was flushed
            setLed(Busy);
            _opStep = LatchedSend;
            return;

        case LatchedSend:
        {
            uint8_t params[4] = {(uint8_t)(_pendingPassword >> 24), (uint8_t)(_pendingPassword >> 16),
                                 (uint8_t)(_pendingPassword >> 8), (uint8_t)(_pendingPassword & 0xFF)};
            if (!issueCommand(FpCommand::SetPwd, params, sizeof(params)))
                return;
            _opStep = LatchedAck;
            return;
        }

        case LatchedAck:
            if (!commandTaken())
                return;
            if (_cmdResult.ok())
            {
                // adopt the new password for the running session, no re-init needed
                _password = _pendingPassword;
                setLed(Success);
            }
            else
                setLed(Failed);
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processHealthCheck()
{
    switch (_opStep)
    {
        case HealthVerify:
            // a module that restarted behind our back lost its session, so the password is
            // verified again before the sensor itself is probed
            if (!_sensorMaybeRebooted)
            {
                _opStep = HealthProbe;
                return;
            }
            {
                uint8_t params[4] = {(uint8_t)(_password >> 24), (uint8_t)(_password >> 16),
                                     (uint8_t)(_password >> 8), (uint8_t)(_password & 0xFF)};
                if (!issueCommand(FpCommand::VfyPwd, params, sizeof(params)))
                    return;
            }
            _opStep = HealthVerifyAck;
            return;

        case HealthVerifyAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                finishOperation(_cmdResult.status);
                return;
            }
            logDebugP("Session verified again after a sensor restart");
            _sensorMaybeRebooted = false;
            _opStep = HealthProbe;
            return;

        case HealthProbe:
        {
            // the R503Pro has no CheckSensor, its handshake serves the same purpose
            FpCommand command = supportsCommand(FpCommand::CheckSensor) ? FpCommand::CheckSensor : FpCommand::HandShake;
            if (!issueCommand(command, nullptr, 0))
                return;
            _opStep = HealthProbeAck;
            return;
        }

        case HealthProbeAck:
            if (!commandTaken())
                return;
            finishOperation(_cmdResult.status);
            return;
    }
}

bool FingerprintInterface::advanceIndexPage()
{
    applyIndexPage(_indexPage, _indexScratch);
    uint16_t covered = (uint16_t)(_indexPage + 1) * 256;
    _indexValidUpTo = covered > libraryCapacity() ? libraryCapacity() : covered;
    _indexPage++;
    return _indexPage < indexPageCount();
}

void FingerprintInterface::processRefreshIndex()
{
    switch (_opStep)
    {
        case IndexCount:
            if (!issueCommand(FpCommand::TempleteNum, nullptr, 0))
                return;
            _opStep = IndexCountAck;
            return;

        case IndexCountAck:
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                finishOperation(_cmdResult.status);
                return;
            }
            _templateCount = _cmdResult.length;
            clearIndex();
            _indexPage = 0;
            _opStep = IndexRead;
            return;

        case IndexRead:
            if (!issueCommand(FpCommand::ReadIndexTable, &_indexPage, 1))
                return;
            _opStep = IndexReadAck;
            return;

        case IndexReadAck:
            if (!commandTaken())
                return;
            if (_cmdResult.ok())
            {
                if (advanceIndexPage())
                    _opStep = IndexRead;
                else
                    finishOperation(FpStatus::Ok);
                return;
            }
            // the R503Pro manual documents index pages 0-3 only, 4-5 (slots 1024-1499) are probed
            if (_model == FpModel::R503Pro && _indexPage >= 4)
            {
                logInfoP("Index page %u not supported (%s), cache valid up to %u",
                         _indexPage, fpStatusText(_cmdResult.status), _indexValidUpTo);
                finishOperation(FpStatus::Ok);
                return;
            }
            finishOperation(_cmdResult.status);
            return;
    }
}

void FingerprintInterface::processMultiAck()
{
    if (_multiAckStepPending)
    {
        _multiAckStepPending = false;
        logDebugP("%s step 0x%02X, parameter %u", fpCommandText(_cmd), _multiAckStep, _cmdResult.location);
        if (_opStepCallback != nullptr)
            _opStepCallback(_multiAckStep, _cmdResult.location);
    }

    switch (_opStep)
    {
        case SimpleSend:
            if (!issueStagedCommand())
                return;
            _opStep = SimpleAck;
            return;

        case SimpleAck:
            if (!commandTaken())
                return;
            finishOperation(_cmdResult.status);
            return;
    }
}

// ---------------------------------------------------------------------------------------
// bring-up
// ---------------------------------------------------------------------------------------

void FingerprintInterface::failBringUp(const char *reason, FpStatus status)
{
    logErrorP("%s (%s)", reason, fpStatusText(status));
    _bringUp = BringUp::Fault;
    _op = Op::None;
    _opStep = 0;
    if (_readyCallback != nullptr)
        _readyCallback(false);
}

void FingerprintInterface::processBringUp()
{
    switch (_bringUp)
    {
        case BringUp::Off:
        case BringUp::Ready:
        case BringUp::Fault:
            _op = Op::None;
            return;

        case BringUp::PowerStabilize:
            if (!delayCheck(_bringUpTimer, FP_POWER_STABILIZE_MS))
                return;
            flushReceive();
            _bringUp = BringUp::BootWait;
            _bringUpTimer = delayTimerInit();
            return;

        case BringUp::BootWait:
            // a warm start (or a board without SCANNER_PWR_PIN) never sends 0x55
            if (!_bootByteSeen && !delayCheck(_bringUpTimer, FP_BOOT_WAIT_MS))
                return;
            logDebugP("Boot byte %s", _bootByteSeen ? "received" : "not received, continuing anyway");
            _bringUp = BringUp::VerifyPwd;
            _bringUpRetries = 0;
            _opStep = 0;
            return;

        case BringUp::VerifyPwd:
            if (_opStep == 0)
            {
                uint8_t params[4] = {(uint8_t)(_password >> 24), (uint8_t)(_password >> 16),
                                     (uint8_t)(_password >> 8), (uint8_t)(_password & 0xFF)};
                if (!issueCommand(FpCommand::VfyPwd, params, sizeof(params)))
                    return;
                _opStep = 1;
                return;
            }
            if (_opStep == 1)
            {
                if (!commandTaken())
                    return;
                if (_cmdResult.ok())
                {
                    logInfoP("Fingerprint sensor found");
                    _bringUp = BringUp::ReadParams;
                    _opStep = 0;
                    return;
                }
                if (_cmdResult.status == FpStatus::PasswordFail)
                {
                    failBringUp("Fingerprint password invalid", _cmdResult.status);
                    return;
                }
                if (++_bringUpRetries >= FP_VFYPWD_ATTEMPTS)
                {
                    failBringUp("Fingerprint scanner not responding", _cmdResult.status);
                    return;
                }
                _bringUpTimer = delayTimerInit();
                _opStep = 2;
                return;
            }
            if (delayCheck(_bringUpTimer, FP_VFYPWD_GAP_MS))
                _opStep = 0;
            return;

        case BringUp::ReadParams:
            if (_opStep == 0)
            {
                if (!issueCommand(FpCommand::ReadSysPara, nullptr, 0))
                    return;
                _opStep = 1;
                return;
            }
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                failBringUp("Cannot read system parameters", _cmdResult.status);
                return;
            }
            if (_sysPara.librarySize != libraryCapacity())
                logErrorP("Configured model %s expects a library of %u but the sensor reports %u, check the ETS scanner type",
                          fpModelText(_model), libraryCapacity(), _sysPara.librarySize);
            _bringUp = supportsCommand(FpCommand::ReadProdInfo) ? BringUp::ReadProduct : BringUp::ReadAlgVer;
            _opStep = 0;
            return;

        case BringUp::ReadProduct:
            if (_opStep == 0)
            {
                if (!issueCommand(FpCommand::ReadProdInfo, nullptr, 0))
                    return;
                _opStep = 1;
                return;
            }
            if (!commandTaken())
                return;
            _bringUp = BringUp::ReadAlgVer;
            _opStep = 0;
            return;

        case BringUp::ReadAlgVer:
            if (_opStep == 0)
            {
                if (!issueCommand(FpCommand::GetAlgVer, nullptr, 0))
                    return;
                _opStep = 1;
                return;
            }
            if (!commandTaken())
                return;
            _bringUp = BringUp::ReadFwVer;
            _opStep = 0;
            return;

        case BringUp::ReadFwVer:
            if (_opStep == 0)
            {
                if (!issueCommand(FpCommand::GetFwVer, nullptr, 0))
                    return;
                _opStep = 1;
                return;
            }
            if (!commandTaken())
                return;
            _bringUp = BringUp::ReadCount;
            _opStep = 0;
            return;

        case BringUp::ReadCount:
            if (_opStep == 0)
            {
                if (!issueCommand(FpCommand::TempleteNum, nullptr, 0))
                    return;
                _opStep = 1;
                return;
            }
            if (!commandTaken())
                return;
            if (!_cmdResult.ok())
            {
                failBringUp("Cannot read template count", _cmdResult.status);
                return;
            }
            _templateCount = _cmdResult.length;
            clearIndex();
            _indexPage = 0;
            _bringUp = BringUp::ReadIndex;
            _opStep = 0;
            return;

        case BringUp::ReadIndex:
            if (_opStep == 0)
            {
                if (!issueCommand(FpCommand::ReadIndexTable, &_indexPage, 1))
                    return;
                _opStep = 1;
                return;
            }
            if (!commandTaken())
                return;
            if (_cmdResult.ok())
            {
                if (advanceIndexPage())
                {
                    _opStep = 0;
                    return;
                }
            }
            else if (_model == FpModel::R503Pro && _indexPage >= 4)
            {
                // pages 4-5 (slots 1024-1499) are undocumented for the R503Pro
                logInfoP("Index page %u not supported (%s), cache valid up to %u",
                         _indexPage, fpStatusText(_cmdResult.status), _indexValidUpTo);
            }
            else
            {
                failBringUp("Cannot read the template index table", _cmdResult.status);
                return;
            }

            _bringUp = BringUp::Ready;
            _op = Op::None;
            _opStep = 0;
            logInfoP("Scanner ready, %u templates stored", _templateCount);
            if (_readyCallback != nullptr)
                _readyCallback(true);
            return;
    }
}

// ---------------------------------------------------------------------------------------
// raw command starters
// ---------------------------------------------------------------------------------------

bool FingerprintInterface::startGenImg(FpCallback callback)
{
    return beginRawCommand(FpCommand::GenImg, nullptr, 0, callback);
}

bool FingerprintInterface::startGenChar(uint8_t bufferId, FpCallback callback)
{
    if (bufferId < 1 || bufferId > 6)
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    return beginRawCommand(FpCommand::GenChar, &bufferId, 1, callback);
}

bool FingerprintInterface::startMatch(FpCallback callback)
{
    return beginRawCommand(FpCommand::Match, nullptr, 0, callback);
}

bool FingerprintInterface::startSearch(uint8_t bufferId, uint16_t startId, uint16_t num, FpCallback callback)
{
    uint8_t params[5] = {bufferId, (uint8_t)(startId >> 8), (uint8_t)(startId & 0xFF),
                         (uint8_t)(num >> 8), (uint8_t)(num & 0xFF)};
    return beginRawCommand(FpCommand::Search, params, sizeof(params), callback);
}

bool FingerprintInterface::startRegModel(FpCallback callback)
{
    return beginRawCommand(FpCommand::RegModel, nullptr, 0, callback);
}

bool FingerprintInterface::startStore(uint8_t bufferId, uint16_t location, FpCallback callback)
{
    // raw command - bypasses the index cache; prefer the composite (startDeleteTemplate/
    // startEmptyDatabase/startStoreTemplate) which keeps the cache in sync
    uint8_t params[3] = {bufferId, (uint8_t)(location >> 8), (uint8_t)(location & 0xFF)};
    return beginRawCommand(FpCommand::Store, params, sizeof(params), callback);
}

bool FingerprintInterface::startLoadChar(uint8_t bufferId, uint16_t location, FpCallback callback)
{
    uint8_t params[3] = {bufferId, (uint8_t)(location >> 8), (uint8_t)(location & 0xFF)};
    return beginRawCommand(FpCommand::LoadChar, params, sizeof(params), callback);
}

bool FingerprintInterface::startUpChar(uint8_t bufferId, uint8_t *destination, uint16_t destinationSize, FpCallback callback)
{
    if (!beginRawCommand(FpCommand::UpChar, &bufferId, 1, callback))
        return false;

    _dataDst = destination;
    _dataDstSize = destinationSize;
    return true;
}

bool FingerprintInterface::startDownChar(uint8_t bufferId, const uint8_t *source, uint16_t length, FpCallback callback)
{
    if (source == nullptr || length == 0)
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginRawCommand(FpCommand::DownChar, &bufferId, 1, callback))
        return false;

    _dataSrc = source;
    _dataSrcLength = length;
    return true;
}

bool FingerprintInterface::startUpImage(FpDataSink sink, FpCallback callback)
{
    if (!beginRawCommand(FpCommand::UpImage, nullptr, 0, callback))
        return false;

    _dataSink = sink;
    return true;
}

bool FingerprintInterface::startDownImage(FpDataSource source, uint32_t length, FpCallback callback)
{
    if (source == nullptr || length == 0)
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginRawCommand(FpCommand::DownImage, nullptr, 0, callback))
        return false;

    _dataSource = source;
    _dataSrcLength = length;
    return true;
}

bool FingerprintInterface::startDeletChar(uint16_t startId, uint16_t count, FpCallback callback)
{
    // raw command - bypasses the index cache; prefer the composite (startDeleteTemplate/
    // startEmptyDatabase/startStoreTemplate) which keeps the cache in sync
    uint8_t params[4] = {(uint8_t)(startId >> 8), (uint8_t)(startId & 0xFF),
                         (uint8_t)(count >> 8), (uint8_t)(count & 0xFF)};
    return beginRawCommand(FpCommand::DeletChar, params, sizeof(params), callback);
}

bool FingerprintInterface::startEmpty(FpCallback callback)
{
    // raw command - bypasses the index cache; prefer the composite (startDeleteTemplate/
    // startEmptyDatabase/startStoreTemplate) which keeps the cache in sync
    return beginRawCommand(FpCommand::Empty, nullptr, 0, callback);
}

bool FingerprintInterface::startSetSysPara(uint8_t parameter, uint8_t value, FpCallback callback)
{
    uint8_t params[2] = {parameter, value};
    return beginRawCommand(FpCommand::SetSysPara, params, sizeof(params), callback);
}

bool FingerprintInterface::startReadSysPara(FpCallback callback)
{
    return beginRawCommand(FpCommand::ReadSysPara, nullptr, 0, callback);
}

bool FingerprintInterface::startSetPwd(uint32_t password, FpCallback callback)
{
    uint8_t params[4] = {(uint8_t)(password >> 24), (uint8_t)(password >> 16),
                         (uint8_t)(password >> 8), (uint8_t)(password & 0xFF)};
    return beginRawCommand(FpCommand::SetPwd, params, sizeof(params), callback);
}

bool FingerprintInterface::startVfyPwd(uint32_t password, FpCallback callback)
{
    uint8_t params[4] = {(uint8_t)(password >> 24), (uint8_t)(password >> 16),
                         (uint8_t)(password >> 8), (uint8_t)(password & 0xFF)};
    return beginRawCommand(FpCommand::VfyPwd, params, sizeof(params), callback);
}

bool FingerprintInterface::startGetRandomCode(uint8_t *destination, FpCallback callback)
{
    if (!beginRawCommand(FpCommand::GetRandomCode, nullptr, 0, callback))
        return false;

    _dataDst = destination;
    _dataDstSize = destination != nullptr ? 4 : 0;
    return true;
}

bool FingerprintInterface::startSetAdder(uint32_t address, FpCallback callback)
{
    uint8_t params[4] = {(uint8_t)(address >> 24), (uint8_t)(address >> 16),
                         (uint8_t)(address >> 8), (uint8_t)(address & 0xFF)};
    if (!beginRawCommand(FpCommand::SetAdder, params, sizeof(params), callback))
        return false;

    _pendingAddress = address;
    return true;
}

bool FingerprintInterface::startReadInfPage(uint8_t *destination, uint16_t destinationSize, FpCallback callback)
{
    if (!beginRawCommand(FpCommand::ReadInfPage, nullptr, 0, callback))
        return false;

    _dataDst = destination;
    _dataDstSize = destinationSize;
    return true;
}

bool FingerprintInterface::startWriteNotepad(uint8_t page, const uint8_t *content, FpCallback callback)
{
    if (page > 0x0F || content == nullptr)
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }

    uint8_t params[1 + FP_NOTEPAD_PAGE_SIZE];
    params[0] = page;
    memcpy(params + 1, content, FP_NOTEPAD_PAGE_SIZE);
    return beginRawCommand(FpCommand::WriteNotepad, params, sizeof(params), callback);
}

bool FingerprintInterface::startReadNotepad(uint8_t page, uint8_t *destination, FpCallback callback)
{
    if (page > 0x0F)
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginRawCommand(FpCommand::ReadNotepad, &page, 1, callback))
        return false;

    _dataDst = destination;
    _dataDstSize = destination != nullptr ? FP_NOTEPAD_PAGE_SIZE : 0;
    return true;
}

bool FingerprintInterface::startTempleteNum(FpCallback callback)
{
    return beginRawCommand(FpCommand::TempleteNum, nullptr, 0, callback);
}

bool FingerprintInterface::startReadIndexTable(uint8_t page, uint8_t *destination, FpCallback callback)
{
    if (!beginRawCommand(FpCommand::ReadIndexTable, &page, 1, callback))
        return false;

    _dataDst = destination;
    _dataDstSize = destination != nullptr ? FP_NOTEPAD_PAGE_SIZE : 0;
    return true;
}

bool FingerprintInterface::startGetImageEx(FpCallback callback)
{
    return beginRawCommand(FpCommand::GetImageEx, nullptr, 0, callback);
}

bool FingerprintInterface::startCancel(FpCallback callback)
{
    return beginRawCommand(FpCommand::Cancel, nullptr, 0, callback);
}

bool FingerprintInterface::startAuraLedConfig(uint8_t control, uint8_t speed, uint8_t color, uint8_t count, FpCallback callback)
{
    if (!supportsLedColor(color))
    {
        _startError = FpStatus::ErrUnsupportedModel;
        return false;
    }

    uint8_t params[4] = {control, speed, color, count};
    return beginRawCommand(FpCommand::AuraLedConfig, params, sizeof(params), callback);
}

bool FingerprintInterface::startCheckSensor(FpCallback callback)
{
    return beginRawCommand(FpCommand::CheckSensor, nullptr, 0, callback);
}

bool FingerprintInterface::startGetAlgVer(uint8_t *destination, FpCallback callback)
{
    if (!beginRawCommand(FpCommand::GetAlgVer, nullptr, 0, callback))
        return false;

    _dataDst = destination;
    _dataDstSize = destination != nullptr ? FP_VERSION_STRING_SIZE : 0;
    return true;
}

bool FingerprintInterface::startGetFwVer(uint8_t *destination, FpCallback callback)
{
    if (!beginRawCommand(FpCommand::GetFwVer, nullptr, 0, callback))
        return false;

    _dataDst = destination;
    _dataDstSize = destination != nullptr ? FP_VERSION_STRING_SIZE : 0;
    return true;
}

bool FingerprintInterface::startReadProdInfo(FpCallback callback)
{
    return beginRawCommand(FpCommand::ReadProdInfo, nullptr, 0, callback);
}

bool FingerprintInterface::startSoftRst(FpCallback callback)
{
    return beginRawCommand(FpCommand::SoftRst, nullptr, 0, callback);
}

bool FingerprintInterface::startHandShake(FpCallback callback)
{
    return beginRawCommand(FpCommand::HandShake, nullptr, 0, callback);
}

// ---------------------------------------------------------------------------------------
// composite operations
// ---------------------------------------------------------------------------------------

bool FingerprintInterface::startDetectFinger(FpCallback callback)
{
    return beginRawCommand(FpCommand::GenImg, nullptr, 0, callback);
}

bool FingerprintInterface::startSearchFinger(FpCallback callback)
{
    return beginOperation(Op::SearchFinger, callback);
}

bool FingerprintInterface::startEnroll(uint16_t location, FpProgressCallback progressCallback, FpCallback callback)
{
    if (location >= libraryCapacity())
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginOperation(Op::Enroll, callback))
        return false;

    _opLocation = location;
    _opProgressCallback = progressCallback;
    // no stale progress of the previous enrollment is ever reported
    _enrollProgress = 0;
    return true;
}

bool FingerprintInterface::startRetrieveTemplate(uint16_t location, uint8_t *destination, uint16_t destinationSize, FpCallback callback)
{
    if (destination == nullptr || destinationSize < templateSize())
    {
        _startError = FpStatus::ErrBufferTooSmall;
        return false;
    }
    if (location >= libraryCapacity())
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginOperation(Op::RetrieveTemplate, callback))
        return false;

    _opLocation = location;
    _dataDst = destination;
    _dataDstSize = destinationSize;
    return true;
}

bool FingerprintInterface::startStoreTemplate(uint16_t location, const uint8_t *source, uint16_t length, FpCallback callback)
{
    // a short or long template would be silently truncated by the module
    if (location >= libraryCapacity() || source == nullptr || length != templateSize())
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginOperation(Op::StoreTemplate, callback))
        return false;

    _opLocation = location;
    _dataSrc = source;
    _dataSrcLength = length;
    return true;
}

bool FingerprintInterface::startDeleteTemplate(uint16_t location, FpCallback callback)
{
    if (location >= libraryCapacity())
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginOperation(Op::DeleteTemplate, callback))
        return false;

    _opLocation = location;
    return true;
}

bool FingerprintInterface::startEmptyDatabase(FpCallback callback)
{
    return beginOperation(Op::EmptyDatabase, callback);
}

bool FingerprintInterface::startSetPassword(uint32_t newPassword, FpCallback callback)
{
    if (!beginOperation(Op::SetPassword, callback))
        return false;

    _pendingPassword = newPassword;
    return true;
}

bool FingerprintInterface::startHealthCheck(FpCallback callback)
{
    return beginOperation(Op::HealthCheck, callback);
}

bool FingerprintInterface::startRefreshIndex(FpCallback callback)
{
    return beginOperation(Op::RefreshIndex, callback);
}

bool FingerprintInterface::startAutoEnroll(uint16_t location, bool allowOverwrite, bool allowDuplicate, bool requireLeave,
                                           FpStepCallback stepCallback, FpCallback callback)
{
    // valid are a concrete slot inside the library and the "assign automatically" range of the
    // model (R503: 0xC8-0xFF in one byte, R503Pro: from 0x05DC in two bytes); everything else
    // would be truncated when the location is encoded
    if (location >= libraryCapacity())
    {
        bool autoAssign = _model == FpModel::R503Pro ? location >= 0x05DC
                                                     : (location >= 0x00C8 && location <= 0x00FF);
        if (!autoAssign)
        {
            _startError = FpStatus::ErrInvalidParam;
            return false;
        }
    }
    if (!beginOperation(Op::MultiAck, callback))
        return false;

    uint8_t params[6];
    uint8_t length = 0;
    // the R503 encodes the location in one byte, the R503Pro in two
    if (_model == FpModel::R503Pro)
    {
        params[length++] = (uint8_t)(location >> 8);
        params[length++] = (uint8_t)(location & 0xFF);
    }
    else
        params[length++] = (uint8_t)(location & 0xFF);

    params[length++] = allowOverwrite ? 1 : 0;
    params[length++] = allowDuplicate ? 1 : 0;
    params[length++] = 1; // always ask for the intermediate step acknowledges
    params[length++] = requireLeave ? 1 : 0;

    _stagedCmd = FpCommand::AutoEnroll;
    stageParams(params, length);
    _opStepCallback = stepCallback;
    return true;
}

bool FingerprintInterface::startAutoIdentify(uint8_t securityLevel, uint16_t startId, uint16_t num, uint8_t maxErrors,
                                             FpStepCallback stepCallback, FpCallback callback)
{
    if (securityLevel < 1 || securityLevel > 5)
    {
        _startError = FpStatus::ErrInvalidParam;
        return false;
    }
    if (!beginOperation(Op::MultiAck, callback))
        return false;

    uint8_t params[7];
    uint8_t length = 0;
    params[length++] = securityLevel;
    if (_model == FpModel::R503Pro)
    {
        params[length++] = (uint8_t)(startId >> 8);
        params[length++] = (uint8_t)(startId & 0xFF);
        params[length++] = (uint8_t)(num >> 8);
        params[length++] = (uint8_t)(num & 0xFF);
    }
    else
    {
        params[length++] = (uint8_t)(startId & 0xFF);
        params[length++] = (uint8_t)(num & 0xFF);
    }
    params[length++] = 1; // always ask for the intermediate step acknowledges
    params[length++] = maxErrors;

    _stagedCmd = FpCommand::AutoIdentify;
    stageParams(params, length);
    _opStepCallback = stepCallback;
    return true;
}
