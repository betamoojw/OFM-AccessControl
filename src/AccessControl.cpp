#include "AccessControl.h"
#include "FingerprintInterface.h"
#include "I2CDev.h"
#include "KeypadEmpty.h"
#include "KeypadForGira.h"
#include "KeypadMatrix3x4.h"

KeypadBase *AccessControl::keypadBase = nullptr;

const std::string AccessControl::name()
{
    return "Fingerprint";
}

const std::string AccessControl::version()
{
    return MAIN_Version;
}

void AccessControl::setup()
{
    logInfoP("Setup fingerprint module");
    logIndentUp();

    initFlashFingerprint();
    initFlashNfc();
    initFlashKeypad();

#ifdef SCANNER_PWR_PIN
    pinMode(SCANNER_PWR_PIN, OUTPUT);
#endif
    finger.init();
    // the bring-up runs asynchronously: the system parameter dump and the boot LED are done by
    // onFingerprintReady() as soon as it publishes the result
    switchFingerprintPower(true);

    for (uint16_t i = 0; i < ParamACC_VisibleActions; i++)
    {
        _channels[i] = new ActionChannel(i);
        _channels[i]->setup();
    }

    pinMode(SCANNER_TOUCH_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(SCANNER_TOUCH_PIN), AccessControl::interruptDisplayTouched, FALLING);

    if (ParamACC_EnableTouchPcb ||
        ParamACC_NfcScanner == 1)
    {
        openknx.gpio.pinMode(DIRECT_LED_GREEN_PIN, OUTPUT);
        openknx.gpio.pinMode(DIRECT_LED_RED_PIN, OUTPUT);

        openknx.gpio.pinMode(DIRECT_TOUCH_LEFT_PIN, INPUT);
        openknx.gpio.pinMode(DIRECT_TOUCH_RIGHT_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(DIRECT_TOUCH_LEFT_PIN), AccessControl::interruptTouchLeft, CHANGE);
        attachInterrupt(digitalPinToInterrupt(DIRECT_TOUCH_RIGHT_PIN), AccessControl::interruptTouchRight, CHANGE);
    }
    else if (ParamACC_NfcScanner == 2)
    {
        openknx.gpio.pinMode(EXTERN_TOUCH_LEFT_PIN, INPUT);
        openknx.gpio.pinMode(EXTERN_TOUCH_RIGHT_PIN, INPUT);
        openknx.gpio.pinMode(EXTERN_LED_GREEN_PIN, OUTPUT);
        openknx.gpio.pinMode(EXTERN_LED_RED_PIN, OUTPUT);
    }

    switchLedRedPower(false);
    switchLedGreenPower(true);

    KoACC_FingerLedRingColor.valueNoSend((uint8_t)0, Dpt(5, 10));
    KoACC_FingerLedRingControl.valueNoSend((uint8_t)FpLedControl::Off, Dpt(5, 10));
    KoACC_FingerLedRingSpeed.valueNoSend((uint8_t)0, Dpt(5, 10));
    KoACC_FingerLedRingCount.valueNoSend((uint8_t)0, Dpt(5, 10));

    checkSensorTimer = delayTimerInit();
    initResetTimer = delayTimerInit();

    initNfc();

    // create correct keypad
    switch (ParamACC_Keypad)
    {
        case 1:
            keypadBase = new KeypadForGira();
            break;
        case 2:
            keypadBase = new KeypadMatrix3x4();
            break;
    
        default:
            keypadBase = new KeypadEmpty();
            break;
    }

    keypadBase->init();
    keypadBase->registerCallback([this](char key) { onKeypadKeyPressed(key); });
    logInfoP("Fingerprint module ready.");
    logIndentDown();
}

void AccessControl::dispatchAuthAction(bool started) {
    // this is called each time an auth-action is started and should dispatch this to all
    // auth hardware to visualize auth waiting state
    // the LED goes into the drivers latch, which is flushed at the next command boundary
    if (started)
    {
        finger.setLed(FingerprintInterface::WaitForFinger);
        keypadBase->setBackgroundLed(255);
        keypadBase->setFeedback(KeypadBase::FeedbackType::WaitForCode);
    }
    else
    {
        finger.setLed(FingerprintInterface::None);
        keypadBase->setFeedback(KeypadBase::FeedbackType::Off);
    }

}

void AccessControl::initNfc(bool testMode, uint8_t testModeNfc)
{
    if (!testMode &&
        ParamACC_NfcScanner == 0)
        return;

#ifdef NCI_DEBUG
    logging::initialize();
    logging::enable(logging::destination::destUart1);

    delay(1000); // delay required to get debug serial ready

    logging::enable(logging::source::criticalError);
    logging::enable(logging::source::nciMessages);
    logging::enable(logging::source::stateChanges);
    logging::enable(logging::source::tagEvents);
#endif

    uint8_t nfcType =
        (!testMode && ParamACC_NfcScanner == 2 ||
         testMode && testModeNfc == 2) ? 2 : 1;

    if (nfcType == 2)
    {
        openknx.gpio.pinMode(0x0200, OUTPUT);
        openknx.gpio.digitalWrite(0x0200, LOW);
        openknx.gpio.pinMode(0x0201, OUTPUT);
        openknx.gpio.digitalWrite(0x0201, LOW);
    }

    PN7160Interface::initialize(NFC_IRQ_PIN, NFC_VEN_PIN, NFC_PN7160_ADDR);
    logInfoP("Initialized PN7160 (nfcType=%u).", nfcType);
}

bool AccessControl::switchFingerprintPower(bool on, bool testMode)
{
    if (!on && !testMode)
    {
        logDebugP("Ignore power off switch for now");
        return true;
    }

    logDebugP("Switch power on: %u", on);

    if (on)
    {
        // a running or already finished bring-up is never repeated
        if (fpPower == FpPowerState::Ready || fpPower == FpPowerState::Booting)
        {
            logDebugP("Fingerprint power already on");
            return true;
        }

        if (!testMode &&
            ParamACC_FingerprintScanner == 3)
        {
            // "Kein Fingerprint": no power, no UART traffic at all; every fingerprint operation
            // degrades exactly like it does with a dead scanner because the driver never leaves its
            // Off bring-up state, so isReady() stays false and the cached getters answer empty
            if (!fpScannerDisabled)
            {
                fpScannerDisabled = true;
                logInfoP("Fingerprint scanner disabled by ETS (no fingerprint scanner), skipping bring-up");
                KoACC_FingerScannerStatus.value(false, DPT_Switch);
            }

            return false;
        }

        uint8_t scannerType = ParamACC_FingerprintScanner;
        FpModel model = scannerType == 2 ? FpModel::R503Pro : FpModel::R503;
        uint32_t scannerPassword = testMode ? 0 : _fingerprintStorage.readInt(FLASH_FINGER_SCANNER_PASSWORD_OFFSET);
        logDebugP("Initialize scanner (ETS type %u, model %s) with password: %u", scannerType, fpModelText(model), scannerPassword);

        finger.setModel(model);
        finger.setPassword(scannerPassword);
        finger.setReadyCallback([this](bool ready) { onFingerprintReady(ready); });

#ifdef SCANNER_PWR_PIN
        digitalWrite(SCANNER_PWR_PIN, FINGER_PWR_ON);
#endif

        logInfoP("Fingerprint start");
        fpPowerTestMode = testMode;
        fpPower = FpPowerState::Booting;
        if (!finger.powerOn())
        {
            onFingerprintReady(false);
            return false;
        }

        // the bring-up itself is driven by finger.loop() from loop(); true means "power is applied
        // and the bring-up is under way", not "the scanner is ready"
        return true;
    }
    else
    {
        if (fpPower == FpPowerState::Off)
        {
            logDebugP("Fingerprint power already off");
            return true;
        }

        // the driver drops the UART, releases the TX pin and cuts the power pin itself; an operation
        // which is still in flight is aborted from its own loop() so no callback fires re-entrantly
        finger.powerOff();
        fpPower = FpPowerState::Off;

#ifdef SCANNER_PWR_PIN
        digitalWrite(SCANNER_PWR_PIN, FINGER_PWR_OFF);
#endif
        return true;
    }
}

// the only place which publishes the result of a bring-up; it is called from finger.loop() and
// must therefore only set flags, group objects and LEDs - never start another scanner operation
void AccessControl::onFingerprintReady(bool ready)
{
    // the boot flash belongs to the bring-up which runs out of setup(); every later one (a Fault
    // retry, the test mode exit) must not produce it, so the latch is consumed here no matter how
    // this bring-up ended
    bool bootLedPending = initBootLedPending;
    initBootLedPending = false;

    if (!ready)
    {
        fpPower = FpPowerState::Failed;

        // the driver already logged the reason (no answer, wrong password, ...)
        logErrorP("Fingerprint scanner not found!");

        if (!fpPowerTestMode)
            KoACC_FingerScannerStatus.value(false, DPT_Switch);

        return;
    }

    fpPower = FpPowerState::Ready;

    logInfoP("Found fingerprint sensor!");

    if (!fpPowerTestMode)
    {
        KoACC_FingerScannerStatus.value(true, DPT_Switch);
        // the parameter dump walks the whole index cache (and lists every stored location in a debug
        // build), which does not fit into the loop time budget; it runs once per bring-up
        openknx.common.skipLooptimeWarning();
        // in test mode the sequencer dumps the parameters itself
        finger.logSystemParameters();
    }

    // LED commands are latched inside the driver and flushed now that it is ready
    if (isLocked)
        finger.setLed(FingerprintInterface::Locked);
    else if (bootLedPending)
    {
        finger.setLed(FingerprintInterface::Success); // boot flash, cleared by initResetTimer

        // a bring-up which outlasted the initial window (several VfyPwd attempts) meets an already
        // cleared initResetTimer, so it is re-armed and the flash is really shown and cleared
        // a second later. Letting the reset block run a second time is harmless: LED None, green LED
        // off and - in touch mode - a switchFingerprintPower(false) which is a no-op.
        if (initResetTimer == 0)
            initResetTimer = delayTimerInit();
    }
}

// The scanner driver executes one operation at a time; the arbiter makes sure that only one
// module activity ever holds it. An activity acquires it before it starts its first driver
// operation and releases it when its last one is through.
bool AccessControl::arbiterAcquire(FpOwner who)
{
    if (fpOwner != FpOwner::None)
        return false;

    // the console test mode is the one activity which claims the scanner before it is up - it
    // drives the bring-up itself (the device may not even be configured) and is a manual, exclusive
    // operation, so only a foreign owner can keep it waiting
    if (who == FpOwner::Test)
    {
        fpOwner = who;
        return true;
    }

    // no operation is ever started before the bring-up published a ready scanner
    if (fpPower != FpPowerState::Ready)
        return false;

    // a foreign operation may still be in flight (bring-up, health check)
    if (finger.isBusy())
        return false;

    fpOwner = who;
    return true;
}

void AccessControl::arbiterRelease(FpOwner who)
{
    // a mismatch is a bookkeeping bug of the caller and never clears the ownership -
    // stealing it from the activity which really holds it would let two of them drive the single
    // flight driver at the same time, which is exactly what the arbiter exists to prevent
    if (fpOwner != who)
    {
        logErrorP("Scanner released by owner %u although owner %u holds it", (uint8_t)who, (uint8_t)fpOwner);
        return;
    }

    fpOwner = FpOwner::None;
}

void AccessControl::switchLedGreenPower(bool on)
{
    if (!ParamACC_EnableTouchPcb &&
        ParamACC_NfcScanner == 0)
        return;
    
    bool direct = ParamACC_EnableTouchPcb || ParamACC_NfcScanner == 1;
    openknx.gpio.digitalWrite(direct ? DIRECT_LED_GREEN_PIN : EXTERN_LED_GREEN_PIN, on ? HIGH : LOW);

    logDebugP("Switch LED green power: %u", on);
}

void AccessControl::switchLedRedPower(bool on)
{
    if (!ParamACC_EnableTouchPcb &&
        ParamACC_NfcScanner == 0)
        return;
    
    bool direct = ParamACC_EnableTouchPcb || ParamACC_NfcScanner == 1;
    openknx.gpio.digitalWrite(direct ? DIRECT_LED_RED_PIN : EXTERN_LED_RED_PIN, on ? HIGH : LOW);
    
    logDebugP("Switch LED red power: %u", on);
}

void AccessControl::initFlashFingerprint()
{
    _fingerprintStorage.init("fingerprint", FINGERPRINT_FLASH_OFFSET, FINGERPRINT_FLASH_SIZE);
    uint32_t magicWord = _fingerprintStorage.readInt(0);
    if (magicWord != OPENKNX_ACC_FLASH_FINGER_MAGIC_WORD)
    {
        logInfoP("Fingerprint flash contents invalid:");
        logIndentUp();
        logDebugP("Indentification code read: %u", magicWord);

        uint8_t clearBuffer[FLASH_SECTOR_SIZE] = {};
        for (size_t i = 0; i < FINGERPRINT_FLASH_SIZE / FLASH_SECTOR_SIZE; i++)
            _fingerprintStorage.write(FLASH_SECTOR_SIZE * i, clearBuffer, FLASH_SECTOR_SIZE);
        _fingerprintStorage.commit();
        logDebugP("Flash cleared.");

        _fingerprintStorage.writeInt(0, OPENKNX_ACC_FLASH_FINGER_MAGIC_WORD);
        _fingerprintStorage.commit();
        logDebugP("Indentification code written.");

        logIndentDown();
    }
    else
        logInfoP("Fingerprint flash contents valid.");
}

void AccessControl::initFlashNfc()
{
    _nfcStorage.init("nfc", NFC_FLASH_OFFSET, NFC_FLASH_SIZE);
    uint32_t magicWord = _nfcStorage.readInt(0);
    if (magicWord != OPENKNX_ACC_FLASH_NFC_MAGIC_WORD)
    {
        logInfoP("NFC flash contents invalid:");
        logIndentUp();
        logDebugP("Indentification code read: %u", magicWord);

        uint8_t clearBuffer[FLASH_SECTOR_SIZE] = {};
        for (size_t i = 0; i < NFC_FLASH_SIZE / FLASH_SECTOR_SIZE; i++)
            _nfcStorage.write(FLASH_SECTOR_SIZE * i, clearBuffer, FLASH_SECTOR_SIZE);
        _nfcStorage.commit();
        logDebugP("Flash cleared.");

        _nfcStorage.writeInt(0, OPENKNX_ACC_FLASH_NFC_MAGIC_WORD);
        _nfcStorage.commit();
        logDebugP("Indentification code written.");

        logIndentDown();
    }
    else
        logInfoP("NFC flash contents valid.");
}

void AccessControl::initFlashKeypad()
{
    _keypadStorage.init("keypad", KEYPAD_FLASH_OFFSET, KEYPAD_FLASH_SIZE);
    uint32_t magicWord = _keypadStorage.readInt(0);
    if (magicWord != OPENKNX_ACC_FLASH_KEY_MAGIC_WORD)
    {
        logInfoP("Keypad flash contents invalid:");
        logIndentUp();
        logDebugP("Indentification code read: %u", magicWord);

        uint8_t clearBuffer[FLASH_SECTOR_SIZE] = {};
        for (size_t i = 0; i < KEYPAD_FLASH_SIZE / FLASH_SECTOR_SIZE; i++)
            _keypadStorage.write(FLASH_SECTOR_SIZE * i, clearBuffer, FLASH_SECTOR_SIZE);
        _keypadStorage.commit();
        logDebugP("Flash cleared.");

        _keypadStorage.writeInt(0, OPENKNX_ACC_FLASH_KEY_MAGIC_WORD);
        _keypadStorage.commit();
        logDebugP("Indentification code written.");

        logIndentDown();
    }
    else
        logInfoP("Keypad flash contents valid.");
}

void AccessControl::interruptDisplayTouched()
{
    touched = true;
}

void AccessControl::interruptTouchLeft()
{
    touchLeftTouched = digitalRead(DIRECT_TOUCH_LEFT_PIN) == HIGH;
}

void AccessControl::interruptTouchRight()
{
    touchRightTouched = digitalRead(DIRECT_TOUCH_RIGHT_PIN) == HIGH;
}

// The framework calls this on every pass, the no-argument loop() below only while knx.configured()
// is true. Everything which has to work on an unconfigured device lives here: the scanner driver
// itself, the "acc fpi test" diagnostics and the console test mode sequencer.
void AccessControl::loop(bool configured)
{
    // drives the asynchronous parts of the scanner (bring-up, scan, maintenance, health check) and
    // fires their callbacks
    finger.loop();

    // the test mode owns the scanner, the LEDs, the relay, the NFC reader and the keypad, so the
    // productive pipeline stays out of its way for the whole run
    if (testModeStep > 0)
    {
        processTestMode();
        return;
    }

    // the scanner diagnostics run alongside the productive pipeline - exclusive access to the driver
    // is granted by the ownership arbiter (FpOwner::Test), not by suppressing the pipeline
    if (fpiTestStep > 0)
        processFingerprintTest();

    if (configured)
        loop();
}

void AccessControl::loop()
{
    // a lock which arrives while a scan is in flight must not keep the scanner owned: its result is
    // discarded and the arbiter handed back. The removal wait only owns the scanner while one of its
    // probes is in flight, so "scanState != Idle" does not imply ownership - without ownership there
    // is nothing of ours in flight and the state can be dropped right away, and the arbiter is only
    // touched when we really hold it.
    if (isLocked &&
        scanState != ScanState::Idle &&
        (fpOwner != FpOwner::Scan || scanDone || !finger.isBusy()))
    {
        logDebugP("Scan dropped, module locked");
        scanDone = false;
        scanState = ScanState::Idle;
        scanRemoveProbing = false;
        if (fpOwner == FpOwner::Scan)
            arbiterRelease(FpOwner::Scan);
    }

    // the start timeout of an armed enroll request is evaluated outside the lock gate: a request which
    // was armed while the module is locked (ETS function property or group object - both are processed
    // while locked) never reaches the trigger below, so without this its timer would stay armed
    // forever and the ETS wait poll would never terminate. Only the timeout lives here, the
    // enrollment itself is started exclusively while the module is unlocked.
    if (enrollRequestedFingerTimer > 0 && !enrollActive && !enrollDone &&
        delayCheck(enrollRequestedFingerTimer, ENROLL_START_TIMEOUT))
    {
        logInfoP("Enroll request:");
        logIndentUp();
        logInfoP("Scanner did not become available (locked=%u, owner=%u, power=%u).", isLocked, (uint8_t)fpOwner, (uint8_t)fpPower);
        logIndentDown();

        // the finalization below owns all the bookkeeping (log line, request fields, LED timer)
        enrollActiveLocation = enrollRequestedFingerLocation;
        enrollResult = FpStatus::ErrBusy;
        enrollSuccess = false;
        enrollDone = true;
    }

    // the finalization of an asynchronous enrollment must run even while the module is locked: a lock
    // which arrives mid enroll lets the running composite finish, and skipping the finalization would
    // leak enrollActive and the scanner ownership
    processEnrollResult();

    // the same holds for the two asynchronous sync template transfers. Their finalization writes
    // flash and group objects, resets the LEDs and hands the scanner back, so it must never be gated
    // behind the lock either.
    processSyncExportResult();
    processSyncImportResult();

    // and for the queued maintenance operations, whose finalization writes flash (the new scanner
    // password), sends the delete sync broadcast, logs and hands the scanner back
    processMaintenanceResult();

    // highest priority scanner activity. The priority order (SyncReceive import > Enroll > SyncSend
    // export > Maintenance > Scan > HealthCheck) is realized through the order of the triggers in this
    // loop. A received template also keeps the whole sync path blocked while it waits, so it goes
    // first.
    processSyncImportStart();

    if (!isLocked)
    {
        if (enrollRequestedFingerTimer > 0 && delayCheck(enrollRequestedFingerTimer, ENROLL_REQUEST_DELAY) &&
            !enrollActive)
        {
            // the request timer stays armed for the whole enrollment (the ETS wait poll reads it)
            // and also while the scanner is still owned by another activity, so the trigger simply
            // re-fires on the next pass
            startEnrollFinger(enrollRequestedFingerLocation);
        }

        if (shutdownSensorTimer > 0 && delayCheck(shutdownSensorTimer, SHUTDOWN_SENSOR_DELAY))
        {
            switchFingerprintPower(false);
            shutdownSensorTimer = 0;
        }

        if (initResetTimer > 0 && delayCheck(initResetTimer, INIT_RESET_TIMEOUT))
        {
            finger.setLed(FingerprintInterface::None);
            switchLedGreenPower(false);

            if (ParamACC_ScanMode == 0)
                switchFingerprintPower(false);

            initResetTimer = 0;
        }

        // a timer which is still armed from the scan before must not overwrite the LED sequence of a
        // running enrollment, template transfer or maintenance operation: they all show "Busy" for
        // their whole run and re-arm the timer in their own finalization. syncImportPending belongs
        // into the set as well - a received template which waits for the scanner already shows "Busy".
        if (resetFingerLedTimer > 0 && !enrollActive && !syncExportActive && !syncImportPending &&
            !syncImportActive && !maintActive &&
            delayCheck(resetFingerLedTimer, LED_RESET_TIMEOUT))
        {
            resetRingLed();
            resetFingerLedTimer = 0;
        }

        if (resetTouchPcbLedTimer > 0 && delayCheck(resetTouchPcbLedTimer, LED_RESET_TIMEOUT))
        {
            switchLedGreenPower(false);
            switchLedRedPower(false);
            resetTouchPcbLedTimer = 0;
        }

        if (resetTouchPcbLedTimerFast > 0 && delayCheck(resetTouchPcbLedTimerFast, LED_RESET_FAST_TIMEOUT))
        {
            switchLedGreenPower(false);
            switchLedRedPower(false);
            resetTouchPcbLedTimerFast = 0;
        }

        for (uint16_t i = 0; i < ParamACC_VisibleActions; i++)
            _channels[i]->loop();
    }

    // absolute bound for an incoming broadcast which never completes: a sender which is power cycled or
    // reprogrammed mid transfer simply stops sending data packets, and without this the assembly state
    // would keep the whole sync path blocked until the next control packet happens to arrive. Deliberately
    // outside the lock gate and in front of the export triggers below, so a recovered device can send its
    // own pending broadcast in the very same pass. A reception which reached the template import is
    // bounded by SYNC_IMPORT_START_TIMEOUT instead, hence the two import flags.
    if (syncReceiveLastPacketTimer > 0 && syncReceiving && !syncImportPending && !syncImportActive &&
        delayCheck(syncReceiveLastPacketTimer, SYNC_RECEIVE_PACKET_TIMEOUT))
    {
        logWarningP("Sync-Receive (syncType=%u): aborted, no further packet within %u ms (%u/%u packets received)",
                    syncReceiveType, (uint32_t)SYNC_RECEIVE_PACKET_TIMEOUT, syncReceivePacketReceivedCount, syncReceivePacketCount);

        // only a finger transfer has a ring LED to report on; the assembly state itself is dropped for
        // every type, exactly like a failed checksum does
        if (syncReceiveType == SyncType::FINGER)
        {
            finger.setLed(FingerprintInterface::Failed);
            resetFingerLedTimer = delayTimerInit();
        }

        syncReceiving = false;
        syncReceiveLastPacketTimer = 0;
    }

    // the sync export triggers. They sit after the enrollment trigger and before the scan pipeline so
    // that the arbiter hands the scanner out in the priority order documented above. startSyncSend()
    // only reports whether the request was consumed - a scanner which is owned by another activity or
    // a broadcast which is still dribbling keeps the request armed and is retried.
    if (syncRequestedFingerTimer > 0 && delayCheck(syncRequestedFingerTimer, SYNC_AFTER_ENROLL_DELAY))
    {
        if (startSyncSend(SyncType::FINGER, syncRequestedFingerId))
        {
            syncRequestedFingerTimer = 0;
            syncRequestedFingerId = 0;
        }
    }

    if (syncRequestedNfcTimer > 0 && delayCheck(syncRequestedNfcTimer, SYNC_AFTER_ENROLL_DELAY))
    {
        if (startSyncSend(SyncType::NFC, syncRequestedNfcId))
        {
            syncRequestedNfcTimer = 0;
            syncRequestedNfcId = 0;
        }
    }

    // the keypad broadcast, armed by the two ETS handlers (change/sync keypad code). Like the NFC one
    // it is pure flash plus compression and needs no scanner at all, so startSyncSend() only ever
    // defers it while another sync is still in flight - which is exactly the retry the return value
    // asks for.
    if (syncRequestedKeyTimer > 0 && delayCheck(syncRequestedKeyTimer, SYNC_AFTER_ENROLL_DELAY))
    {
        if (startSyncSend(SyncType::KEY, syncRequestedKeyId))
        {
            syncRequestedKeyTimer = 0;
            syncRequestedKeyId = 0;
        }
    }

    // the queued maintenance operations. They sit behind the import, the enrollment and the export
    // triggers and in front of the scan pipeline (import > enroll > export > maintenance > scan >
    // health). Deliberately outside the lock gate: the ETS function properties and the received delete
    // broadcasts are processed while the module is locked as well.
    processMaintenanceStart();

    if (!isLocked)
    {
        // the scan pipeline is the opportunistic user of the scanner, so it asks the arbiter after the
        // import, the enrollment and the export triggers did - asking first would let a scan postpone
        // an enrollment or a broadcast for a whole scan cycle
        processScanStateMachine();

        if (checkSensorTimer > 0 && delayCheck(checkSensorTimer, CHECK_SENSOR_DELAY))
        {
            // the health check is asynchronous as well; a tick which meets a busy driver or a scan
            // in flight is simply dropped
            if (fpPower == FpPowerState::Ready &&
                fpOwner == FpOwner::None &&
                !finger.isBusy())
            {
                finger.startHealthCheck([this](const FpResult &result) {
                    bool success = result.ok();
                    bool currentStatus = KoACC_FingerScannerStatus.value(DPT_Switch);
                    if (currentStatus != success)
                    {
                        KoACC_FingerScannerStatus.value(success, DPT_Switch);
                        logInfoP("Check scanner status: %u", success);
                    }
                });
            }

            checkSensorTimer = delayTimerInit();
        }
    }

    if (ParamACC_NfcScanner == 2)
    {
        touchLeftTouched = openknx.gpio.digitalRead(EXTERN_TOUCH_LEFT_PIN) == HIGH;
        touchRightTouched = openknx.gpio.digitalRead(EXTERN_TOUCH_RIGHT_PIN) == HIGH;
    }

    if ((bool)KoACC_TouchPcbButtonLeft.value(DPT_Switch) != touchLeftTouched)
    {
        logDebugP("Left touch button touched=%u.", touchLeftTouched);
        KoACC_TouchPcbButtonLeft.value(touchLeftTouched, DPT_Switch);
    }
    if ((bool)KoACC_TouchPcbButtonRight.value(DPT_Switch) != touchRightTouched)
    {
        logDebugP("Right touch button touched=%u.", touchRightTouched);
        KoACC_TouchPcbButtonRight.value(touchRightTouched, DPT_Switch);
    }

    processSyncSend();
    loopNfc();
    keypadBase->loop();
    // too long pause between 2 keypress
    if (keypadLastKeypressTimer > 0 && delayCheck(keypadLastKeypressTimer, ParamACC_KeypressDelayTimeMS))
        clearKeypadBuffer(KeypadBase::FeedbackType::PauseExceeded);


}

void AccessControl::loopNfc(bool testMode)
{
    if (!testMode &&
        ParamACC_NfcScanner == 0)
        return;

    if (enrollNfcStarted > 0)
    {
        if (delayCheck(enrollNfcStarted, NFC_ENROLL_TIMEOUT))
        {
            logInfoP("Enrolling NFC tag failed.");

            //###ToDo: remote management status feedback

            switchLedRedPower(true);
            resetTouchPcbLedTimer = delayTimerInit();

            enrollNfcStarted = 0;
            enrollNfcId = ACC_ID_INVALID;
        }

        if (delayCheck(enrollNfcLedLastChanged, NFC_ENROLL_LED_BLINK_INTERVAL))
        {
            enrollNfcLedOn = !enrollNfcLedOn;
            switchLedGreenPower(enrollNfcLedOn ? HIGH : LOW);

            enrollNfcLedLastChanged = delayTimerInit();
        }
    }

    nci::run();

    uint8_t uniqueIdLength;
    const uint8_t* uniqueId;
    tagStatus currentTagStatus = nci::getTagStatus();
    switch (currentTagStatus) {
        case tagStatus::foundNew:
            logDebugP("New tag detected:");
            logIndentUp();
            
            uniqueIdLength = nci::tagData.getUniqueIdLength();
            uniqueId = nci::tagData.getUniqueId();

            logDebugP("uniqueID (length=%d):", uniqueIdLength);
            for (uint8_t index = 0; index < uniqueIdLength; index++)
                logDebugP("0x%02X ", uniqueId[index]);

            if (!testMode)
            {
                if (enrollNfcStarted > 0)
                {
                    uint32_t storageOffset = 0;
                    uint8_t tagUid[10] = {};
                    for (uint16_t i = 0; i < MAX_NFCS; i++)
                    {
                        storageOffset = ACC_CalcNfcStorageOffset(i);
                        _nfcStorage.read(storageOffset, tagUid, 10);
                        if (!memcmp(tagUid, uniqueId, 10))
                        {
                            enrollNfcDuplicateId = i;
                            logInfoP("Not enrolled as unique tag ID already present in nfcID %u.", enrollNfcDuplicateId);
                            break;
                        }
                    }

                    if (enrollNfcDuplicateId == ACC_ID_INVALID)
                    {
                        storageOffset = ACC_CalcNfcStorageOffset(enrollNfcId);
                        logDebugP("storageOffset: %d", storageOffset);
                        _nfcStorage.write(storageOffset, const_cast<uint8_t*>(uniqueId), uniqueIdLength);
                        _nfcStorage.commit();

                        logInfoP("Enrolled to nfcID %u.", enrollNfcId);
                
                        //###ToDo: remote management status feedback

                        switchLedGreenPower(true);
                    }
                    else
                    {
                        //###ToDo: remote management status feedback

                        switchLedRedPower(true);
                    }

                    resetTouchPcbLedTimer = delayTimerInit();
                    enrollNfcStarted = 0;
                }
                else
                {
                    uint32_t storageOffset = 0;
                    uint8_t tagUid[10] = {};
                    bool found = false;
                    uint16_t foundId = 0;
                    for (uint16_t nfcId = 0; nfcId < MAX_NFCS; nfcId++)
                    {
                        storageOffset = ACC_CalcNfcStorageOffset(nfcId);
                        _nfcStorage.read(storageOffset, tagUid, 10);
                        if (!memcmp(tagUid, uniqueId, uniqueIdLength))
                        {
                            found = true;
                            foundId = nfcId;
                            break;
                        }
                    }

                    if (found)
                    {
                        logDebugP("Tag found (id=%u)", foundId);
                        processNfcScanSuccess(foundId);
                    }
                    else
                    {
                        logInfoP("Tag not found");
                        KoACC_NfcScanSuccess.value(false, DPT_Switch);
                
                        sendScanAccessData(SyncType::NFC, false);
                
                        // if NFC tag present, but scan failed, reset all authentication action calls
                        for (uint16_t i = 0; i < ParamACC_VisibleActions; i++)
                            _channels[i]->resetActionCall();

                        switchLedRedPower(true);
                        resetTouchPcbLedTimer = delayTimerInit();
                    }
                }
            }
            else
                testModeNfcFound = true;

            logIndentDown();
            break;
        case tagStatus::removed:
            logDebugP("Tag removed.");
            break;
    }

    nciState currentNciState = nci::getState();
    if (currentNciState == nciState::error)
    {
        nci::reset();
        logDebugP("NCI reset.");
    }
}

void AccessControl::processNfcScanSuccess(uint16_t foundId, bool external)
{
    KoACC_NfcScanSuccess.value(true, DPT_Switch);
    KoACC_NfcScanSuccessId.value(foundId, Dpt(7, 1));

    sendScanAccessData(SyncType::NFC, true, foundId);

    bool actionExecuted = false;
    for (size_t i = 0; i < ParamNFCACT_NfcActionCount; i++)
    {
        uint16_t nfcId = knx.paramWord(NFCACT_FaNfcId + NFCACT_ParamBlockOffset + i * NFCACT_ParamBlockSize);
        if (nfcId == foundId)
        {
            uint16_t actionId = knx.paramWord(NFCACT_FaActionId + NFCACT_ParamBlockOffset + i * NFCACT_ParamBlockSize) - 1;
            if (actionId < ACC_VisibleActions)
                actionExecuted |= _channels[actionId]->processScan(foundId);
            else
                logInfoP("Invalid ActionId: %d", actionId);
        }
    }

    if (actionExecuted)
    {
        if (!external)
        {
            switchLedGreenPower(true);
            resetTouchPcbLedTimer = delayTimerInit();
        }
    }
    else
    {
        if (!external)
        {
            switchLedGreenPower(true);
            resetTouchPcbLedTimerFast = delayTimerInit();
        }
    }
}

void AccessControl::sendScanAccessData(SyncType syncType, bool success, uint16_t foundId)
{
    KoACC_ScanAccessData.valueNoSend(foundId, Dpt(15, 0, 0));   // access identification code
    KoACC_ScanAccessData.valueNoSend(false, Dpt(15, 0, 1));     // detection error
    KoACC_ScanAccessData.valueNoSend(success, Dpt(15, 0, 2));   // permission accepted
    KoACC_ScanAccessData.valueNoSend(false, Dpt(15, 0, 3));     // read direction (not used)
    KoACC_ScanAccessData.valueNoSend(false, Dpt(15, 0, 4));     // encryption (not used for now)
    KoACC_ScanAccessData.value(syncType, Dpt(15, 0, 5));        // index of access identification code (used as type)
}

// The asynchronous scan pipeline: one driver operation is in flight at a time, its result is latched
// by the completion callback and evaluated here in the next loop pass. Touch mode re-issues the search
// within a CAPTURE_RETRIES_TOUCH_TIMEOUT window, continuous mode polls every 100 ms.
void AccessControl::processScanStateMachine()
{
    if (ParamACC_ScanMode == 0)
    {
        // ---- touch mode ----------------------------------------------------------------
        switch (scanState)
        {
            case ScanState::Idle:
                if (touched)
                {
                    if (fpPower != FpPowerState::Ready &&
                        fpPower != FpPowerState::Booting)
                    {
                        // the scanner is neither up nor coming up: the bring-up is kicked off and the
                        // touch is consumed and lost - keeping it pending would arm the flag forever
                        // for a disabled or dead scanner. KoACC_FingerTouched is written anyway and
                        // the removal branch below completes the pulse on one of the next passes (it
                        // clears the group object as soon as the scanner is not ready), so a dead or
                        // disabled scanner reports the touch instead of swallowing it.
                        KoACC_FingerTouched.value(true, DPT_Switch);
                        switchFingerprintPower(true);
                        touched = false;
                        logDebugP("Touch dropped (power=%u)", (uint8_t)fpPower);
                    }
                    else if (arbiterAcquire(FpOwner::Scan))
                    {
                        logInfoP("Touched");
                        // KoACC_FingerTouched has to be written before the first scanner operation is
                        // started (this group object ordering is part of the external behaviour)
                        KoACC_FingerTouched.value(true, DPT_Switch);
                        touched = false;
                        touchDeferLogTimer = 0;

                        if (startScanSearch())
                        {
                            scanWindowStart = delayTimerInit();
                            scanState = ScanState::Scanning;
                        }
                        else
                        {
                            arbiterRelease(FpOwner::Scan);
                            logDebugP("Touch scan not started (power=%u, busy=%u)", (uint8_t)fpPower, finger.isBusy());
                        }
                    }
                    else if (touchDeferLogTimer == 0 || delayCheck(touchDeferLogTimer, FP_DEFER_LOG_DELAY))
                    {
                        // another activity owns the scanner (e.g. a running enrollment): the touch
                        // stays pending and is served as soon as the owner is through
                        touchDeferLogTimer = delayTimerInit();
                        logDebugP("Touch deferred, scanner busy (owner=%u, power=%u, busy=%u)",
                                  (uint8_t)fpOwner, (uint8_t)fpPower, finger.isBusy());
                    }
                }
                else if (KoACC_FingerTouched.value(DPT_Switch))
                {
                    // the touched KO is still set although no window is open (e.g. a touch which
                    // could not be served): probe for the finger being removed
                    if (fpPower != FpPowerState::Ready)
                        finishTouchedRemoval();
                    else
                    {
                        // the arbiter is not taken here, WaitRemove acquires it per probe and
                        // releases it in between
                        scanRemoveProbeTimer = 0;
                        scanRemoveProbing = false;
                        scanState = ScanState::WaitRemove;
                    }
                }
                break;

            case ScanState::Scanning:
                if (!scanDone)
                    break;

                scanDone = false;
                processScanResult(scanResult);

                // no finger seen yet and the capture window is still open: re-issue the search
                if (scanResult.status == FpStatus::NoFinger &&
                    !delayCheck(scanWindowStart, CAPTURE_RETRIES_TOUCH_TIMEOUT) &&
                    startScanSearch())
                    break;

                // window over (or a scan concluded): watch for the finger being taken off, which
                // clears the touched KO and arms the shutdown timer
                // the scanner is handed back either way - WaitRemove re-acquires it for every single
                // probe
                if (KoACC_FingerTouched.value(DPT_Switch))
                {
                    scanRemoveProbeTimer = 0;
                    scanRemoveProbing = false;
                    scanState = ScanState::WaitRemove;
                }
                else
                    scanState = ScanState::Idle;

                arbiterRelease(FpOwner::Scan);
                break;

            // the removal wait holds FpOwner::Scan only while one probe is in flight and hands it back
            // in between. Holding it for as long as a finger rests on the sensor would starve every
            // other scanner activity (an enrollment would fail after its start timeout, a received
            // template and the queued maintenance operations after 90 s, the health check would be
            // dropped pass after pass). A tick which does not get the arbiter simply retries on the
            // next one, the touched group object stays true meanwhile.
            case ScanState::WaitRemove:
                if (scanRemoveProbing)
                {
                    // a probe is on the wire, its result is what the state waits for
                    if (!scanDone)
                        break;

                    scanDone = false;
                    scanRemoveProbing = false;
                    arbiterRelease(FpOwner::Scan);

                    if (scanResult.status != FpStatus::Ok)
                    {
                        // no finger on the sensor anymore (every non-OK answer counts as "no finger")
                        finishTouchedRemoval();
                        scanState = ScanState::Idle;
                        break;
                    }

                    // still present, probe again after the pacing interval
                    scanRemoveProbeTimer = delayTimerInit();
                    break;
                }

                // nothing of ours is in flight and the scanner belongs to whoever needs it
                if (scanRemoveProbeTimer > 0 && !delayCheck(scanRemoveProbeTimer, SCAN_REMOVE_PROBE_DELAY))
                    break;

                if (fpPower != FpPowerState::Ready)
                {
                    // the scanner cannot be asked anymore (powered off, failed): give up on the
                    // removal window
                    finishTouchedRemoval();
                    scanState = ScanState::Idle;
                    break;
                }

                if (!arbiterAcquire(FpOwner::Scan))
                {
                    // another activity owns the scanner: retry on the next tick, the touched group
                    // object stays true until the finger really is gone
                    if (touchDeferLogTimer == 0 || delayCheck(touchDeferLogTimer, FP_DEFER_LOG_DELAY))
                    {
                        touchDeferLogTimer = delayTimerInit();
                        logDebugP("Finger removal probe deferred, scanner owned by %u (power=%u, busy=%u)",
                                  (uint8_t)fpOwner, (uint8_t)fpPower, finger.isBusy());
                    }

                    break;
                }

                touchDeferLogTimer = 0;
                if (!startScanRemoveProbe())
                {
                    // the driver refuses the probe: give up on the removal window
                    arbiterRelease(FpOwner::Scan);
                    finishTouchedRemoval();
                    scanState = ScanState::Idle;
                    break;
                }

                scanRemoveProbing = true;
                break;
        }

        return;
    }

    // ---- continuous mode ---------------------------------------------------------------
    switch (scanState)
    {
        case ScanState::Idle:
            if (searchForFingerDelayTimer == 0 || delayCheck(searchForFingerDelayTimer, 100))
            {
                if (arbiterAcquire(FpOwner::Scan) &&
                    startScanSearch())
                    scanState = ScanState::Scanning;
                else
                {
                    // scanner still booting, disabled or busy with a foreign operation: retry
                    // with the normal poll cadence instead of on every loop pass
                    if (fpOwner == FpOwner::Scan)
                        arbiterRelease(FpOwner::Scan);

                    searchForFingerDelayTimer = delayTimerInit();
                }
            }
            break;

        case ScanState::Scanning:
            if (!scanDone)
                break;

            scanDone = false;
            processScanResult(scanResult);

            // the poll interval is restarted at completion, not at the start of the scan - otherwise
            // the effective poll rate would double
            searchForFingerDelayTimer = delayTimerInit();
            scanState = ScanState::Idle;
            arbiterRelease(FpOwner::Scan);
            break;

        case ScanState::WaitRemove:
            // continuous mode has no removal window, the touched KO is tracked by the scan result.
            // Only reachable when the scan mode changed underneath a running removal wait; that wait
            // may not own the scanner anymore, so the release is guarded.
            scanState = ScanState::Idle;
            scanRemoveProbing = false;
            if (fpOwner == FpOwner::Scan)
                arbiterRelease(FpOwner::Scan);
            break;
    }
}

// GenImg -> LED ScanFinger -> GenChar(1) -> Search as one driver operation
bool AccessControl::startScanSearch()
{
    scanDone = false;
    scanResult = {};

    if (finger.startSearchFinger([this](const FpResult &result) {
            // callbacks only latch, the next operation is always started from loop()
            scanResult = result;
            scanDone = true;
        }))
        return true;

    logDebugP("Finger search rejected by the driver (%s)", fpStatusText(finger.lastStartError()));
    return false;
}

// single GenImg presence probe, used by the finger removal detection
bool AccessControl::startScanRemoveProbe()
{
    scanDone = false;
    scanResult = {};

    if (finger.startDetectFinger([this](const FpResult &result) {
            scanResult = result;
            scanDone = true;
        }))
        return true;

    logDebugP("Finger detection rejected by the driver (%s)", fpStatusText(finger.lastStartError()));
    return false;
}

void AccessControl::finishTouchedRemoval()
{
    KoACC_FingerTouched.value(false, DPT_Switch);
    shutdownSensorTimer = delayTimerInit();
}

// Evaluation of one completed scan: the driver already ran capture, feature extraction and library
// search, this is the group object, action channel and LED side of the result. Returns true when the
// sensor saw a finger.
bool AccessControl::processScanResult(const FpResult &result)
{
    bool continuousMode = ParamACC_ScanMode == 1;

    // every answer which is not a completed scan is treated as "no finger". Deliberate: a transport
    // error (timeout, checksum, framing) follows this path instead of the no-match cascade below,
    // because a single UART glitch must not reset the authentication action calls of every channel -
    // the auth window is closed by ParamACC_AuthDelayTimeMS anyway, while a spurious reset would drop
    // a legitimately started multi factor authentication.
    if (result.status != FpStatus::Ok &&
        result.status != FpStatus::NoMatch &&
        result.status != FpStatus::NotFound)
    {
        if (result.status != FpStatus::NoFinger)
            logDebugP("Finger scan failed: %s (0x%02X)", fpStatusText(result.status), result.rawConfirmation);

        if (continuousMode &&
            KoACC_FingerTouched.value(DPT_Switch))
            KoACC_FingerTouched.value(false, DPT_Switch);

        hasLastFoundLocation = false;
        return false;
    }

    if (continuousMode &&
        !KoACC_FingerTouched.value(DPT_Switch))
        KoACC_FingerTouched.value(true, DPT_Switch);

    if (result.status == FpStatus::Ok)
    {
        logDebugP("Match #%d with confidence %d", result.location, result.score);

        if (continuousMode &&
            hasLastFoundLocation && lastFoundLocation == result.location)
        {
            logDebugP("Same finger found in location %d and ignored", result.location);
            resetFingerLedTimer = delayTimerInit();
            return true;
        }

        logInfoP("Finger found in location %d", result.location);
        processFingerScanSuccess(result.location);

        hasLastFoundLocation = true;
        lastFoundLocation = result.location;
    }
    else
    {
        logDebugP("No match with confidence %d", result.score);

        hasLastFoundLocation = false;
        finger.setLed(FingerprintInterface::ScanNoMatch);

        logInfoP("Finger not found");
        KoACC_FingerScanSuccess.value(false, DPT_Switch);

        sendScanAccessData(SyncType::FINGER, false);

        // if finger present, but scan failed, reset all authentication action calls
        for (uint16_t i = 0; i < ParamACC_VisibleActions; i++)
            _channels[i]->resetActionCall();
    }

    resetFingerLedTimer = delayTimerInit();
    return true;
}

// The LED ring group objects default to color 0, which the driver rejects as an unsupported color.
// The sensor ignores the color for the "always off" control, so white is substituted - the LED really
// turns off.
void AccessControl::setLedRingRaw(uint8_t color, uint8_t control, uint8_t speed, uint8_t count)
{
    if (!finger.supportsLedColor(color))
        color = (uint8_t)FpLedColor::White;

    // note the argument order of the driver: control, speed, color, count
    finger.setLedRaw(control, speed, color, count);
}

void AccessControl::resetRingLed()
{
    setLedRingRaw(KoACC_FingerLedRingColor.value(Dpt(5, 10)), KoACC_FingerLedRingControl.value(Dpt(5, 10)), KoACC_FingerLedRingSpeed.value(Dpt(5, 10)), KoACC_FingerLedRingCount.value(Dpt(5, 10)));
    logInfoP("LED ring: color=%u, control=%u, speed=%u, count=%u", (uint8_t)KoACC_FingerLedRingColor.value(Dpt(5, 10)), (uint8_t)KoACC_FingerLedRingControl.value(Dpt(5, 10)), (uint8_t)KoACC_FingerLedRingSpeed.value(Dpt(5, 10)), (uint8_t)KoACC_FingerLedRingCount.value(Dpt(5, 10)));
}

void AccessControl::processFingerScanSuccess(uint16_t location, bool external)
{
    KoACC_FingerScanSuccess.value(true, DPT_Switch);
    KoACC_FingerScanSuccessId.value(location, Dpt(7, 1));

    sendScanAccessData(SyncType::FINGER, true, location);

    bool actionExecuted = false;
    for (size_t i = 0; i < ParamFINACT_FingerActionCount; i++)
    {
        uint16_t fingerId = knx.paramWord(FINACT_FaFingerId + FINACT_ParamBlockOffset + i * FINACT_ParamBlockSize);
        if (fingerId == location)
        {
            uint16_t actionId = knx.paramWord(FINACT_FaActionId + FINACT_ParamBlockOffset + i * FINACT_ParamBlockSize) - 1;
            if (actionId < ACC_VisibleActions)
                actionExecuted |= _channels[actionId]->processScan(location);
            else
                logInfoP("Invalid ActionId: %d", actionId);
        }
    }

    if (actionExecuted)
    {
        if (!external)
            finger.setLed(FingerprintInterface::ScanMatch);
    }
    else
    {
        if (!external)
            finger.setLed(FingerprintInterface::ScanMatchNoAction);

        KoACC_FingerTouchedNoAction.value(true, DPT_Switch);
    }
}

// Kicks off the asynchronous enrollment composite of the driver (6 captures, RegModel, Store,
// index update and the whole LED sequence). Nothing is waited for here, the completion is latched
// by the callback and evaluated by processEnrollResult() from loop(), so KNX stays fully
// responsive for the whole enrollment.
bool AccessControl::startEnrollFinger(uint16_t location)
{
    // the scanner has to be powered up first; the request waits for the asynchronous bring-up. A
    // scanner which is not there at all (ETS "Kein Fingerprint", power-on rejected) fails the request
    // immediately instead of leaving it armed.
    if (fpPower != FpPowerState::Ready &&
        fpPower != FpPowerState::Booting &&
        !switchFingerprintPower(true))
    {
        logInfoP("Enroll request:");
        logIndentUp();
        logInfoP("Fingerprint scanner not available (power=%u).", (uint8_t)fpPower);
        logIndentDown();

        // enrollActive stays false for a request which never started a driver composite - it gates the
        // progress the ETS wait poll reads, and enrollProgress() would still answer the value of the
        // previous run. The finalization owns all the remaining bookkeeping.
        enrollActiveLocation = location;
        enrollResult = FpStatus::ErrNotReady;
        enrollSuccess = false;
        enrollDone = true;
        return false;
    }

    if (!arbiterAcquire(FpOwner::Enroll))
    {
        // the scan pipeline, a health check or the bring-up still holds the scanner: the request
        // timer stays armed on purpose and the trigger re-fires on the next pass. The wait is bounded
        // by the ENROLL_START_TIMEOUT check in loop(), which also covers a request that was armed
        // while the module is locked and therefore never reaches this function at all.
        if (enrollDeferLogTimer == 0 || delayCheck(enrollDeferLogTimer, FP_DEFER_LOG_DELAY))
        {
            enrollDeferLogTimer = delayTimerInit();
            logDebugP("Enroll request delayed, scanner busy (owner=%u, power=%u, busy=%u)",
                      (uint8_t)fpOwner, (uint8_t)fpPower, finger.isBusy());
        }

        return false;
    }

    enrollDeferLogTimer = 0;
    logInfoP("Enroll request:");
    logIndentUp();
    logInfoP("Enrolling to location %d.", location);

    enrollActiveLocation = location;
    enrollDone = false;
    enrollSuccess = false;
    enrollResult = FpStatus::Ok;

    // the progress callback exists for the driver internals only: it already keeps enrollProgress()
    // (read by the ETS wait poll) and drives the complete LED sequence, so nothing is mirrored here
    if (finger.startEnroll(
            location,
            nullptr,
            [this](const FpResult &result) {
                // callbacks only latch, all finalization runs in processEnrollResult()
                enrollResult = result.status;
                enrollSuccess = result.ok();
                enrollDone = true;
            }))
    {
        // set only for a run the driver really accepted (it zeroes its progress there), so the ETS wait
        // poll can never read the progress of a previous enrollment
        enrollActive = true;
    }
    else
    {
        logErrorP("Enroll rejected by the driver (%s)", fpStatusText(finger.lastStartError()));

        enrollResult = finger.lastStartError();
        enrollSuccess = false;
        enrollDone = true;
    }

    logIndentDown();
    return !enrollDone;
}

// Finalization of an asynchronous enrollment, called from loop() outside the lock gate: the log lines,
// the sync broadcast arming, the LED reset timer and the release of the scanner ownership.
void AccessControl::processEnrollResult()
{
    if (!enrollDone)
        return;

    enrollDone = false;

    logIndentUp();
    if (enrollSuccess)
    {
        logInfoP("Enrolled to location %d.", enrollActiveLocation);

        //###ToDo: remote management status feedback

        // armed as soon as the enrollment succeeded; the ETS wait poll reports success while this
        // timer is pending
        syncRequestedFingerId = enrollActiveLocation;
        syncRequestedFingerTimer = delayTimerInit();
    }
    else
    {
        logInfoP("Enrolling template failed.");
        logIndentUp();
        // the reason is spelled out because both finger waits of the driver are bounded: a finger which
        // is never placed on or never taken off the sensor fails the enrollment with a timeout (the
        // driver logs which of the two waits ran out)
        logInfoP("Reason: %s (0x%02X)", fpStatusText(enrollResult), (uint8_t)enrollResult);
        logIndentDown();

        //###ToDo: remote management status feedback
    }
    logIndentDown();

    // the Success/Failed LED is part of the driver composite, only the ring LED reset is re-armed here
    resetFingerLedTimer = delayTimerInit();

    // a lock which arrived during the enrollment has already applied its LED, which the composite
    // overwrote with Success/Failed afterwards; it is re-applied so the visible state matches the
    // locked module again (the LED reset timer above is gated behind !isLocked)
    if (isLocked)
        finger.setLed(FingerprintInterface::Locked);

    // last wins: an enrollment requested for another location while this one was running (group
    // object or ETS) has re-armed the request fields, so they are kept and the trigger starts the
    // next enrollment on the following pass
    if (enrollRequestedFingerLocation == enrollActiveLocation)
    {
        enrollRequestedFingerTimer = 0;
        enrollRequestedFingerLocation = 0;
    }
    else
        logInfoP("Enroll re-requested for location %d while location %d was running", enrollRequestedFingerLocation, enrollActiveLocation);

    enrollActive = false;
    // not acquired at all when the request failed before the arbiter was asked
    if (fpOwner == FpOwner::Enroll)
        arbiterRelease(FpOwner::Enroll);
}

// True while an ETS maintenance operation is queued or in flight. The ETS handlers answer their
// function property optimistically, so a second request has to be refused with the failure code.
bool AccessControl::maintenanceBusy() const
{
    return maintPending != MaintOp::None || maintActive;
}

// A delete which arrived as a sync broadcast from another device. Nobody is waiting for an answer, so
// it is never executed inline (a scanner operation must not run in the group object callback) and never
// dropped either: it is queued and drained by processMaintenanceStart() from loop().
void AccessControl::queueBusDelete(uint16_t location)
{
    if (busDeleteQueueCount >= BUS_DELETE_QUEUE_SIZE)
    {
        logErrorP("Sync-Receive (delete finger): queue full, fingerId=%u dropped", location);
        return;
    }

    busDeleteQueue[(busDeleteQueueHead + busDeleteQueueCount) % BUS_DELETE_QUEUE_SIZE] = location;
    if (busDeleteQueueCount == 0)
        busDeletePendingTimer = delayTimerInit();
    busDeleteQueueCount++;
}

// The arbiter side of the queued maintenance operations. Called from loop() after the sync import, the
// enrollment and the sync export triggers and before the scan pipeline, so the priority order
// (import > enroll > export > maintenance > scan > health) is realized by position.
void AccessControl::processMaintenanceStart()
{
    if (maintActive)
        return;

    bool hasBusDelete = busDeleteQueueCount > 0;
    if (!hasBusDelete && maintPending == MaintOp::None)
        return;

    // sampled BEFORE the bring-up is kicked off: switchFingerprintPower(true) turns a Failed scanner
    // into Booting, so a test afterwards could never see Failed and a queued operation would keep
    // re-triggering the bring-up for the whole MAINT_START_TIMEOUT window (about 16 attempts over 90 s)
    // instead of failing right away. Mirrors the reachable test in processSyncImportStart().
    bool wasFailed = fpPower == FpPowerState::Failed;

    // the scanner is brought up if it is not up yet; a scanner which cannot be powered at all (ETS
    // "Kein Fingerprint") fails the request right away instead of keeping it queued
    bool available = fpPower == FpPowerState::Ready ||
                     fpPower == FpPowerState::Booting ||
                     switchFingerprintPower(true);

    if (available && arbiterAcquire(FpOwner::Maintenance))
    {
        maintDeferLogTimer = 0;
        busDeleteDeferLogTimer = 0;

        if (hasBusDelete)
        {
            uint16_t location = busDeleteQueue[busDeleteQueueHead];
            busDeleteQueueHead = (busDeleteQueueHead + 1) % BUS_DELETE_QUEUE_SIZE;
            busDeleteQueueCount--;
            busDeletePendingTimer = delayTimerInit();

            // a delete which came in over the bus is never broadcast again
            startMaintenanceOp(MaintOp::DeleteFinger, location, false);
            return;
        }

        MaintOp op = maintPending;
        maintPending = MaintOp::None;
        startMaintenanceOp(op, maintDeleteLocation, maintDeleteSendSync);
        return;
    }

    // the scanner is owned by another activity (a running enrollment holds it for up to a minute) or
    // it never becomes ready: the requests stay queued and are retried on the next pass, bounded so
    // a dead scanner cannot block the queue forever
    bool giveUp = !available || wasFailed;

    if (hasBusDelete)
    {
        if (giveUp || delayCheck(busDeletePendingTimer, MAINT_START_TIMEOUT))
        {
            uint16_t location = busDeleteQueue[busDeleteQueueHead];
            busDeleteQueueHead = (busDeleteQueueHead + 1) % BUS_DELETE_QUEUE_SIZE;
            busDeleteQueueCount--;
            busDeletePendingTimer = delayTimerInit();

            logInfoP("Delete request:");
            logIndentUp();
            logInfoP("Deleting template failed.");
            logIndentUp();
            logInfoP("Reason: scanner did not become available (location=%u, owner=%u, power=%u).", location, (uint8_t)fpOwner, (uint8_t)fpPower);
            logIndentDown();
            logIndentDown();
        }
        else if (busDeleteDeferLogTimer == 0 || delayCheck(busDeleteDeferLogTimer, FP_DEFER_LOG_DELAY))
        {
            busDeleteDeferLogTimer = delayTimerInit();
            logDebugP("Received delete delayed, scanner owned by %u (power=%u, busy=%u, queued=%u)",
                      (uint8_t)fpOwner, (uint8_t)fpPower, finger.isBusy(), busDeleteQueueCount);
        }
    }

    if (maintPending == MaintOp::None)
        return;

    if (giveUp || delayCheck(maintPendingTimer, MAINT_START_TIMEOUT))
    {
        // the ETS answer was already given optimistically, so this is the one place which can only
        // report the loss
        logErrorP("Maintenance operation %u dropped, the scanner did not become available (owner=%u, power=%u)",
                  (uint8_t)maintPending, (uint8_t)fpOwner, (uint8_t)fpPower);
        maintPending = MaintOp::None;
        return;
    }

    if (maintDeferLogTimer == 0 || delayCheck(maintDeferLogTimer, FP_DEFER_LOG_DELAY))
    {
        maintDeferLogTimer = delayTimerInit();
        logDebugP("Maintenance operation %u delayed, scanner owned by %u (power=%u, busy=%u)",
                  (uint8_t)maintPending, (uint8_t)fpOwner, (uint8_t)fpPower, finger.isBusy());
    }
}

// Kicks the matching driver composite off. The composites emit the whole LED sequence (Busy, then
// Success/Failed, and DeleteNotFound without any wire traffic for a location the index cache does not
// know), keep the index cache truthful and - for the password - adopt the new one for the running
// session.
bool AccessControl::startMaintenanceOp(MaintOp op, uint16_t location, bool sendSync)
{
    maintActive = true;
    maintActiveOp = op;
    maintActiveLocation = location;
    maintActiveSendSync = sendSync;
    maintDone = false;
    maintOk = false;
    maintResult = FpStatus::Ok;

    // callbacks only latch, the whole finalization runs in processMaintenanceResult()
    auto done = [this](const FpResult &result) {
        maintResult = result.status;
        maintOk = result.ok();
        maintDone = true;
    };

    bool accepted = false;
    switch (op)
    {
        case MaintOp::DeleteFinger:
            logInfoP("Delete request: deleting template from location %d.", location);
            accepted = finger.startDeleteTemplate(location, done);
            break;
        case MaintOp::EmptyDatabase:
            logInfoP("Reset scanner: emptying the finger library.");
            accepted = finger.startEmptyDatabase(done);
            break;
        case MaintOp::SetPassword:
            logInfoP("Setting new fingerprint scanner password.");
            accepted = finger.startSetPassword(maintNewPasswordCrc, done);
            break;
        default:
            logErrorP("Unsupported maintenance operation %u", (uint8_t)op);
            break;
    }

    if (!accepted)
    {
        maintResult = finger.lastStartError();
        maintOk = false;
        maintDone = true;
    }

    return accepted;
}

// Finalization of a queued maintenance operation: the log lines, the delete sync broadcast, the
// password flash write and the release of the scanner ownership. Runs from loop(), never from a driver
// callback.
void AccessControl::processMaintenanceResult()
{
    if (!maintDone)
        return;

    maintDone = false;

    switch (maintActiveOp)
    {
        case MaintOp::DeleteFinger:
            logInfoP("Delete request:");
            logIndentUp();
            if (maintOk)
            {
                logInfoP("Template deleted from location %d.", maintActiveLocation);

                //###ToDo: remote management status feedback

                // the broadcast is sent only for a template which really was deleted
                if (maintActiveSendSync)
                    startSyncDelete(SyncType::FINGER, maintActiveLocation);
            }
            else
            {
                logInfoP("Deleting template failed.");
                logIndentUp();
                logInfoP("Reason: %s (0x%02X)", fpStatusText(maintResult), (uint8_t)maintResult);
                logIndentDown();

                //###ToDo: remote management status feedback
            }
            logIndentDown();
            break;

        case MaintOp::EmptyDatabase:
            if (maintOk)
                logInfoP("Reset scanner: finger library emptied.");
            else
            {
                logErrorP("Reset scanner: emptying the finger library failed.");
                logIndentUp();
                logInfoP("Reason: %s (0x%02X)", fpStatusText(maintResult), (uint8_t)maintResult);
                logIndentDown();
            }
            break;

        case MaintOp::SetPassword:
            if (maintOk)
            {
                logInfoP("Setting new fingerprint scanner password: Success.");
                logIndentUp();
                // the flash copy is written only when the sensor confirmed the new password; the driver
                // has already adopted it for the running session, so no re-init is needed
                logDebugP("Saving new password in flash.");
                _fingerprintStorage.writeInt(FLASH_FINGER_SCANNER_PASSWORD_OFFSET, maintNewPasswordCrc);
                _fingerprintStorage.commit();
                logIndentDown();
            }
            else
            {
                logInfoP("Setting new fingerprint scanner password: Failed.");
                logIndentUp();
                logInfoP("Reason: %s (0x%02X)", fpStatusText(maintResult), (uint8_t)maintResult);
                logInfoP("The password in the flash is left untouched.");
                logIndentDown();
            }
            break;

        default:
            break;
    }

    // the Busy/Success/Failed LED sequence is part of the driver composite, only the ring LED reset
    // is re-armed here
    resetFingerLedTimer = delayTimerInit();

    maintActive = false;
    maintActiveOp = MaintOp::None;
    if (fpOwner == FpOwner::Maintenance)
        arbiterRelease(FpOwner::Maintenance);
}

bool AccessControl::deleteNfc(uint16_t nfcId, bool sync)
{
    logInfoP("Delete request:");
    logIndentUp();

    uint32_t storageOffset = ACC_CalcNfcStorageOffset(nfcId);
    logDebugP("storageOffset: %d", storageOffset);

    uint8_t emptyTest[10] = {};
    uint8_t tagUid[10] = {};
    _nfcStorage.read(storageOffset, tagUid, 10);
    
    // if tag UID empty, no tag with this ID defined
    bool success = memcmp(emptyTest, tagUid, 10);
    if (success)
    {
        char personName[28] = {}; // empty

        uint32_t storageOffset = ACC_CalcNfcStorageOffset(nfcId);
        _nfcStorage.write(storageOffset, *emptyTest, 10);
        _nfcStorage.write(storageOffset + 10, *personName, 28);
        _nfcStorage.commit();

        if (sync)
            startSyncDelete(SyncType::NFC, nfcId);
            
        //###ToDo: remote management status feedback

        switchLedGreenPower(true);
        logInfoP("NFC tag with ID %d deleted.", nfcId);
    }
    else
    {
        //###ToDo: remote management status feedback

        switchLedRedPower(true);
        logInfoP("NFC tag with ID %d not found.");
    }

    resetTouchPcbLedTimer = delayTimerInit();
    return success;
}

bool AccessControl::deleteKey(uint16_t keyId, bool sync)
{
    logInfoP("Delete request:");
    logIndentUp();

    uint32_t storageOffset = ACC_CalcKeyStorageOffset(keyId);
    logDebugP("storageOffset: %d", storageOffset);

    uint8_t emptyTest[10] = {};
    uint8_t codeUid[10] = {};
    _keypadStorage.read(storageOffset, codeUid, 10);

    // if code UID empty, no code with this ID defined
    bool success = memcmp(emptyTest, codeUid, 10);
    if (success)
    {
        char personName[28] = {}; // empty

        uint32_t storageOffset = ACC_CalcKeyStorageOffset(keyId);
        _keypadStorage.write(storageOffset, *emptyTest, 10);
        _keypadStorage.write(storageOffset + 10, *personName, 28);
        _keypadStorage.commit();

        if (sync)
            startSyncDelete(SyncType::KEY, keyId);
            
        //###ToDo: remote management status feedback

        keypadBase->setFeedback(KeypadBase::FeedbackType::Ok);
        logInfoP("Key with ID %d deleted.", keyId);
    }
    else
    {
        //###ToDo: remote management status feedback

        keypadBase->setFeedback(KeypadBase::FeedbackType::Failed);
        logInfoP("Key with ID %d not found.", keyId);
    }

    return success;
}

void AccessControl::processInputKo(GroupObject& ko)
{
    // uint16_t idReceived;

    uint16_t asap = ko.asap();
    switch (asap)
    {
        case ACC_KoLock:
            processInputKoLock(ko);
            break;
        case ACC_KoFingerLedRingColor:
        case ACC_KoFingerLedRingControl:
        case ACC_KoFingerLedRingSpeed:
        case ACC_KoFingerLedRingCount:
            resetRingLed();
            break;
        case ACC_KoTouchPcbLedRed:
        case ACC_KoTouchPcbLedGreen:
            processInputKoTouchPcbLed(ko);
            break;
        case ACC_KoSync:
            processSyncReceive(ko.valueRef());
            break;
    }

    if (isLocked)
        return;

    switch (asap)
    {
        case ACC_KoFingerEnrollNext:
        case ACC_KoFingerEnrollId:
            processInputKoEnrollFinger(ko);
            break;
        case ACC_KoNfcEnrollNext:
        case ACC_KoNfcEnrollId:
            processInputKoEnrollNfc(ko);
            break;
        case ACC_KoKeypadBacklight:
            processInputKoKeypadBacklight(ko);
        case ACC_KoKeypadLed:
            processInputKoKeypadFeedbackLed(ko);
        case ACC_KoRemoteManagementCommand:
        case ACC_KoRemoteManagementStatus:
            //###ToDo: Implement
            break;
        case ACC_KoVacInput:
        case ACC_KoVacOutput:
            //###ToDo: Implement

            // idReceived = ko.value(Dpt(7, 1));
            // logInfoP("FingerID received: %d", idReceived);

            // processFingerScanSuccess(idReceived, true);
            break;
        default:
        {
            for (uint16_t i = 0; i < ParamACC_VisibleActions; i++)
                _channels[i]->processInputKo(ko);
        }
    }
}

void AccessControl::processInputKoLock(GroupObject &ko)
{
    isLocked = ko.value(DPT_Switch);
    KoACC_LockStatus.value(isLocked, DPT_Switch);
    logInfoP("Locked: %d", isLocked);

    if (switchFingerprintPower(true))
    {
        if (isLocked)
            finger.setLed(FingerprintInterface::Locked);
        else
            resetRingLed();
    }
}

void AccessControl::processInputKoTouchPcbLed(GroupObject &ko)
{
    bool ledOn = ko.value(DPT_Switch);
    uint16_t asap = ko.asap();
    if (asap == ACC_KoTouchPcbLedRed)
        switchLedRedPower(ledOn ? HIGH : LOW);
    else if (asap == ACC_KoTouchPcbLedGreen)
        switchLedGreenPower(ledOn ? HIGH : LOW);
}

void AccessControl::processInputKoEnrollFinger(GroupObject &ko)
{
    bool success = false;
    uint16_t location = 0;
    uint16_t asap = ko.asap();
    if (asap == ACC_KoFingerEnrollNext)
    {
        // the index cache is only truthful once the bring-up published a ready scanner - asking it
        // earlier answers 0xFFFF for every library, which would arm an enrollment that could only fail
        // after its start timeout. The bring-up is kicked off (a repeated request succeeds as soon as
        // it is through) and the request itself is failed right here.
        switchFingerprintPower(true);
        success = fpPower == FpPowerState::Ready;
        if (success)
        {
            location = finger.getNextFreeLocation();
            logInfoP("Next availabe location: %d", location);
        }
        else
            logErrorP("Failed getting next available location");
    }
    else if (asap == ACC_KoFingerEnrollId)
    {
        success = true;
        location = ko.value(Dpt(7, 1));
        logInfoP("Location provided: %d", location);
    }

    if (!success)
        return;

    enrollRequestedFingerTimer = delayTimerInit();
    enrollRequestedFingerLocation = location;
}

void AccessControl::processInputKoEnrollNfc(GroupObject &ko)
{
    bool success;
    uint16_t nfcId = 0;
    uint16_t asap = ko.asap();
    if (asap == ACC_KoNfcEnrollNext)
    {
        uint32_t storageOffset = 0;
        uint8_t tagUid[10] = {};
        uint8_t emptyTest[10] = {};
        
        for (uint16_t existentId = 0; existentId < MAX_NFCS; existentId++)
        {
            storageOffset = ACC_CalcNfcStorageOffset(existentId);
            _nfcStorage.read(storageOffset, tagUid, 10);
            if (!memcmp(tagUid, emptyTest, 10))
            {
                success = true;
                nfcId = existentId;
                break;
            }
        }

        if (success)
            logInfoP("Next ID: %u", nfcId);
    }
    else if (asap == ACC_KoNfcEnrollId)
    {
        success = true;
        nfcId = ko.value(Dpt(7, 1));
        logInfoP("ID provided: %u", nfcId);
    }

    if (!success)
        return;

    enrollNfcStarted = delayTimerInit();
    enrollNfcId = nfcId;
    enrollNfcDuplicateId = ACC_ID_INVALID;
}


void AccessControl::processInputKoKeypadBacklight(GroupObject &ko)
{
    // determine DPT of keypad backlight KO
    Dpt dpt = (ParamACC_BacklightIntensity == PT_BacklightIntensity::byKO) ? DPT_DecimalFactor : DPT_Switch;
    keypadBase->setBacklight((ko.value(dpt)));
}

void AccessControl::processInputKoKeypadFeedbackLed(GroupObject &ko)
{
    // first deactivate local feedback
    keypadBase->setFeedback(KeypadBase::FeedbackType::Off);
    // determine DPT of keypad backlight KO
    uint32_t ledColor = ko.value(DPT_Colour_RGB);
    keypadBase->setInfoLed(ledColor);
}

void AccessControl::onKeypadKeyPressed(char key)
{
    bool isFirstKeypress = keypadBase->getBacklight() == 0;
    
    if (key > 0)
        logDebugP("Keypad key pressed: %c", key);
    
    // backlight is always processed on keypress
    if (ParamACC_BacklightState == PT_BacklightState::onByAnyKey)
        keypadBase->setBacklight(true);

    // send keypress event if enabled
    if (ParamACC_KeypressTrigger && key != '\0')
        KoACC_KeypadKeypress.value(true, DPT_Switch);
    
    // further processing depends on configured keys
    if (isFirstKeypress && ParamACC_KeypressIngore && ParamACC_BacklightState != PT_BacklightState::alwaysOn && ParamACC_BacklightState != PT_BacklightState::alwaysOff)
        return;

    // do we have a terminal key defined?
    char terminalKey = keypadKeymap[ParamACC_KeyTermination];
    // filter all special keys
    if (ParamACC_KeyProcessingF && terminalKey != 'F' && (key == 'F' || keypadPreviousKey == 'F'))
    {
        keypadPreviousKey = key;
        keypadBase->setFeedback(key=='F' ? KeypadBase::FeedbackType::ButtonPress : KeypadBase::FeedbackType::Off);
        KoACC_KeypadPressF.value(key=='F', DPT_Switch);
        return;
    }
    if (ParamACC_KeyProcessingB && terminalKey != 'B' && (key == 'B' || keypadPreviousKey == 'B'))
    {
        keypadPreviousKey = key;
        keypadBase->setFeedback(key=='B' ? KeypadBase::FeedbackType::ButtonPress : KeypadBase::FeedbackType::Off);
        KoACC_KeypadPressB.value(key=='B', DPT_Switch);
        return;
    }
    if (ParamACC_KeyProcessingK && terminalKey != 'K' &&  (key == 'K' || keypadPreviousKey == 'K'))
    {
        keypadPreviousKey = key;
        keypadBase->setFeedback(key=='K' ? KeypadBase::FeedbackType::ButtonPress : KeypadBase::FeedbackType::Off);
        KoACC_KeypadPressK.value(key=='K', DPT_Switch);
        return;
    }

    // is there a special delete key defined?
    if (ParamACC_KeyProcessingC)
    {
        char clearKey = terminalKey == 'C' ? '*' : 'C';
        if (key == clearKey)
        { 
            clearKeypadBuffer(KeypadBase::FeedbackType::CodeDeleted);
            return;
        }
    }

    // We process "key up" just till this point
    keypadPreviousKey = '\0';
    if (key == '\0')
        return;
    else
        keypadBase->setFeedback(KeypadBase::FeedbackType::Kepress);

    keypadLastKeypressTimer = delayTimerInit();
    if (keypadCodePosition < MAX_KEY_LEN - 1)
    {
        keypadCode[++keypadCodePosition] = key;
    }
    logInfoP("Current Keycode: %s", keypadCode);

    if (terminalKey == '\0')
    {
        checkKeypadCode(keypadCode, false);
    }
    else if (terminalKey == key || keypadCodePosition == MAX_KEY_LEN - 1)
    {
        checkKeypadCode(keypadCode, true);
    }
}

void AccessControl::clearKeypadBuffer(KeypadBase::FeedbackType feedbackType)
{
    memset(keypadCode, 0, sizeof(keypadCode));
    keypadCodePosition = -1;
    keypadLastKeypressTimer = 0;
    keypadBase->setFeedback(feedbackType);
    logInfoP("Current Keycode: <empty>");

}

bool AccessControl::checkKeypadCode(char* enteredCode, bool checkForFail)
{
    uint32_t storageOffset = 0;
    uint8_t storedCode[11] = {}; // space for 0-termination
    bool found = false;
    uint16_t foundId = 0;
    for (uint16_t codeId = 0; codeId < MAX_NFCS; codeId++)
    {
        storageOffset = ACC_CalcKeyStorageOffset(codeId);
        _keypadStorage.read(storageOffset, storedCode, 10);
        uint8_t storedLen = strlen((const char *)storedCode);
        if (strcmp((const char *)storedCode, enteredCode) && storedLen < 10 && checkForFail)
        {
            // in case strings are not exact equal we ignore the terminal character
            storedCode[storedLen] = keypadKeymap[ParamACC_KeyTermination];
        }
        if (!strcmp((const char *)storedCode, enteredCode))
        {
            found = true;
            foundId = codeId;
            break;
        }
    }

    if (found)
    {
        logDebugP("Keycode found (id=%u)", foundId);
        processKeypadScanSuccess(foundId);
    }
    else if (checkForFail)
    {
        logInfoP("Keycode not found");
        // KoACC_NfcScanSuccess.value(false, DPT_Switch);
        
        // if code failed, reset all authentication action calls
        for (uint16_t i = 0; i < ParamACC_VisibleActions; i++)
        _channels[i]->resetActionCall();
        
        clearKeypadBuffer(KeypadBase::FeedbackType::CodeUnknown);
        return false;
    }
    return true;
}

void AccessControl::processKeypadScanSuccess(uint16_t foundId, bool external)
{
    KoACC_KeypadScanSuccess.value(true, DPT_Switch);
    KoACC_KeypadScanSuccessId.value(foundId, Dpt(7, 1));

    sendScanAccessData(SyncType::KEY, true, foundId);

    bool actionExecuted = false;
    for (size_t i = 0; i < ParamKEYACT_KeypadActionCount; i++)
    {
        uint16_t codeId = knx.paramWord(KEYACT_FaCodeId + KEYACT_ParamBlockOffset + i * KEYACT_ParamBlockSize);
        if (codeId == foundId)
        {
            uint16_t actionId = knx.paramWord(KEYACT_FaActionId + KEYACT_ParamBlockOffset + i * KEYACT_ParamBlockSize) - 1;
            if (actionId < ACC_VisibleActions)
                actionExecuted |= _channels[actionId]->processScan(foundId);
            else
                logInfoP("Invalid ActionId: %d", actionId);
        }
    }

    if (!external)
        clearKeypadBuffer(actionExecuted ? KeypadBase::FeedbackType::ActionOk : KeypadBase::FeedbackType::ActionNotFound);
}

void AccessControl::startSyncDelete(SyncType syncType, uint16_t deleteId)
{
    if (!ParamACC_EnableSync ||
        syncReceiving)
        return;

    logInfoP("Sync-Send (syncType=%u): delete: deleteId=%u", syncType, deleteId);

    /*
    Sync Delete Packet Layout:
    -   0: 1 byte : sequence number (0: control packet)
    -   1: 1 byte : sync type (0: new finger, 1: delete finger, 10: new NFC, 11: delete NFC, 20: new key, 21: delete key)
    -   2: 1 byte : sync data format version (currently always 0)
    - 3-4: 2 bytes: finger ID
    */

    uint8_t syncTypeCode = 0;
    switch (syncType)
    {
        case SyncType::FINGER:
            syncTypeCode = 1;
            break;
        case SyncType::NFC:
            syncTypeCode = 11;
            break;
        case SyncType::KEY:
            syncTypeCode = 21;
            break;
        default:
            logErrorP("Sync-Send (syncType=%u): delete: Unsupported sync type", syncType);
            return;
    }

    uint8_t *data = KoACC_Sync.valueRef();
    data[0] = 0;
    data[1] = syncTypeCode;
    data[2] = 0;
    data[3] = deleteId >> 8;
    data[4] = deleteId;
    KoACC_Sync.objectWritten();

    syncIgnoreTimer = delayTimerInit();
}

// Phase 1 of a sync broadcast. The finger template export is an asynchronous driver composite
// from here on - it is started and the function returns, processSyncExportResult() then runs phase 2
// (person data, compression, checksum, control packet). NFC and keypad records need no scanner at
// all and are still filled and sent synchronously.
// Return value: true = the request has been consumed (started, or definitively dropped), false = the
// caller has to keep it armed and retry on a later pass.
bool AccessControl::startSyncSend(SyncType syncType, uint16_t syncId, bool loadModel)
{
    if (!ParamACC_EnableSync)
        return true;

    // the compressed buffer is still being dribbled out packet by packet, or a template transfer of
    // either direction is in flight - a second fill would clobber the buffers of the running transfer
    // and truncate it, so the request stays armed and is retried instead
    if (syncSending ||
        syncExportActive ||
        syncImportPending ||
        syncImportActive)
    {
        if (syncDeferLogTimer == 0 || delayCheck(syncDeferLogTimer, FP_DEFER_LOG_DELAY))
        {
            syncDeferLogTimer = delayTimerInit();
            logDebugP("Sync-Send (syncType=%u) delayed, previous sync still running (sending=%u, export=%u, import=%u)",
                      syncType, syncSending, syncExportActive, syncImportPending || syncImportActive);
        }

        return false;
    }

    // an incoming sync which is currently being assembled drops an outgoing broadcast
    if (syncReceiving)
        return true;

    // the finger template export needs the scanner, so it is taken through the arbiter before anything
    // is logged or filled. A foreign owner (scan, enrollment, health check) or a bring-up which has not
    // published a ready scanner yet simply postpones the broadcast - the request stays armed and the log
    // lines below stay a once-per-broadcast affair.
    if (syncType == SyncType::FINGER)
    {
        if (!switchFingerprintPower(true))
        {
            logErrorP("Sync-Send (syncType=%u): powering scanner on failed", syncType);
            return true;
        }

        if (!arbiterAcquire(FpOwner::SyncSend))
        {
            // absolute bound for a broadcast which never gets the scanner (a dead scanner, an owner
            // which never lets go). Without it the request would stay armed forever and block every
            // later sync send behind it. Same class of safety net as SYNC_IMPORT_START_TIMEOUT on the
            // receiving side.
            if (syncRequestedFingerTimer > 0 &&
                delayCheck(syncRequestedFingerTimer, SYNC_SEND_START_TIMEOUT))
            {
                logErrorP("Sync-Send (syncType=%u): scanner did not become available (owner=%u, power=%u), request dropped",
                          syncType, (uint8_t)fpOwner, (uint8_t)fpPower);
                syncDeferLogTimer = 0;
                return true; // consume the request
            }

            if (syncDeferLogTimer == 0 || delayCheck(syncDeferLogTimer, FP_DEFER_LOG_DELAY))
            {
                syncDeferLogTimer = delayTimerInit();
                logDebugP("Sync-Send (syncType=%u) delayed, scanner owned by %u (power=%u, busy=%u)",
                          syncType, (uint8_t)fpOwner, (uint8_t)fpPower, finger.isBusy());
            }

            return false;
        }

        syncDeferLogTimer = 0;
    }

    logInfoP("Sync-Send (syncType=%u): started: syncId=%u, loadModel=%u, syncDelay=%u", syncType, syncId, loadModel, ParamACC_SyncDelay);

    uint8_t syncTypeCode = 0;
    uint32_t storageOffset = 0;
    uint8_t syncData[max(OPENKNX_ACC_FLASH_FINGER_DATA_SIZE, OPENKNX_ACC_FLASH_NFC_DATA_SIZE)] = {};
    switch (syncType)
    {
        case SyncType::FINGER:
        {
            syncTypeCode = 0;

            // the arbiter guarantees an idle scan pipeline here
            finger.setLed(FingerprintInterface::Busy);

            // zeroed on every fill (see the member declaration): the person data keeps its frozen
            // place at offset FP_TEMPLATE_SIZE_MAX and the tail behind the template compresses away
            memset(syncExportBuffer, 0, SYNC_BUFFER_SIZE);

            syncExportActive = true;
            syncExportDone = false;
            syncExportOk = false;
            syncExportResult = FpStatus::Ok;
            syncExportSyncId = syncId;

            // callbacks only latch, the finalization runs in processSyncExportResult()
            auto exportDone = [this](const FpResult &result) {
                syncExportResult = result.status;
                syncExportOk = result.ok();
                syncExportDone = true;
            };

            // templateSize(), not FP_TEMPLATE_SIZE_MAX: an R503Pro answers UpChar with 512 bytes, so
            // asking it for 1536 would run into the data phase timeout. The template lands at offset 0
            // of the zeroed 1565 byte buffer either way.
            bool accepted = loadModel
                                ? finger.startRetrieveTemplate(syncId, syncExportBuffer, finger.templateSize(), exportDone)
                                : finger.startUpChar(1, syncExportBuffer, finger.templateSize(), exportDone);
            if (!accepted)
            {
                syncExportResult = finger.lastStartError();
                syncExportOk = false;
                syncExportDone = true;
            }

            return true;
        }
        case SyncType::NFC:
            syncTypeCode = 10;

            memset(syncExportBuffer, 0, SYNC_BUFFER_SIZE);
            storageOffset = ACC_CalcNfcStorageOffset(syncId);
            _nfcStorage.read(storageOffset, syncData, OPENKNX_ACC_FLASH_NFC_DATA_SIZE);
            memcpy(syncExportBuffer, syncData, OPENKNX_ACC_FLASH_NFC_DATA_SIZE);
            break;
        case SyncType::KEY:
            syncTypeCode = 20;

            memset(syncExportBuffer, 0, SYNC_BUFFER_SIZE);
            storageOffset = ACC_CalcKeyStorageOffset(syncId);
            _keypadStorage.read(storageOffset, syncData, OPENKNX_ACC_FLASH_KEY_DATA_SIZE);
            memcpy(syncExportBuffer, syncData, OPENKNX_ACC_FLASH_KEY_DATA_SIZE);
            break;
        default:
            logErrorP("Sync-Send (syncType=%u): delete: Unsupported sync type", syncType);
            return true;
    }

    sendSyncControlPacket(syncTypeCode, syncId);
    return true;
}

// Finalization of the asynchronous template export. Called from loop(), so the flash read, the
// compression and the group object write never run from a driver callback.
void AccessControl::processSyncExportResult()
{
    if (!syncExportDone)
        return;

    syncExportDone = false;

    bool success = syncExportOk;
    if (success)
    {
        uint32_t storageOffset = ACC_CalcFingerStorageOffset(syncExportSyncId);
        uint8_t syncData[OPENKNX_ACC_FLASH_FINGER_DATA_SIZE] = {};
        _fingerprintStorage.read(storageOffset, syncData, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);
        // fixed offset FP_TEMPLATE_SIZE_MAX (1536) on both models - the sync wire format is frozen
        memcpy(syncExportBuffer + FP_TEMPLATE_SIZE_MAX, syncData, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);
    }
    else
    {
        logErrorP("Sync-Send (syncType=%u): retrieving template failed", (uint8_t)SyncType::FINGER);
        logIndentUp();
        logInfoP("Reason: %s (0x%02X)", fpStatusText(syncExportResult), (uint8_t)syncExportResult);
        logIndentDown();

        // the reset timer below makes sure a failed broadcast cannot strand the ring LED at "Busy". The
        // request fields were already consumed by the trigger in loop(), a failed export is not retried.
    }

    // the ring LED is handed to the reset timer instead of being reset right here, exactly like the
    // three other finalizations do: resetting it directly while the suppression flag is still set would
    // leave the timer armed, so a stale one would fire a second, immediate reset one pass later.
    // Re-arming and clearing the flag afterwards keeps the LED for the usual second.
    resetFingerLedTimer = delayTimerInit();
    syncExportActive = false;

    // a lock which arrived during the export applied its LED before the composite overwrote it with
    // "Busy"; it is re-applied because the reset timer above is gated behind !isLocked (same as the
    // enrollment finalization does)
    if (isLocked)
        finger.setLed(FingerprintInterface::Locked);

    // phase 2 and the packet dribbling in processSyncSend() need no scanner
    if (fpOwner == FpOwner::SyncSend)
        arbiterRelease(FpOwner::SyncSend);

    if (success)
        sendSyncControlPacket(0, syncExportSyncId);
}

// Phase 2 of a sync broadcast: compresses syncExportBuffer, checksums the result and sends the control
// packet. syncSending is set here and only here, which is what the ETS "wait for sync sending" function
// property polls.
void AccessControl::sendSyncControlPacket(uint8_t syncTypeCode, uint16_t syncId)
{
    // compressing 1565 bytes plus the CRC is legitimately heavy work which does not fit into the loop
    // time budget; it runs once per broadcast, so the warning is suppressed for this pass
    openknx.common.skipLooptimeWarning();

    const int maxDstSize = LZ4_compressBound(SYNC_BUFFER_SIZE);
    const int compressedDataSize = LZ4_compress_default((char*)syncExportBuffer, (char*)syncSendBuffer, SYNC_BUFFER_SIZE, maxDstSize);

    syncSendBufferLength = compressedDataSize;
    syncSendPacketCount = ceil(syncSendBufferLength / (float)SYNC_SEND_PACKET_DATA_LENGTH) + 1; // currently separated control packet
    uint16_t checksum = crc16.ccitt(syncSendBuffer, syncSendBufferLength);

    logDebugP("Sync-Send (syncTypeCode=%u, 1/%u): control packet: bufferLength=%u, lengthPerPacket=%u, checksum=%u, fingerId=%u, uncompressed=%u, compressed=%u", syncTypeCode, syncSendPacketCount, syncSendBufferLength, SYNC_SEND_PACKET_DATA_LENGTH, checksum, syncId, (uint16_t)(SYNC_BUFFER_SIZE), syncSendBufferLength);

    /*
    Sync Control Packet Layout:
    -    0: 1 byte : sequence number (0: control packet)
    -    1: 1 byte : sync type (0: new finger, 1: delete finger, 10: new NFC, 11: delete NFC, 20: new key, 21: delete key)
    -    2: 1 byte : sync data format version (currently always 0)
    -  3-4: 2 bytes: total data content size
    -    5: 1 byte : max. payload data length per data packet
    -    6: 1 byte : number of data packets
    -  7-8: 2 bytes: checksum
    - 9-10: 2 bytes: finger ID
    */

    uint8_t *data = KoACC_Sync.valueRef();
    data[0] = 0;
    data[1] = syncTypeCode;
    data[2] = 0;
    data[3] = syncSendBufferLength >> 8;
    data[4] = syncSendBufferLength;
    data[5] = SYNC_SEND_PACKET_DATA_LENGTH;
    data[6] = syncSendPacketCount;
    data[7] = checksum >> 8;
    data[8] = checksum;
    data[9] = syncId >> 8;
    data[10] = syncId;
    KoACC_Sync.objectWritten();

    syncSendTimer = delayTimerInit();
    syncSendPacketSentCount = 1;
    syncSending = true;
}

void AccessControl::processSyncSend()
{
    if (!syncSending ||
        !delayCheck(syncSendTimer, ParamACC_SyncDelay))
        return;

    syncSendTimer = delayTimerInit();

    uint8_t *data = KoACC_Sync.valueRef();
    data[0] = syncSendPacketSentCount;
    uint8_t dataPacketNo = syncSendPacketSentCount - 1; // = sequence number - 1
    uint16_t dataOffset = dataPacketNo * SYNC_SEND_PACKET_DATA_LENGTH;
    uint8_t dataLength = dataOffset + SYNC_SEND_PACKET_DATA_LENGTH < syncSendBufferLength ? SYNC_SEND_PACKET_DATA_LENGTH : syncSendBufferLength - dataOffset;
    memcpy(data + 1, syncSendBuffer + dataOffset, dataLength);
    KoACC_Sync.objectWritten();

    syncSendPacketSentCount++;
    logDebugP("Sync-Send (%u/%u): data packet: dataPacketNo=%u, dataOffset=%u, dataLength=%u", syncSendPacketSentCount, syncSendPacketCount, dataPacketNo, dataOffset, dataLength);

    if (syncSendPacketSentCount == syncSendPacketCount)
    {
        logDebugP("Sync-Send: finished");

        syncSending = false;
        syncIgnoreTimer = delayTimerInit();
    }
}

void AccessControl::processSyncReceive(uint8_t* data)
{
    // while a received finger template is waiting for the scanner or is being written to it,
    // syncImportBuffer holds the payload of that transfer. A new incoming sync would decompress over
    // it, so all sync traffic is ignored for that window, just like it is while an own broadcast is
    // being sent out. syncExportActive belongs into the set as well: a running template export owns the
    // scanner, and an incoming template would queue an import behind it which the export's own trigger
    // would then have to compete with.
    if (syncSending ||
        syncExportActive ||
        syncImportPending ||
        syncImportActive)
        return;

    if (syncIgnoreTimer > 0)
    {
        if (delayCheck(syncIgnoreTimer, SYNC_IGNORE_DELAY))
            syncIgnoreTimer = 0;
        else
            return;
    }
    
    if (data[0] == 0) // sequence number
    {
        // a control packet always ends whatever was being assembled before (the branches below either
        // start a new assembly or handle a delete), so the reception timeout is disarmed here and only
        // re-armed by the three "new record" types
        syncReceiveLastPacketTimer = 0;

        uint16_t syncDeleteFingerId;
        uint16_t syncDeleteNfcId;
        uint16_t syncDeleteKeyId;
        switch (data[1]) // sync type
        {
            case 0: // new finger
            case 10: // new NFC
            case 20: // new key
                switch (data[1])
                {
                    case 0:
                        syncReceiveType = SyncType::FINGER;
                        break;
                    case 10:
                        syncReceiveType = SyncType::NFC;
                        break;
                    case 20:
                        syncReceiveType = SyncType::KEY;
                        break;
                    default:
                        logInfoP("Sync-Receive: Unsupported sync type: %u", data[1]);
                        return;
                }

                if (data[2] != 0)
                {
                    logInfoP("Sync-Receive (syncType=%u): Unsupported sync version: %u", syncReceiveType, data[2]);
                    return;
                }

                syncReceiveBufferLength = (data[3] << 8) | data[4];
                syncReceiveLengthPerPacket = data[5];
                syncReceivePacketCount = data[6];
                syncReceiveBufferChecksum = (data[7] << 8) | data[8];
                syncReceiveSyncId = (data[9] << 8) | data[10];

                logDebugP("Sync-Receive (syncType=%u, 1/%u): control packet: bufferLength=%u, lengthPerPacket=%u, checksum=%u, syncId=%u", syncReceiveType, syncReceivePacketCount, syncReceiveBufferLength, syncReceiveLengthPerPacket, syncReceiveBufferChecksum, syncReceiveSyncId);

                memset(syncReceivePacketReceived, 0, sizeof(syncReceivePacketReceived));
                syncReceivePacketReceived[0] = true;
                syncReceivePacketReceivedCount = 1;
                syncReceiving = true;
                syncReceiveLastPacketTimer = delayTimerInit();

                return;
            case 1: // delete finger
                if (data[2] != 0)
                {
                    logInfoP("Sync-Receive (delete finger): Unsupported sync version: %u", data[2]);
                    return;
                }

                syncDeleteFingerId = (data[3] << 8) | data[4];
                logDebugP("Sync-Receive (delete finger): fingerId=%u", syncDeleteFingerId);

                // queued instead of executed inline - a scanner operation must not run in the group
                // object callback. It is never dropped and never broadcast again; a location the index
                // cache does not know is answered by the driver composite with the DeleteNotFound LED
                // and without any wire traffic.
                queueBusDelete(syncDeleteFingerId);

                syncReceiving = false;
                return;
            case 11: // delete NFC
                if (data[2] != 0)
                {
                    logInfoP("Sync-Receive (delete NFC): Unsupported sync version: %u", data[2]);
                    return;
                }

                syncDeleteNfcId = (data[3] << 8) | data[4];
                logDebugP("Sync-Receive (delete NFC): fingerId=%u", syncDeleteNfcId);

                deleteNfc(syncDeleteNfcId, false);

                syncReceiving = false;
                return;

            case 21: // delete key
                if (data[2] != 0)
                {
                    logInfoP("Sync-Receive (delete key): Unsupported sync version: %u", data[2]);
                    return;
                }

                syncDeleteKeyId = (data[3] << 8) | data[4];
                logDebugP("Sync-Receive (delete key): keyId=%u", syncDeleteKeyId);

                deleteKey(syncDeleteKeyId, false);

                syncReceiving = false;
                return;
            default:
                logInfoP("Sync-Receive: Unsupported sync type: %u", data[1]);
                syncReceiving = false;
                return;
        }
    }

    if (!syncReceiving)
    {
        logInfoP("Sync-Receive (syncType=%u): data packet without control packet", syncReceiveType);
        return;
    }

    // any packet of the running transfer proves the sender is still alive, a repeated one included
    syncReceiveLastPacketTimer = delayTimerInit();

    uint8_t sequenceNo = data[0];
    if (syncReceivePacketReceived[sequenceNo])
    {
        logInfoP("Sync-Receive (syncType=%u): same packet already received", syncReceiveType);
        return;
    }

    syncReceivePacketReceived[sequenceNo] = true;
    uint8_t dataPacketNo = sequenceNo - 1;
    uint16_t dataOffset = dataPacketNo * syncReceiveLengthPerPacket;
    uint8_t dataLength = dataOffset + syncReceiveLengthPerPacket < syncReceiveBufferLength ? syncReceiveLengthPerPacket : syncReceiveBufferLength - dataOffset;
    memcpy(syncReceiveBuffer + dataOffset, data + 1, dataLength);

    syncReceivePacketReceivedCount++;
    logDebugP("Sync-Receive (syncType=%u, %u/%u): data packet: dataPacketNo=%u, dataOffset=%u, dataLength=%u", syncReceiveType, syncReceivePacketReceivedCount, syncReceivePacketCount, dataPacketNo, dataOffset, dataLength);

    if (syncReceivePacketReceivedCount == syncReceivePacketCount)
    {
        if (syncReceiveType == SyncType::FINGER)
        {
            if (!switchFingerprintPower(true))
            {
                logErrorP("Sync-Receive (syncType=%u): powering scanner on failed", syncReceiveType);

                // the payload is complete but unusable without a scanner, so the reception ends here.
                // No LED: the ring is untouched in this path (the "Busy" below is only reached with a
                // scanner which is coming up), and a scanner which is off or disabled by ETS cannot
                // show anything anyway.
                syncReceiving = false;
                syncReceiveLastPacketTimer = 0;
                return;
            }

            finger.setLed(FingerprintInterface::Busy);
        }

        // the CRC over the received buffer and the decompression below are the receive side counterpart
        // of the heavy work in sendSyncControlPacket(); they run once per received broadcast, right in
        // the group object callback, so the warning is suppressed for this pass
        openknx.common.skipLooptimeWarning();

        // both failure returns below end the reception: the assembly state is dropped so the device is
        // receptive again right away and, above all, its own broadcasts are not suppressed any more.
        // Neither of them has reached syncImportPending, so no queued import can be cut short here.
        uint16_t checksum = crc16.ccitt(syncReceiveBuffer, syncReceiveBufferLength);
        if (syncReceiveBufferChecksum == checksum)
            logDebugP("Sync-Receive (syncType=%u): finished (checksum=%u)", syncReceiveType, syncReceiveBufferChecksum);
        else
        {
            logErrorP("Sync-Receive (syncType=%u): finished failed (checksum expected=%u, calculated=%u)", syncReceiveType, syncReceiveBufferChecksum, checksum);

            if (syncReceiveType == SyncType::FINGER)
            {
                finger.setLed(FingerprintInterface::Failed);
                resetFingerLedTimer = delayTimerInit();
            }

            syncReceiving = false;
            syncReceiveLastPacketTimer = 0;
            return;
        }

        // decompresses into the member buffer, zeroed first so nothing of a previous sync can
        // survive behind a payload the decompression did not cover
        memset(syncImportBuffer, 0, SYNC_BUFFER_SIZE);
        const int decompressedSize = LZ4_decompress_safe((char*)syncReceiveBuffer, (char*)syncImportBuffer, syncReceiveBufferLength, SYNC_BUFFER_SIZE);
        if (decompressedSize != SYNC_BUFFER_SIZE)
        {
            logErrorP("Sync-Receive (syncType=%u): decompression failed (size expected=%u, received=%u)", syncReceiveType, SYNC_BUFFER_SIZE, decompressedSize);

            if (syncReceiveType == SyncType::FINGER)
            {
                finger.setLed(FingerprintInterface::Failed);
                resetFingerLedTimer = delayTimerInit();
            }

            syncReceiving = false;
            syncReceiveLastPacketTimer = 0;
            return;
        }

        uint32_t storageOffset = 0;
        switch (syncReceiveType)
        {
            case SyncType::FINGER:
                // the template import is an asynchronous driver composite: it is started by
                // processSyncImportStart() from loop() and finished by processSyncImportResult(),
                // which writes the person data, logs and sets the LED. syncReceiving stays true
                // until then, so the observable "a sync is being received" window spans the whole
                // transfer and the sync path keeps ignoring incoming traffic meanwhile.
                syncImportPending = true;
                syncImportPendingTimer = delayTimerInit();
                // the transfer itself is complete; from here on SYNC_IMPORT_START_TIMEOUT is the bound
                syncReceiveLastPacketTimer = 0;
                return;
            case SyncType::NFC:
                storageOffset = ACC_CalcNfcStorageOffset(syncReceiveSyncId);
                _nfcStorage.write(storageOffset, syncImportBuffer, OPENKNX_ACC_FLASH_NFC_DATA_SIZE);
                _nfcStorage.commit();
                break;
            case SyncType::KEY:
                storageOffset = ACC_CalcKeyStorageOffset(syncReceiveSyncId);
                _keypadStorage.write(storageOffset, syncImportBuffer, OPENKNX_ACC_FLASH_KEY_DATA_SIZE);
                _keypadStorage.commit();
                break;
        }

        logInfoP("Sync-Receive (syncType=%u): data stored", syncReceiveType);
        syncReceiving = false;
        syncReceiveLastPacketTimer = 0;
    }
}

// The arbiter side of a received finger template. The import is the highest priority scanner
// activity (it keeps the whole sync path blocked while it waits) and it is always started from
// loop(), never from the group object callback which assembled the last packet.
void AccessControl::processSyncImportStart()
{
    if (!syncImportPending)
        return;

    if (!arbiterAcquire(FpOwner::SyncReceive))
    {
        // a scanner which never becomes ready or an owner which never lets go would block every
        // further sync, so the wait is bounded
        if (fpPower == FpPowerState::Failed ||
            delayCheck(syncImportPendingTimer, SYNC_IMPORT_START_TIMEOUT))
        {
            logErrorP("Sync-Receive (syncType=%u): scanner did not become available (owner=%u, power=%u)", syncReceiveType, (uint8_t)fpOwner, (uint8_t)fpPower);

            finger.setLed(FingerprintInterface::Failed);
            resetFingerLedTimer = delayTimerInit();

            syncImportPending = false;
            syncReceiving = false;
            return;
        }

        if (syncImportDeferLogTimer == 0 || delayCheck(syncImportDeferLogTimer, FP_DEFER_LOG_DELAY))
        {
            syncImportDeferLogTimer = delayTimerInit();
            logDebugP("Sync-Receive (syncType=%u): import delayed, scanner owned by %u (power=%u, busy=%u)",
                      syncReceiveType, (uint8_t)fpOwner, (uint8_t)fpPower, finger.isBusy());
        }

        return;
    }

    syncImportDeferLogTimer = 0;
    syncImportPending = false;
    syncImportActive = true;
    syncImportDone = false;
    syncImportOk = false;
    syncImportResult = FpStatus::Ok;

    // startStoreTemplate() is DownChar + post data guard + Store + index cache update.
    // templateSize(), not FP_TEMPLATE_SIZE_MAX: only the leading 512 bytes go to an R503Pro.
    if (!finger.startStoreTemplate(syncReceiveSyncId, syncImportBuffer, finger.templateSize(),
                                           [this](const FpResult &result) {
                                               // callbacks only latch, see processSyncImportResult()
                                               syncImportResult = result.status;
                                               syncImportOk = result.ok();
                                               syncImportDone = true;
                                           }))
    {
        syncImportResult = finger.lastStartError();
        syncImportOk = false;
        syncImportDone = true;
    }
}

// Finalization of a received finger template: the flash write, the log line and the LEDs.
void AccessControl::processSyncImportResult()
{
    if (!syncImportDone)
        return;

    syncImportDone = false;

    if (syncImportOk)
    {
        uint32_t storageOffset = ACC_CalcFingerStorageOffset(syncReceiveSyncId);
        // fixed offset FP_TEMPLATE_SIZE_MAX (1536) on both models - the sync wire format is frozen
        _fingerprintStorage.write(storageOffset, syncImportBuffer + FP_TEMPLATE_SIZE_MAX, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);
        _fingerprintStorage.commit();

        finger.setLed(FingerprintInterface::Success);
        resetFingerLedTimer = delayTimerInit();

        logInfoP("Sync-Receive (syncType=%u): data stored", syncReceiveType);
    }
    else
    {
        logErrorP("Sync-Receive (syncType=%u): storing finger template failed", syncReceiveType);
        logIndentUp();
        logInfoP("Reason: %s (0x%02X)", fpStatusText(syncImportResult), (uint8_t)syncImportResult);
        logIndentDown();

        finger.setLed(FingerprintInterface::Failed);
        resetFingerLedTimer = delayTimerInit();
    }

    syncImportActive = false;
    syncReceiving = false;
    if (fpOwner == FpOwner::SyncReceive)
        arbiterRelease(FpOwner::SyncReceive);
}

bool AccessControl::processFunctionProperty(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    if (!knx.configured() || objectIndex != 160 || propertyId != 3)
        return false;

    switch(data[0])
    {
        case 1:
            handleFunctionPropertyEnrollFinger(data, resultData, resultLength);
            return true;
        case 2:
            handleFunctionPropertySyncFinger(data, resultData, resultLength);
            return true;
        case 3:
            handleFunctionPropertyDeleteFinger(data, resultData, resultLength);
            return true;
        case 4:
            handleFunctionPropertyChangeFinger(data, resultData, resultLength);
            return true;
        case 6:
            handleFunctionPropertyResetFingerScanner(data, resultData, resultLength);
            return true;
        case 7:
            handleFunctionPropertyWaitEnrollFingerFinished(data, resultData, resultLength);
            return true;
        case 11:
            handleFunctionPropertySearchPersonByFingerId(data, resultData, resultLength);
            return true;
        case 12:
            handleFunctionPropertySearchFingerIdByPerson(data, resultData, resultLength);
            return true;
        case 21:
            handleFunctionPropertySetFingerPassword(data, resultData, resultLength);
            return true;
        case 101:
            handleFunctionPropertyEnrollNfc(data, resultData, resultLength);
            return true;
        case 102:
            handleFunctionPropertySyncNfc(data, resultData, resultLength);
            return true;
        case 103:
            handleFunctionPropertyDeleteNfc(data, resultData, resultLength);
            return true;
        case 104:
            handleFunctionPropertyChangeNfc(data, resultData, resultLength);
            return true;
        case 106:
            handleFunctionPropertyResetNfcScanner(data, resultData, resultLength);
            return true;
        case 107:
            handleFunctionPropertyWaitEnrollNfcFinished(data, resultData, resultLength);
            return true;
        case 111:
            handleFunctionPropertySearchTagByNfcId(data, resultData, resultLength);
            return true;
        case 112:
            handleFunctionPropertySearchNfcIdByTag(data, resultData, resultLength);
            return true;
        case 202:
            handleFunctionPropertySyncKeypad(data, resultData, resultLength);
            return true;
        case 203:
            handleFunctionPropertyDeleteKeypad(data, resultData, resultLength);
            return true;
        case 204:
            handleFunctionPropertyChangeKeypad(data, resultData, resultLength);
            return true;
        case 206:
            handleFunctionPropertyResetKeypad(data, resultData, resultLength);
            return true;
        case 211:
            handleFunctionPropertySearchCodeNameByCodeId(data, resultData, resultLength);
            return true;
        case 212:
            handleFunctionPropertySearchCodeIdByCodeName(data, resultData, resultLength);
            return true;
        case 240:
            handleFunctionPropertyWaitSyncSending(data, resultData, resultLength);
            return true;
    }

    return false;
}

void AccessControl::handleFunctionPropertyEnrollFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Enroll request");
    logIndentUp();

    uint16_t fingerId = (data[1] << 8) | data[2];
    logDebugP("fingerId: %d", fingerId);

    uint8_t personFinger = data[3];
    logDebugP("personFinger: %d", personFinger);

    uint8_t personName[28] = {};
    for (uint8_t i = 0; i < 28; i++)
    {
        memcpy(personName + i, data + 4 + i, 1);
        if (personName[i] == 0) // null termination
            break;
    }
    logDebugP("personName: %s", personName);

    uint32_t storageOffset = ACC_CalcFingerStorageOffset(fingerId);
    logDebugP("storageOffset: %d", storageOffset);
    _fingerprintStorage.writeByte(storageOffset, personFinger); // only 4 bits used
    _fingerprintStorage.write(storageOffset + 1, personName, 28);
    _fingerprintStorage.commit();

    enrollRequestedFingerTimer = delayTimerInit();
    enrollRequestedFingerLocation = fingerId;
    
    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyWaitEnrollFingerFinished(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Wait until Enroll request finished");
    // logIndentUp();

    // resultData[0] true, if enroll request is finished
    resultData[0] = enrollRequestedFingerTimer == 0;
    resultLength = 2;
    if (enrollRequestedFingerTimer == 0)
    {
        // resultData[1] true, if enroll request was successful
        resultData[1] = syncRequestedFingerTimer > 0;
    } else {
        // as long as enroll is not finished, return progress; the driver keeps it live for the
        // running enrollment (1-6 = waiting for capture n, 7 = create model, 8 = store) and 0 is
        // reported while the request is only armed and no composite has been started yet
        resultData[1] = enrollActive ? finger.enrollProgress() : 0;
    }
    // logIndentDown();
}

void AccessControl::handleFunctionPropertyChangeFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Change request");
    logIndentUp();

    uint16_t fingerId = (data[1] << 8) | data[2];
    logDebugP("fingerId: %d", fingerId);

    if (switchFingerprintPower(true))
    {
        if (finger.hasLocation(fingerId))
        {
            uint8_t personFinger = data[3];
            logDebugP("personFinger: %d", personFinger);

            uint8_t personName[28] = {};
            bool personNameEmpty = false;
            for (uint8_t i = 0; i < 28; i++)
            {
                memcpy(personName + i, data + 4 + i, 1);
                if (personName[i] == 0) // null termination
                {
                    personNameEmpty = i == 0;
                    break;
                }
            }
            logDebugP("personName: %s", personName);

            uint32_t storageOffset = ACC_CalcFingerStorageOffset(fingerId);
            logDebugP("storageOffset: %d", storageOffset);
            _fingerprintStorage.writeByte(storageOffset, personFinger); // only 4 bits used
            if (!personNameEmpty)
                _fingerprintStorage.write(storageOffset + 1, personName, 28);
            _fingerprintStorage.commit();

            syncRequestedFingerId = fingerId;
            syncRequestedFingerTimer = delayTimerInit();

            resultData[0] = 0;
        }
        else
        {
            logInfoP("Finger not found");
            resultData[0] = 1;
        }
    }
    else
        resultData[0] = 1;

    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertySyncFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Sync request");
    logIndentUp();

    uint16_t fingerId = (data[1] << 8) | data[2];
    logDebugP("fingerId: %d", fingerId);

    if (switchFingerprintPower(true))
    {
        if (finger.hasLocation(fingerId))
        {
            syncRequestedFingerId = fingerId;
            syncRequestedFingerTimer = delayTimerInit();

            resultData[0] = 0;
        }
        else
            resultData[0] = 1;
    }
    else
        resultData[0] = 1;

    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyDeleteFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Delete request");
    logIndentUp();

    uint16_t fingerId = (data[1] << 8) | data[2];
    logDebugP("fingerId: %d", fingerId);

    char personName[28] = {}; // empty

    uint32_t storageOffset = ACC_CalcFingerStorageOffset(fingerId);
    _fingerprintStorage.writeByte(storageOffset, 0); // "0" for not set
    _fingerprintStorage.write(storageOffset + 1, *personName, 28);
    _fingerprintStorage.commit();

    // the scanner side is queued and executed asynchronously, so everything which decides the answer of
    // this call has to come from local state. A location the drivers index cache does not know (and
    // every location when the scanner is not ready, because the cache then answers false for all of
    // them) takes the "not found" path: the DeleteNotFound LED, no wire traffic and the failure code.
    bool success = false;
    if (!finger.hasLocation(fingerId))
    {
        logInfoP("Deleting template failed.");
        logIndentUp();
        logInfoP("Reason: no template stored in location %d.", fingerId);
        logIndentDown();

        // only latched for a scanner which is up. The driver keeps the LED in its latch
        // until the next command boundary, so a latch set while it is not ready (the cache answers
        // "no template" for every location then) would surface on a later successful bring-up.
        if (fpPower == FpPowerState::Ready)
        {
            finger.setLed(FingerprintInterface::DeleteNotFound);
            resetFingerLedTimer = delayTimerInit();
        }
    }
    else if (maintenanceBusy())
    {
        logInfoP("Deleting template failed.");
        logIndentUp();
        logInfoP("Reason: another maintenance operation is still running.");
        logIndentDown();

        resetFingerLedTimer = delayTimerInit();
    }
    else
    {
        // answered optimistically: the operation itself runs from loop() and, on success, sends the
        // delete sync broadcast (converting this to a request/poll pair would need knxprod changes)
        maintPending = MaintOp::DeleteFinger;
        maintDeleteLocation = fingerId;
        maintDeleteSendSync = true;
        maintPendingTimer = delayTimerInit();
        maintDeferLogTimer = 0;

        logInfoP("Delete of location %d queued.", fingerId);
        success = true;
    }

    resultData[0] = success ? 0 : 1;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyResetFingerScanner(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Reset scanner");
    logIndentUp();

    // the local person data wipe stays synchronous (it is flash, not the scanner), emptying the finger
    // library is queued. The wipe is skipped when the scanner is not available, so the person data and
    // the finger library can never get out of step.
    bool success = false;
    if (fpPower != FpPowerState::Ready)
    {
        logInfoP("Resetting the scanner failed.");
        logIndentUp();
        logInfoP("Reason: fingerprint scanner not available (power=%u).", (uint8_t)fpPower);
        logIndentDown();

        // kick the bring-up off so a repeated attempt can succeed
        switchFingerprintPower(true);
    }
    else if (maintenanceBusy())
    {
        logInfoP("Resetting the scanner failed.");
        logIndentUp();
        logInfoP("Reason: another maintenance operation is still running.");
        logIndentDown();
    }
    else
    {
        // 1500 flash records plus the commit are far above the loop time budget and this runs in the
        // ETS request context, so the warning is suppressed for this pass
        openknx.common.skipLooptimeWarning();

        char fingerData[OPENKNX_ACC_FLASH_FINGER_DATA_SIZE] = {}; // empty
        for (uint16_t i = 0; i < MAX_FINGERS; i++)
        {
            uint32_t storageOffset = ACC_CalcFingerStorageOffset(i);
            _fingerprintStorage.write(storageOffset, *fingerData, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);
        }
        _fingerprintStorage.commit();

        maintPending = MaintOp::EmptyDatabase;
        maintPendingTimer = delayTimerInit();
        maintDeferLogTimer = 0;

        logInfoP("Person data wiped, emptying the finger library queued.");
        success = true;
    }

    resultData[0] = success ? 0 : 1;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertySearchPersonByFingerId(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Search person by FingerId");
    logIndentUp();

    uint16_t fingerId = (data[1] << 8) | data[2];
    logDebugP("fingerId: %d", fingerId);

    if (switchFingerprintPower(true))
    {
        if (!finger.hasLocation(fingerId))
        {
            logDebugP("Unrecognized by scanner!");
            resultData[0] = 1;
            resultLength = 1;

            logIndentDown();
            return;
        }

        uint8_t personName[28] = {};

        uint32_t storageOffset = ACC_CalcFingerStorageOffset(fingerId);
        logDebugP("storageOffset: %d", storageOffset);
        uint8_t personFinger = _fingerprintStorage.readByte(storageOffset);
        if (personFinger > 0)
        {
            _fingerprintStorage.read(storageOffset + 1, personName, 28);

            logDebugP("Found:");
            logIndentUp();
            logDebugP("personFinger: %d", personFinger);
            logDebugP("personName: %s", personName);
            logIndentDown();

            resultData[0] = 0;
            resultData[1] = personFinger;
            resultLength = 2;
            for (uint8_t i = 0; i < 28; i++)
            {
                memcpy(resultData + 2 + i, personName + i, 1);
                resultLength++;

                if (personName[i] == 0) // null termination
                    break;
            }
        }
        else
        {
            logDebugP("Not found.");

            resultData[0] = 1;
            resultLength = 1;
        }
    }
    else
    {
        resultData[0] = 1;
        resultLength = 1;
    }

    logIndentDown();
}

void AccessControl::handleFunctionPropertySearchFingerIdByPerson(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Search FingerId(s) by person");
    logIndentUp();

    uint8_t searchPersonFinger = data[1];
    logDebugP("searchPersonFinger: %d", searchPersonFinger); // can be "0" if only by name should be searched

    char searchPersonName[28] = {};
    uint8_t searchPersonNameLength = 28;
    for (size_t i = 0; i < 28; i++)
    {
        memcpy(searchPersonName + i, data + 2 + i, 1);
        if (searchPersonName[i] == 0) // null termination
        {
            searchPersonNameLength = i;
            break;
        }
    }
    logDebugP("searchPersonName: %s (length: %u)", searchPersonName, searchPersonNameLength);
    logDebugP("resultLength: %u", resultLength);

    uint8_t recordLength = OPENKNX_ACC_FLASH_FINGER_DATA_SIZE + 2;
    uint8_t foundCount = 0;
    uint16_t foundTotalCount = 0;
    if (switchFingerprintPower(true))
    {
        // the drivers index cache is walked directly, nothing is materialized for this single call site.
        // hasLocation() answers false for every location while the scanner is not ready, so a dead
        // scanner simply returns "nothing found".
        uint16_t capacity = finger.libraryCapacity();
        uint16_t templateCount = finger.getTemplateCount();
        uint16_t visitedCount = 0;

        uint32_t storageOffset = 0;
        uint8_t personFinger = 0;
        uint8_t personName[28] = {};
        for (uint16_t fingerId = 0; fingerId < capacity && visitedCount < templateCount; fingerId++)
        {
            if (!finger.hasLocation(fingerId))
                continue;

            visitedCount++;
            storageOffset = ACC_CalcFingerStorageOffset(fingerId);
            personFinger = _fingerprintStorage.readByte(storageOffset);
            if (searchPersonFinger > 0)
                if (searchPersonFinger != personFinger)
                    continue;

            _fingerprintStorage.read(storageOffset + 1, personName, 28);
            if (strcasestr((char *)personName, searchPersonName) != nullptr)
            {
                // logDebugP("Found:");
                // logIndentUp();
                // logDebugP("fingerId: %d", fingerId);
                // logDebugP("personFinger: %d", personFinger);
                // logDebugP("personName: %s", personName);
                // logIndentDown();

                // we return max. 7 results (3 + 31 * 7 = 220 bytes)
                if (foundCount < 7)
                {
                    resultData[3 + foundCount * recordLength] = fingerId >> 8;
                    resultData[3 + foundCount * recordLength + 1] = fingerId;
                    resultData[3 + foundCount * recordLength + 2] = personFinger;
                    memcpy(resultData + 3 + foundCount * recordLength + 3, personName, 28);

                    foundCount++;
                }

                foundTotalCount++;
            }
        }
    }
    
    resultData[0] = foundCount > 0 ? 0 : 1; 
    resultData[1] = foundTotalCount >> 8;
    resultData[2] = foundTotalCount;
    resultLength = 3 + foundCount * recordLength;

    logDebugP("foundTotalCount: %u", foundTotalCount);
    logDebugP("returned resultLength: %u", resultLength);
    logIndentDown();
}

void AccessControl::handleFunctionPropertySetFingerPassword(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Set password");
    logIndentUp();

    uint8_t passwordOption = data[1];
    logDebugP("passwordOption: %d", passwordOption);

    uint8_t dataOffset = 1;

    char newPassword[16] = {};
    for (size_t i = 0; i < 16; i++)
    {
        dataOffset++;
        memcpy(newPassword + i, data + dataOffset, 1);

        if (newPassword[i] == 0) // null termination
            break;
    }

    uint32_t newPasswordCrc = 0;
    if (newPassword[0] != 48 || // = "0": if user inputs only "0", we just use it as is without CRC
        newPassword[1] != 0)    // null termination
        newPasswordCrc = crc32.crc32((uint8_t *)newPassword, 16);
    logDebugP("newPassword: %s (crc: %u)", newPassword, newPasswordCrc);

    // change password
    uint32_t oldPasswordCrc = 0;
    if (passwordOption == 2)
    {
        char oldPassword[16] = {};
        for (uint8_t i = 0; i < 16; i++)
        {
            dataOffset++;
            memcpy(oldPassword + i, data + dataOffset, 1);

            if (oldPassword[i] == 0) // null termination
                break;
        }

        if (oldPassword[0] != 48 || // = "0": if user inputs only "0", we just use it as is without CRC
            oldPassword[1] != 0)    // null termination
            oldPasswordCrc = crc32.crc32((uint8_t *)oldPassword, 16);
        logDebugP("oldPassword: %s (crc: %u)", oldPassword, oldPasswordCrc);
    }

    uint32_t currentCrc = _fingerprintStorage.readInt(FLASH_FINGER_SCANNER_PASSWORD_OFFSET);
    logDebugP("currentCrc: %u", currentCrc);

    bool success = false;
    if (currentCrc == oldPasswordCrc)
    {
        logDebugP("Current matches old CRC.");
        logIndentUp();

        // the old password validation above is local (flash CRC) and stays synchronous, the SetPwd
        // command itself is queued. On success the finalization writes the new CRC to the flash - it is
        // persisted only when the sensor confirmed the change - and the driver adopts the new password
        // for the running session, so no re-init is needed.
        if (fpPower != FpPowerState::Ready)
        {
            logInfoP("Setting new fingerprint scanner password: Failed.");
            logIndentUp();
            logInfoP("Reason: fingerprint scanner not available (power=%u).", (uint8_t)fpPower);
            logIndentDown();

            // kick the bring-up off so a repeated attempt can succeed
            switchFingerprintPower(true);
        }
        else if (maintenanceBusy())
        {
            logInfoP("Setting new fingerprint scanner password: Failed.");
            logIndentUp();
            logInfoP("Reason: another maintenance operation is still running.");
            logIndentDown();
        }
        else
        {
            maintPending = MaintOp::SetPassword;
            maintNewPasswordCrc = newPasswordCrc;
            maintPendingTimer = delayTimerInit();
            maintDeferLogTimer = 0;

            logInfoP("Setting new fingerprint scanner password queued.");
            success = true;
        }

        logIndentDown();

        resultData[0] = success ? 0 : 2;
    }
    else
    {
        logDebugP("Invalid old password provided.");
        resultData[0] = 1;
    }

    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyEnrollNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Enroll request");
    logIndentUp();

    uint16_t nfcId = (data[1] << 8) | data[2];
    logDebugP("nfcId: %d", nfcId);

    uint8_t tagName[28] = {};
    for (uint8_t i = 0; i < 28; i++)
    {
        memcpy(tagName + i, data + 3 + i, 1);
        if (tagName[i] == 0) // null termination
            break;
    }
    logDebugP("tagName: %s", tagName);

    uint8_t tagUid[10] = {}; // empty

    uint32_t storageOffset = ACC_CalcNfcStorageOffset(nfcId);
    logDebugP("storageOffset: %d", storageOffset);
    _nfcStorage.write(storageOffset, *tagUid, 10);
    _nfcStorage.write(storageOffset + 10, tagName, 28);
    _nfcStorage.commit();

    enrollNfcStarted = delayTimerInit();
    enrollNfcId = nfcId;
    enrollNfcDuplicateId = ACC_ID_INVALID;
    
    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyWaitEnrollNfcFinished(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Wait until Enroll request finished");
    // logIndentUp();

    // resultData[0] true, if enroll request is finished
    resultData[0] = enrollNfcStarted == 0;
    resultLength = 1;
    if (enrollNfcStarted == 0)
    {
        // resultData[1] true, if enroll request was successful
        resultData[1] = (enrollNfcId != ACC_ID_INVALID && enrollNfcDuplicateId == ACC_ID_INVALID);
        // resultData[2] true, duplicate Nfc UID detected
        resultData[2] = enrollNfcDuplicateId > ACC_ID_INVALID;
        resultLength = 3;
        if (enrollNfcId != ACC_ID_INVALID || enrollNfcDuplicateId > ACC_ID_INVALID) // if successful or duplicate detected
        {
            // resultData[3-12] tag UID
            _nfcStorage.read(ACC_CalcNfcStorageOffset((uint32_t)(enrollNfcDuplicateId > ACC_ID_INVALID ? enrollNfcDuplicateId : enrollNfcId)), resultData + 3, 10);
            // resultData[13-14] duplicate Nfc ID
            resultData[13] = enrollNfcDuplicateId >> 8;
            resultData[14] = enrollNfcDuplicateId & 0xFF;
            resultLength = 15;
        }
    }
    // logIndentDown();
}


void AccessControl::handleFunctionPropertyChangeNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Change request");
    logIndentUp();

    uint16_t nfcId = (data[1] << 8) | data[2];
    logDebugP("nfcId: %d", nfcId);

    uint8_t tagUid[10] = {};
    memcpy(tagUid, data + 3, 10);

    bool tagUidEmpty = true;
    for (uint8_t i = 0; i < 10; i++)
    {
        if (tagUid[i] != 0)
        {
            tagUidEmpty = false;
            break;
        }
    }

    logDebugP("tagUid (empty=%u):", tagUidEmpty);
    logHexDebugP(tagUid, 10);

    uint8_t tagName[28] = {};
    bool tagNameEmpty = false;
    for (uint8_t i = 0; i < 28; i++)
    {
        memcpy(tagName + i, data + 13 + i, 1);
        if (tagName[i] == 0) // null termination
        {
            tagUidEmpty = i == 0;
            break;
        }
    }
    logDebugP("tagName: %s", tagName);

    uint32_t storageOffset = ACC_CalcNfcStorageOffset(nfcId);
    logDebugP("storageOffset: %d", storageOffset);
    if (!tagUidEmpty)
        _nfcStorage.write(storageOffset, tagUid, 10);
    if (!tagNameEmpty)
        _nfcStorage.write(storageOffset + 10, tagName, 28);
    _nfcStorage.commit();

    syncRequestedNfcId = nfcId;
    syncRequestedNfcTimer = delayTimerInit();

    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertySyncNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Sync request");
    logIndentUp();

    uint16_t nfcId = (data[1] << 8) | data[2];
    logDebugP("nfcId: %d", nfcId);

    syncRequestedNfcId = nfcId;
    syncRequestedNfcTimer = delayTimerInit();

    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyDeleteNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Delete request");
    logIndentUp();

    uint16_t nfcId = (data[1] << 8) | data[2];
    logDebugP("nfcId: %d", nfcId);

    bool success = deleteNfc(nfcId);
    
    resultData[0] = success ? 0 : 1;    
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyResetNfcScanner(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Reset scanner");
    logIndentUp();

    char nfcData[OPENKNX_ACC_FLASH_NFC_DATA_SIZE] = {}; // empty
    for (uint16_t i = 0; i < MAX_NFCS; i++)
    {
        uint32_t storageOffset = ACC_CalcNfcStorageOffset(i);
        _nfcStorage.write(storageOffset, *nfcData, OPENKNX_ACC_FLASH_NFC_DATA_SIZE);
    }
    _nfcStorage.commit();

    switchLedGreenPower(true);
    resetTouchPcbLedTimer = delayTimerInit();

    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertySearchTagByNfcId(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Search NFC-Tag by NfcId");
    logIndentUp();

    uint16_t nfcId = (data[1] << 8) | data[2];
    logDebugP("nfcId: %d", nfcId);

    uint32_t storageOffset = ACC_CalcNfcStorageOffset(nfcId);
    logDebugP("storageOffset: %d", storageOffset);

    uint8_t emptyTest[10] = {};
    uint8_t tagUid[10] = {};
    _nfcStorage.read(storageOffset, tagUid, 10);
    if (memcmp(emptyTest, tagUid, 10))
    {
        uint8_t tagName[28] = {};
        _nfcStorage.read(storageOffset + 10, tagName, 28);

        logDebugP("Found:");
        logIndentUp();
        logDebugP("tagUid:");
        logHexDebugP(tagUid, 10);
        logDebugP("tagName: %s", tagName);
        logIndentDown();

        resultData[0] = 0;
        memcpy(resultData + 1, tagUid, 10);
        resultLength = 11;
        for (uint8_t i = 0; i < 28; i++)
        {
            memcpy(resultData + 11 + i, tagName + i, 1);
            resultLength++;

            if (tagName[i] == 0) // null termination
                break;
        }
    }
    else
    {
        logDebugP("Not found.");

        resultData[0] = 1;
        resultLength = 1;
    }

    logIndentDown();
}

void AccessControl::handleFunctionPropertySearchNfcIdByTag(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property NFC: Search NfcId(s) by Tag Name");
    logIndentUp();

    uint8_t searchTagUid[10] = {};
    memcpy(searchTagUid, data + 1, 10);
    logDebugP("searchTagUid:");
    logHexDebugP(searchTagUid, 10);

    uint8_t emptyTest[10] = {};
    bool searchTagUidEmpty = !memcmp(emptyTest, searchTagUid, 10);

    char searchTagName[28] = {};
    uint8_t searchTagNameLength = 28;
    for (size_t i = 0; i < 28; i++)
    {
        memcpy(searchTagName + i, data + 11 + i, 1);
        if (searchTagName[i] == 0) // null termination
        {
            searchTagNameLength = i;
            break;
        }
    }
    logDebugP("searchTagName: %s (length: %u)", searchTagName, searchTagNameLength);
    logDebugP("resultLength: %u", resultLength);

    uint8_t recordLength = OPENKNX_ACC_FLASH_NFC_DATA_SIZE + 2;
    uint8_t foundCount = 0;
    uint16_t foundTotalCount = 0;

    uint32_t storageOffset = 0;
    uint8_t tagUid[10] = {};
    uint8_t tagName[28] = {};
    for (uint16_t nfcId = 0; nfcId < MAX_NFCS; nfcId++)
    {
        storageOffset = ACC_CalcNfcStorageOffset(nfcId);
        _nfcStorage.read(storageOffset, tagUid, 10);
        if (!searchTagUidEmpty)
        {
            if (memcmp(tagUid, searchTagUid, 10))
                continue;
        }

        _nfcStorage.read(storageOffset + 10, tagName, 28);
        if (strcasestr((char *)tagName, searchTagName) != nullptr && tagName[0] != 0)
        {
            // we return max. 5 results (3 + 5 * 40 = 203 bytes)
            if (foundCount < 5)
            {
                logDebugP("Found:");
                logIndentUp();
                logDebugP("nfcId: %d", nfcId);
                logDebugP("tagUid");
                logHexDebugP(tagUid, 10);
                logDebugP("tagName: %s", tagName);
                logIndentDown();

                resultData[3 + foundCount * recordLength] = nfcId >> 8;
                resultData[3 + foundCount * recordLength + 1] = nfcId;
                memcpy(resultData + 3 + foundCount * recordLength + 2, tagUid, 10);
                memcpy(resultData + 3 + foundCount * recordLength + 12, tagName, 28);

                foundCount++;
            }

            foundTotalCount++;
        }
    }
    
    resultData[0] = foundCount > 0 ? 0 : 1;
    resultData[1] = foundTotalCount >> 8;
    resultData[2] = foundTotalCount;
    resultLength = 3 + foundCount * recordLength;

    logDebugP("foundTotalCount: %u", foundTotalCount);
    logDebugP("returned resultLength: %u", resultLength);
    logIndentDown();
}


void AccessControl::handleFunctionPropertyChangeKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property keypad: Change request");
    logIndentUp();

    uint16_t keyId = (data[1] << 8) | data[2];
    logDebugP("keyId: %d", keyId);

    uint8_t codeUid[10] = {};
    memcpy(codeUid, data + 3, 10);

    bool codeUidEmpty = true;
    for (uint8_t i = 0; i < 10; i++)
    {
        if (codeUid[i] != 0)
        {
            codeUidEmpty = false;
            break;
        }
    }

    logDebugP("codeUid (empty=%u):", codeUidEmpty);
    logHexDebugP(codeUid, 10);

    uint8_t codeName[28] = {};
    bool codeNameEmpty = false;
    for (uint8_t i = 0; i < 28; i++)
    {
        memcpy(codeName + i, data + 13 + i, 1);
        if (codeName[i] == 0) // null termination
        {
            codeUidEmpty = i == 0;
            break;
        }
    }
    logDebugP("codeName: %s", codeName);

    uint32_t storageOffset = ACC_CalcKeyStorageOffset(keyId);
    logDebugP("storageOffset: %d", storageOffset);
    if (!codeUidEmpty)
        _keypadStorage.write(storageOffset, codeUid, 10);
    if (!codeNameEmpty)
        _keypadStorage.write(storageOffset + 10, codeName, 28);
    _keypadStorage.commit();

    syncRequestedKeyId = keyId;
    syncRequestedKeyTimer = delayTimerInit();

    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertySyncKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property keypad: Sync request");
    logIndentUp();

    uint16_t keyId = (data[1] << 8) | data[2];
    logDebugP("keyId: %d", keyId);

    syncRequestedKeyId = keyId;
    syncRequestedKeyTimer = delayTimerInit();

    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyDeleteKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property keypad: Delete request");
    logIndentUp();

    uint16_t keyId = (data[1] << 8) | data[2];
    logDebugP("keyId: %d", keyId);

    bool success = deleteKey(keyId);
    
    resultData[0] = success ? 0 : 1;    
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyResetKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property keypad: Reset keypad");
    logIndentUp();

    char keyData[OPENKNX_ACC_FLASH_KEY_DATA_SIZE] = {}; // empty
    for (uint16_t i = 0; i < MAX_KEYS; i++)
    {
        uint32_t storageOffset = ACC_CalcKeyStorageOffset(i);
        _keypadStorage.write(storageOffset, *keyData, OPENKNX_ACC_FLASH_KEY_DATA_SIZE);
    }
    _keypadStorage.commit();

    switchLedGreenPower(true);
    resetTouchPcbLedTimer = delayTimerInit();

    resultData[0] = 0;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertySearchCodeNameByCodeId(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property keypad: Search Keypad-Code by KeyId");
    logIndentUp();

    uint16_t keyId = (data[1] << 8) | data[2];
    logDebugP("keyId: %d", keyId);

    uint32_t storageOffset = ACC_CalcKeyStorageOffset(keyId);
    logDebugP("storageOffset: %d", storageOffset);

    uint8_t emptyTest[10] = {};
    uint8_t codeUid[10] = {};
    _keypadStorage.read(storageOffset, codeUid, 10);
    if (memcmp(emptyTest, codeUid, 10))
    {
        uint8_t codeName[28] = {};
        _keypadStorage.read(storageOffset + 10, codeName, 28);

        logDebugP("Found:");
        logIndentUp();
        logDebugP("codeUid:");
        logHexDebugP(codeUid, 10);
        logDebugP("codeName: %s", codeName);
        logIndentDown();

        resultData[0] = 0;
        memcpy(resultData + 1, codeUid, 10);
        resultLength = 11;
        for (uint8_t i = 0; i < 28; i++)
        {
            memcpy(resultData + 11 + i, codeName + i, 1);
            resultLength++;

            if (codeName[i] == 0) // null termination
                break;
        }
    }
    else
    {
        logDebugP("Not found.");

        resultData[0] = 1;
        resultLength = 1;
    }

    logIndentDown();
}

void AccessControl::handleFunctionPropertySearchCodeIdByCodeName(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property keypad: Search KeyId(s) by Code Name");
    logIndentUp();

    uint8_t searchCodeUid[10] = {};
    memcpy(searchCodeUid, data + 1, 10);
    logDebugP("searchCodeUid:");
    logHexDebugP(searchCodeUid, 10);

    uint8_t emptyTest[10] = {};
    bool searchCodeUidEmpty = !memcmp(emptyTest, searchCodeUid, 10);

    char searchCodeName[28] = {};
    uint8_t searchCodeNameLength = 28;
    for (size_t i = 0; i < 28; i++)
    {
        memcpy(searchCodeName + i, data + 11 + i, 1);
        if (searchCodeName[i] == 0) // null termination
        {
            searchCodeNameLength = i;
            break;
        }
    }
    logDebugP("searchCodeName: %s (length: %u)", searchCodeName, searchCodeNameLength);
    logDebugP("resultLength: %u", resultLength);

    uint8_t recordLength = OPENKNX_ACC_FLASH_KEY_DATA_SIZE + 2;
    uint8_t foundCount = 0;
    uint16_t foundTotalCount = 0;

    uint32_t storageOffset = 0;
    uint8_t codeUid[10] = {};
    uint8_t codeName[28] = {};
    for (uint16_t keyId = 0; keyId < MAX_KEYS; keyId++)
    {
        storageOffset = ACC_CalcKeyStorageOffset(keyId);
        _keypadStorage.read(storageOffset, codeUid, 10);
        if (!searchCodeUidEmpty)
        {
            if (memcmp(codeUid, searchCodeUid, 10))
                continue;
        }

        _keypadStorage.read(storageOffset + 10, codeName, 28);
        if (strcasestr((char *)codeName, searchCodeName) != nullptr && codeName[0] != 0)
        {
            // we return max. 5 results (3 + 5 * 40 = 203 bytes)
            if (foundCount < 5)
            {
                logDebugP("Found:");
                logIndentUp();
                logDebugP("keyId: %d", keyId);
                logDebugP("codeUid");
                logHexDebugP(codeUid, 10);
                logDebugP("codeName: %s", codeName);
                logIndentDown();

                resultData[3 + foundCount * recordLength] = keyId >> 8;
                resultData[3 + foundCount * recordLength + 1] = keyId;
                memcpy(resultData + 3 + foundCount * recordLength + 2, codeUid, 10);
                memcpy(resultData + 3 + foundCount * recordLength + 12, codeName, 28);

                foundCount++;
            }

            foundTotalCount++;
        }
    }
    
    resultData[0] = foundCount > 0 ? 0 : 1;
    resultData[1] = foundTotalCount >> 8;
    resultData[2] = foundTotalCount;
    resultLength = 3 + foundCount * recordLength;

    logDebugP("foundTotalCount: %u", foundTotalCount);
    logDebugP("returned resultLength: %u", resultLength);
    logIndentDown();
}

void AccessControl::handleFunctionPropertyWaitSyncSending(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property: Wait for Sync Sending");
    logIndentUp();
    if (syncSending) {
        resultData[0] = 0; // valid result
        resultData[1] = 1; // still sending
        resultData[2] = syncSendPacketSentCount;
        resultData[3] = syncSendPacketCount;
        resultLength = 4;
    }
    else
    {
        resultData[0] = 0; // valid result
        resultData[1] = 0; // send finished
        resultLength = 2;
    }
    logIndentDown();
}

void AccessControl::processAfterStartupDelay()
{
}

bool AccessControl::sendReadRequest(GroupObject &ko)
{
    // ensure, that we do not send too many read requests at the same time
    if (delayCheck(readRequestDelay, 300)) // 3 per second
    {
        // we handle input KO and we send only read requests, if KO is uninitialized
        if (!ko.initialized())
            ko.requestObjectRead();
        readRequestDelay = delayTimerInit();
        return true;
    }
    return false;
}

void AccessControl::savePower()
{
    switchFingerprintPower(false);
}

bool AccessControl::restorePower()
{
    if (ParamACC_ScanMode == 1)
        switchFingerprintPower(true);

    return true;
}

bool AccessControl::processCommand(const std::string cmd, bool diagnoseKo)
{
    bool result = false;

    if (cmd.substr(0, 3) != "acc" || cmd.length() < 5)
        return result;

    if (cmd.length() == 5 && cmd.substr(4, 1) == "h")
    {
        openknx.console.writeDiagenoseKo("-> pwr on");
        openknx.console.writeDiagenoseKo("");
        openknx.console.writeDiagenoseKo("-> pwr off");
        openknx.console.writeDiagenoseKo("");
        openknx.console.writeDiagenoseKo("-> fpi test");
        openknx.console.writeDiagenoseKo("");
        openknx.console.writeDiagenoseKo("-> fpi info");
        openknx.console.writeDiagenoseKo("");
        openknx.console.writeDiagenoseKo("-> fpi idx");
        openknx.console.writeDiagenoseKo("");
        openknx.console.writeDiagenoseKo("-> fpi led <0-9>");
        openknx.console.writeDiagenoseKo("");
    }
#ifdef SCANNER_PWR_PIN
    else if (cmd.length() == 10 && cmd.substr(4, 6) == "pwr on")
    {
        digitalWrite(SCANNER_PWR_PIN, FINGER_PWR_ON);
        result = true;
    }
    else if (cmd.length() == 11 && cmd.substr(4, 7) == "pwr off")
    {
        digitalWrite(SCANNER_PWR_PIN, FINGER_PWR_OFF);
        result = true;
    }
#endif
    // the test mode only arms the sequencer here and returns immediately, the script itself is
    // walked step by step from loop(bool) - which the framework also calls on an unconfigured device
    else if (cmd.length() == 13 && cmd.substr(4, 9) == "test mode")
    {
        startTestMode(0, false);
        result = true;
    }
    else if (cmd.length() == 13 && cmd.substr(4, 9) == "test nfc1")
    {
        startTestMode(1, false);
        result = true;
    }
    else if (cmd.length() == 13 && cmd.substr(4, 9) == "test nfc2")
    {
        startTestMode(2, false);
        result = true;
    }
    else if (cmd.length() == 12 && cmd.substr(4, 8) == "test key")
    {
        startTestMode(0, true);
        result = true;
    }
    else if (cmd.length() >= 8 && cmd.substr(4, 4) == "fpi ")
    {
        result = processCommandFingerprintInterface(cmd);
    }

    return result;
}

// ---------------------------------------------------------------------------------------
// scanner diagnostics, running against the productive driver
//
// "acc fpi info|idx|led <n>" answer straight from the live drivers state - they need no wire
// traffic beyond the LED latch and therefore work at any time, also in the middle of normal
// operation. "acc fpi test" is a small sequencer (processFingerprintTest(), pumped from
// loop(bool)): it claims the driver through the ownership arbiter (FpOwner::Test) so that a scan,
// an enrollment or a template transfer cannot run into it, brings a powered off or faulted scanner
// up itself for hardware triage, dumps the system parameters, waits 5 s for a finger and hands the
// arbiter back. Normal operation resumes by itself, no reboot is needed.
// ---------------------------------------------------------------------------------------

bool AccessControl::processCommandFingerprintInterface(const std::string cmd)
{
    if (cmd.length() == 12 && cmd.substr(8, 4) == "test")
    {
        if (ParamACC_FingerprintScanner == 3)
        {
            logInfoP("ETS scanner type is set to 3 (no fingerprint scanner), nothing to test.");
            return true;
        }

        if (testModeStep > 0)
        {
            logInfoP("The console test mode is running (step %u), try again afterwards.", testModeStep);
            return true;
        }

        if (fpiTestStep > 0)
        {
            logInfoP("Scanner test is already running (step %u).", fpiTestStep);
            return true;
        }

        fpiTestScanDone = false;
        fpiTestTimer = delayTimerInit();
        fpiTestStep = 1;
        return true;
    }

    if (cmd.length() == 12 && cmd.substr(8, 4) == "info")
    {
        logInfoP("Powered: %u, ready: %u, busy: %u (power state %u, owner %u)",
                 finger.isPoweredOn(), finger.isReady(), finger.isBusy(), (uint8_t)fpPower, (uint8_t)fpOwner);

        if (!finger.isReady())
        {
            logInfoP("Scanner is not ready, run 'acc fpi test' to bring it up.");
            return true;
        }

        // the dump walks the index cache, which does not fit into the loop time budget
        openknx.common.skipLooptimeWarning();
        finger.logSystemParameters();
        return true;
    }

    if (cmd.length() == 11 && cmd.substr(8, 3) == "idx")
    {
        if (!finger.isReady())
        {
            logInfoP("Scanner is not ready (power state %u), run 'acc fpi test' to bring it up.", (uint8_t)fpPower);
            return true;
        }

        // lists up to 1500 locations, so it never fits into the loop time budget
        openknx.common.skipLooptimeWarning();
        logInfoP("Template index (%u stored, valid up to %u):", finger.getTemplateCount(), finger.indexValidUpTo());
        logIndentUp();
        finger.forEachLocation([this](uint16_t location) { logInfoP("%u", location); });
        logIndentDown();
        logInfoP("Next free location: %u", finger.getNextFreeLocation());
        return true;
    }

    if (cmd.length() == 13 && cmd.substr(8, 4) == "led ")
    {
        char digit = cmd[12];
        if (digit < '0' || digit > '9')
        {
            logInfoP("Usage: acc fpi led <0-9>");
            return true;
        }

        if (!finger.isReady())
        {
            logInfoP("Scanner is not ready (power state %u), run 'acc fpi test' to bring it up.", (uint8_t)fpPower);
            return true;
        }

        FingerprintInterface::LedState state = (FingerprintInterface::LedState)(digit - '0');
        logInfoP("Set LED state %u", (uint8_t)state);
        // latched in the driver and pushed out at the next command boundary; a productive activity
        // (scan, LED reset timer) may of course overwrite it again afterwards
        finger.setLed(state);
        return true;
    }

    return false;
}

// Steps:
//   1  claim the scanner (FpOwner::Test), bring it up if it is not ready
//   2  wait for the bring-up, dump powered/ready/busy and the system parameters, LED WaitForFinger
//   3  search for a finger until one is found or the 5 s window is over
//   4  report, release the scanner
void AccessControl::processFingerprintTest()
{
    switch (fpiTestStep)
    {
        case 1:
            if (!arbiterAcquire(FpOwner::Test))
            {
                if (delayCheck(fpiTestTimer, FPI_TEST_START_TIMEOUT))
                {
                    logInfoP("Scanner test: the scanner is owned by %u, aborted.", (uint8_t)fpOwner);
                    fpiTestStep = 0;
                }
                return;
            }

            logInfoP("Scanner test:");

            // a powered off or faulted scanner is brought up here so the command stays useful for
            // triaging a possibly defective unit; a running one is used as it is
            if (fpPower != FpPowerState::Ready)
            {
                // the flash driver is only initialized by setup(), which the framework skips on an
                // unconfigured device - reading the stored password there returns garbage and would
                // fail the bring-up. The test mode bring-up of switchFingerprintPower() uses the
                // factory default 0 and leaves the status group object alone, which is exactly what an
                // unconfigured device needs; a configured one gets the productive bring-up (ETS model,
                // stored password, status group object) so normal operation is restored by it.
                bool unconfigured = !knx.configured();
                if (unconfigured)
                    logInfoP("Device is not configured, using the factory default password 0.");

                switchFingerprintPower(true, unconfigured);
            }

            fpiTestTimer = delayTimerInit();
            fpiTestStep = 2;
            return;

        case 2:
            // the driver gives a missing scanner up after about 5.5 s and reports Failed, the timeout
            // here is only a safety net
            if (fpPower == FpPowerState::Booting &&
                !delayCheck(fpiTestTimer, FPI_TEST_POWER_TIMEOUT))
                return;

            if (fpPower != FpPowerState::Ready)
            {
                logIndentUp();
                logInfoP("Fingerprint scanner not available (power=%u).", (uint8_t)fpPower);
                logIndentDown();
                fpiTestStep = 4;
                return;
            }

            logIndentUp();
            // the dump walks the index cache, which does not fit into the loop time budget
            openknx.common.skipLooptimeWarning();
            logInfoP("Powered: %u, ready: %u, busy: %u", finger.isPoweredOn(), finger.isReady(), finger.isBusy());
            finger.logSystemParameters();
            logIndentDown();

            logInfoP("Place a finger on the sensor now (test window 5 s)...");
            finger.setLed(FingerprintInterface::WaitForFinger);

            fpiTestScanDone = false;
            fpiTestWindowStart = delayTimerInit();
            fpiTestPollTimer = 0; // first attempt right away
            fpiTestStep = 3;
            return;

        case 3:
            if (fpiTestScanDone)
            {
                logIndentUp();
                if (fpiTestScanResult.ok())
                {
                    logInfoP("Test scan: match at location %u with score %u", fpiTestScanResult.location, fpiTestScanResult.score);
                    finger.setLed(FingerprintInterface::ScanMatch);
                }
                else
                {
                    logInfoP("Test scan: %s (raw 0x%02X)", fpStatusText(fpiTestScanResult.status), fpiTestScanResult.rawConfirmation);
                    finger.setLed(FingerprintInterface::ScanNoMatch);
                }
                logIndentDown();

                fpiTestStep = 4;
                return;
            }

            if (delayCheck(fpiTestWindowStart, FPI_TEST_SCAN_WINDOW))
            {
                // a search which is still in flight is awaited (its own ack timeouts bound this), so
                // the driver is idle again when the arbiter is handed back; a result which arrives
                // just now is still reported by the branch above
                if (finger.isBusy())
                    return;

                logIndentUp();
                logInfoP("Test scan: no finger detected within the test window.");
                logIndentDown();
                finger.setLed(FingerprintInterface::None);

                fpiTestStep = 4;
                return;
            }

            if (finger.isBusy() || !delayCheck(fpiTestPollTimer, FPI_TEST_SCAN_POLL))
                return;

            fpiTestPollTimer = delayTimerInit();
            // the completion callback only latches, the report is written by the next pass
            if (!finger.startSearchFinger([this](const FpResult &result) {
                    if (result.status == FpStatus::NoFinger)
                        return; // keep polling until the test window expires

                    fpiTestScanResult = result;
                    fpiTestScanDone = true;
                }))
                logDebugP("Test scan rejected by the driver (%s)", fpStatusText(finger.lastStartError()));
            return;

        default:
            logInfoP("Scanner test finished.");

            if (fpOwner == FpOwner::Test)
                arbiterRelease(FpOwner::Test);

            // the result LED is taken back by the productive reset timer, exactly like after a scan
            resetFingerLedTimer = delayTimerInit();
            fpiTestStep = 0;
            return;
    }
}

// ---------------------------------------------------------------------------------------
// non blocking test mode sequencer
//
// The test script is a step machine which is walked by processTestMode() from loop(bool), so it also
// works on an unconfigured device - where only loop(bool) is called - and never stalls the KNX stack.
//
// Steps:
//   1  claim the scanner (FpOwner::Test) and start its bring-up
//   2  wait for the bring-up, dump the system parameters, LED Success
//   3  (+1000 ms) LED off, LED pin modes, touch button LED red on
//   4  (+1000 ms) red off, green on
//   5  (+1000 ms) green off, prepare the relay test (or jump to 11 without a relay)
//   6  relay set impulse on
//   7  (+impulse) relay set impulse off
//   8  (+1000 ms) relay reset impulse on
//   9  (+impulse) relay reset impulse off
//   10 (+1000 ms) repeat 6..9 once (two iterations)
//   11 start the NFC sub test (or jump to 13)
//   12 pump the NFC reader until a tag was seen or 10 s are over
//   13 start the keypad sub test (or jump to 15)
//   14 pump the keypad until 100 s are over
//   15 "Testing finished", release the scanner
// ---------------------------------------------------------------------------------------

void AccessControl::startTestMode(uint8_t testModeNfc, bool testModeKeypad)
{
    if (testModeStep > 0)
    {
        logInfoP("Test mode is already running (step %u).", testModeStep);
        return;
    }

    // the test mode suppresses the whole pipeline including the "acc fpi test" sequencer, and both claim
    // FpOwner::Test - so a running scanner test is not interrupted but asked to finish first
    if (fpiTestStep > 0)
    {
        logInfoP("The scanner test is running (step %u), try again afterwards.", fpiTestStep);
        return;
    }

    this->testModeNfcType = testModeNfc;
    this->testModeKeypad = testModeKeypad;
    // reset on every run: the flag is also the exit condition of the sub test waits, so a tag which
    // was found by a previous run must not end the next one immediately
    testModeNfcFound = false;
    testModeRelayIteration = 0;
    testModeTimer = delayTimerInit();
    testModeStep = 1;
}

void AccessControl::processTestMode()
{
    switch (testModeStep)
    {
        case 1:
            // the scanner is claimed before its bring-up (the productive pipeline may own it, and on
            // an unconfigured device it was never powered at all)
            if (!arbiterAcquire(FpOwner::Test))
            {
                if (delayCheck(testModeTimer, TEST_MODE_POWER_TIMEOUT))
                {
                    logInfoP("Test mode: scanner is owned by %u, aborted.", (uint8_t)fpOwner);
                    testModeStep = 0;
                }
                return;
            }

            logInfoP("Starting test mode");
            logIndentUp();
            logInfoP("Testing scanner:");
            logIndentDown();

#ifdef SCANNER_PWR_PIN
            pinMode(SCANNER_PWR_PIN, OUTPUT);
#endif
            // testMode = true: the bring-up is done even for the ETS type "no fingerprint scanner" and
            // the scanner status group object is left alone
            switchFingerprintPower(true, true);

            testModeTimer = delayTimerInit();
            testModeStep = 2;
            return;

        case 2:
            // the bring-up runs asynchronously; the driver gives a missing scanner up after about
            // 5.5 s and reports Failed, the timeout here is only a safety net
            if (fpPower == FpPowerState::Booting &&
                !delayCheck(testModeTimer, TEST_MODE_POWER_TIMEOUT))
                return;

            logIndentUp();
            logIndentUp();
            if (fpPower == FpPowerState::Ready)
                finger.logSystemParameters();
            else
                logInfoP("Fingerprint scanner not available (power=%u).", (uint8_t)fpPower);
            logIndentDown();
            logIndentDown();

            finger.setLed(FingerprintInterface::Success);

            testModeTimer = delayTimerInit();
            testModeStep = 3;
            return;

        case 3:
            if (!delayCheck(testModeTimer, TEST_MODE_STEP_DELAY))
                return;

            finger.setLed(FingerprintInterface::None);

            logInfoP("Testing LEDs:");
            logIndentUp();

            if (testModeNfcType < 2)
            {
                openknx.gpio.pinMode(DIRECT_LED_GREEN_PIN, OUTPUT);
                openknx.gpio.pinMode(DIRECT_LED_RED_PIN, OUTPUT);
            }
            else
            {
                openknx.gpio.pinMode(EXTERN_LED_GREEN_PIN, OUTPUT);
                openknx.gpio.pinMode(EXTERN_LED_RED_PIN, OUTPUT);
            }

            logInfoP("Touch buttons red");
            logIndentDown();
            openknx.gpio.digitalWrite(testModeNfcType < 2 ? DIRECT_LED_RED_PIN : EXTERN_LED_RED_PIN, HIGH);

            testModeTimer = delayTimerInit();
            testModeStep = 4;
            return;

        case 4:
            if (!delayCheck(testModeTimer, TEST_MODE_STEP_DELAY))
                return;

            openknx.gpio.digitalWrite(testModeNfcType < 2 ? DIRECT_LED_RED_PIN : EXTERN_LED_RED_PIN, LOW);

            logIndentUp();
            logInfoP("Touch buttons green");
            logIndentDown();
            openknx.gpio.digitalWrite(testModeNfcType < 2 ? DIRECT_LED_GREEN_PIN : EXTERN_LED_GREEN_PIN, HIGH);

            testModeTimer = delayTimerInit();
            testModeStep = 5;
            return;

        case 5:
            if (!delayCheck(testModeTimer, TEST_MODE_STEP_DELAY))
                return;

            openknx.gpio.digitalWrite(testModeNfcType < 2 ? DIRECT_LED_GREEN_PIN : EXTERN_LED_GREEN_PIN, LOW);

#ifdef OPENKNX_SWA_SET_PINS
            logInfoP("Testing relay:");
            logIndentUp();
            logInfoP("Relay off");
            logIndentDown();
            pinMode(OPENKNX_SWA_SET_PINS, OUTPUT);
            pinMode(OPENKNX_SWA_RESET_PINS, OUTPUT);
            digitalWrite(OPENKNX_SWA_SET_PINS, OPENKNX_SWA_SET_ACTIVE_ON == HIGH ? LOW : HIGH);
            digitalWrite(OPENKNX_SWA_RESET_PINS, OPENKNX_SWA_RESET_ACTIVE_ON == HIGH ? LOW : HIGH);

            testModeRelayIteration = 0;
            testModeStep = 6;
#else
            testModeStep = 11;
#endif
            return;

#ifdef OPENKNX_SWA_SET_PINS
        case 6:
            logIndentUp();
            logInfoP("Relay set");
            logIndentDown();
            digitalWrite(OPENKNX_SWA_SET_PINS, OPENKNX_SWA_SET_ACTIVE_ON == HIGH ? HIGH : LOW);

            testModeTimer = delayTimerInit();
            testModeStep = 7;
            return;

        case 7:
            if (!delayCheck(testModeTimer, OPENKNX_SWA_BISTABLE_IMPULSE_LENGTH))
                return;

            digitalWrite(OPENKNX_SWA_SET_PINS, OPENKNX_SWA_SET_ACTIVE_ON == HIGH ? LOW : HIGH);

            testModeTimer = delayTimerInit();
            testModeStep = 8;
            return;

        case 8:
            if (!delayCheck(testModeTimer, TEST_MODE_STEP_DELAY))
                return;

            logIndentUp();
            logInfoP("Relay reset");
            logIndentDown();
            digitalWrite(OPENKNX_SWA_RESET_PINS, OPENKNX_SWA_RESET_ACTIVE_ON == HIGH ? HIGH : LOW);

            testModeTimer = delayTimerInit();
            testModeStep = 9;
            return;

        case 9:
            if (!delayCheck(testModeTimer, OPENKNX_SWA_BISTABLE_IMPULSE_LENGTH))
                return;

            digitalWrite(OPENKNX_SWA_RESET_PINS, OPENKNX_SWA_RESET_ACTIVE_ON == HIGH ? LOW : HIGH);

            testModeTimer = delayTimerInit();
            testModeStep = 10;
            return;

        case 10:
            if (!delayCheck(testModeTimer, TEST_MODE_STEP_DELAY))
                return;

            // two iterations
            testModeStep = ++testModeRelayIteration < 2 ? 6 : 11;
            return;
#endif

        case 11:
            if (testModeNfcType == 0)
            {
                testModeStep = 13;
                return;
            }

            logInfoP("Waiting for NFC tag:");
            initNfc(true, testModeNfcType);

            testModeTimer = delayTimerInit();
            testModeStep = 12;
            return;

        case 12:
            logIndentUp();
            loopNfc(true);
            logIndentDown();

            if (testModeNfcFound || delayCheck(testModeTimer, TEST_MODE_NFC_TIMEOUT))
                testModeStep = 13;
            return;

        case 13:
            // keypadBase only exists once setup() ran, i.e. only on a configured device
            if (!testModeKeypad || keypadBase == nullptr)
            {
                testModeStep = 15;
                return;
            }

            logInfoP("Waiting for keypad input:");
            keypadBase->runTestMode();

            testModeTimer = delayTimerInit();
            testModeStep = 14;
            return;

        case 14:
            logIndentUp();
            keypadBase->loop(true);
            logIndentDown();

            // testModeNfcFound is the exit flag of this wait as well; the keypad test never sets it,
            // so the window is always the full timeout
            if (testModeNfcFound || delayCheck(testModeTimer, TEST_MODE_KEYPAD_TIMEOUT))
                testModeStep = 15;
            return;

        default:
            logInfoP("Testing finished.");

            if (fpOwner == FpOwner::Test)
                arbiterRelease(FpOwner::Test);

            // the test brought the scanner up with the test password 0 and left the status group object
            // alone. A scanner which has a password set therefore ends up in Failed, and the continuous
            // scan pipeline never kicks a bring-up off itself, so scanning would stay dead until the
            // next reboot. The productive bring-up (real flash password, status group
            // object) is restarted here; fpPower is forced to Off first because
            // switchFingerprintPower() never repeats a bring-up which is Ready or Booting. Only on a
            // configured device: the ETS parameters and the password in the flash are not readable
            // before setup() ran, and there is no productive pipeline to restore either.
            if (knx.configured())
            {
                fpPower = FpPowerState::Off;
                fpPowerTestMode = false;
                switchFingerprintPower(true);

                // the driver only repeats a bring-up which faulted: a scanner which is already up (its
                // password matched the test password) reports no result anymore, so the ready state is
                // adopted right here instead of waiting for a callback which never comes
                if (fpPower == FpPowerState::Booting && finger.isReady())
                    onFingerprintReady(true);
            }

            testModeStep = 0;
            return;
    }
}

AccessControl openknxAccessControl;

// void AccessControl::writeFlash()
// {
//     for (size_t i = 0; i < flashSize(); i++)
//     {
//         //openknx.flash.writeByte(0xd0 + i);
//     }
// }

// void AccessControl::readFlash(const uint8_t* data, const uint16_t size)
// {
//     // printHEX("RESTORE:", data,  len);
// }

// uint16_t AccessControl::flashSize()
// {
//     return 10;
// }