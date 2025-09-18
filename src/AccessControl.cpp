#include "AccessControl.h"
#include "I2CDev.h"

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

#ifdef SCANNER_PWR_PIN
    pinMode(SCANNER_PWR_PIN, OUTPUT);
#endif    
    if (switchFingerprintPower(true))
        finger->logSystemParameters();

    for (uint16_t i = 0; i < ParamACC_VisibleActions; i++)
    {
        _channels[i] = new ActionChannel(i, finger);
        _channels[i]->setup();
    }

    pinMode(SCANNER_TOUCH_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(SCANNER_TOUCH_PIN), AccessControl::interruptDisplayTouched, FALLING);

    if (ParamACC_NfcScanner == 1)
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

    finger->setLed(Fingerprint::State::Success);

    KoACC_FingerLedRingColor.valueNoSend((uint8_t)0, Dpt(5, 10));
    KoACC_FingerLedRingControl.valueNoSend((uint8_t)FINGERPRINT_LED_OFF, Dpt(5, 10));
    KoACC_FingerLedRingSpeed.valueNoSend((uint8_t)0, Dpt(5, 10));
    KoACC_FingerLedRingCount.valueNoSend((uint8_t)0, Dpt(5, 10));

    checkSensorTimer = delayTimerInit();
    initResetTimer = delayTimerInit();

    initNfc();

    logInfoP("Fingerprint module ready.");
    logIndentDown();
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

    if (!testMode && ParamACC_NfcScanner == 2 ||
        testMode && testModeNfc == 2)
    {
        openknx.gpio.pinMode(0x0200, OUTPUT);
        openknx.gpio.digitalWrite(0x0200, LOW);
        openknx.gpio.pinMode(0x0201, OUTPUT);
        openknx.gpio.digitalWrite(0x0201, LOW);
    }

    PN7160Interface::initialize(NFC_IRQ_PIN, NFC_VEN_PIN, NFC_PN7160_ADDR);
    logInfoP("Initialized PN7160.");
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
        if (finger != nullptr)
        {
            logDebugP("Fingerprint power already on");
            return true;
        }

#ifdef SCANNER_PWR_PIN
        digitalWrite(SCANNER_PWR_PIN, FINGER_PWR_ON);
#endif
        initFingerprintScanner(testMode);

        logInfoP("Fingerprint start");
        bool success = finger->start();

        if (!testMode)
            KoACC_FingerScannerStatus.value(success, DPT_Switch);
        
        return success;
    }
    else
    {
        if (finger == nullptr)
        {
            logDebugP("Fingerprint power already off");
            return true;
        }

        finger->close();
        finger = nullptr;

#ifdef SCANNER_PWR_PIN
        digitalWrite(SCANNER_PWR_PIN, FINGER_PWR_OFF);
#endif
        return true;
    }
}

void AccessControl::switchLedGreenPower(bool on)
{
    if (ParamACC_NfcScanner == 0)
        return;
    
    openknx.gpio.digitalWrite(ParamACC_NfcScanner == 1 ? DIRECT_LED_GREEN_PIN : EXTERN_LED_GREEN_PIN, on ? HIGH : LOW);
}

void AccessControl::switchLedRedPower(bool on)
{
    if (ParamACC_NfcScanner == 0)
        return;
    
    openknx.gpio.digitalWrite(ParamACC_NfcScanner == 1 ? DIRECT_LED_RED_PIN : EXTERN_LED_RED_PIN, on ? HIGH : LOW);
}

void AccessControl::initFingerprintScanner(bool testMode)
{
    uint32_t scannerPassword = testMode ? 0 : _fingerprintStorage.readInt(FLASH_FINGER_SCANNER_PASSWORD_OFFSET);
    logDebugP("Initialize scanner with password: %u", scannerPassword);
    finger = new Fingerprint(AccessControl::delayCallback, scannerPassword);
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

void AccessControl::loop()
{
    if (delayCallbackActive)
        return;

    if (!isLocked)
    {
        if (ParamACC_ScanMode == 0)
        {
            if (touched)
            {
                logInfoP("Touched");
                KoACC_FingerTouched.value(true, DPT_Switch);

                if (switchFingerprintPower(true))
                {
                    unsigned long captureStart = delayTimerInit();
                    while (!delayCheck(captureStart, CAPTURE_RETRIES_TOUCH_TIMEOUT))
                    {
                        if (searchForFinger())
                            break;
                    }
                }

                touched = false;
            }
            else
            {
                if (KoACC_FingerTouched.value(DPT_Switch) &&
                    !finger->hasFinger())
                {
                    KoACC_FingerTouched.value(false, DPT_Switch);
                    shutdownSensorTimer = delayTimerInit();
                }
            }
        }
        else if (searchForFingerDelayTimer == 0 || delayCheck(searchForFingerDelayTimer, 100))
        {
            searchForFinger();
            searchForFingerDelayTimer = delayTimerInit();
        }

        if (enrollRequestedFingerTimer > 0 and delayCheck(enrollRequestedFingerTimer, ENROLL_REQUEST_DELAY))
        {
            bool success = enrollFinger(enrollRequestedFingerLocation);
            if (success)
            {
                syncRequestedFingerId = enrollRequestedFingerLocation;
                syncRequestedFingerTimer = delayTimerInit();
            }

            enrollRequestedFingerTimer = 0;
            enrollRequestedFingerLocation = 0;
        }

        if (checkSensorTimer > 0 && delayCheck(checkSensorTimer, CHECK_SENSOR_DELAY))
        {
            bool currentStatus = KoACC_FingerScannerStatus.value(DPT_Switch);
            bool success = finger->checkSensor();
            if (currentStatus != success)
            {
                KoACC_FingerScannerStatus.value(success, DPT_Switch);
                logInfoP("Check scanner status: %u", success);
            }

            checkSensorTimer = delayTimerInit();
        }

        if (shutdownSensorTimer > 0 && delayCheck(shutdownSensorTimer, SHUTDOWN_SENSOR_DELAY))
        {
            switchFingerprintPower(false);
            shutdownSensorTimer = 0;
        }

        if (initResetTimer > 0 && delayCheck(initResetTimer, INIT_RESET_TIMEOUT))
        {
            finger->setLed(Fingerprint::State::None);
            switchLedGreenPower(false);

            if (ParamACC_ScanMode == 0)
                switchFingerprintPower(false);

            initResetTimer = 0;
        }

        if (resetFingerLedTimer > 0 && delayCheck(resetFingerLedTimer, LED_RESET_TIMEOUT))
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

    if (syncRequestedFingerTimer > 0 && delayCheck(syncRequestedFingerTimer, SYNC_AFTER_ENROLL_DELAY))
    {
        startSyncSend(SyncType::FINGER, syncRequestedFingerId);

        syncRequestedFingerTimer = 0;
        syncRequestedFingerId = 0;
    }

    if (syncRequestedNfcTimer > 0 && delayCheck(syncRequestedNfcTimer, SYNC_AFTER_ENROLL_DELAY))
    {
        startSyncSend(SyncType::NFC, syncRequestedNfcId);

        syncRequestedNfcTimer = 0;
        syncRequestedNfcId = 0;
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
    KoACC_ScanAccessData.valueNoSend(foundId, Dpt(15, 1, 0));   // access identification code
    KoACC_ScanAccessData.valueNoSend(false, Dpt(15, 1, 1));     // detection error
    KoACC_ScanAccessData.valueNoSend(success, Dpt(15, 1, 2));   // permission accepted
    KoACC_ScanAccessData.valueNoSend(false, Dpt(15, 1, 3));     // read direction (not used)
    KoACC_ScanAccessData.valueNoSend(false, Dpt(15, 1, 4));     // encryption (not used for now)
    KoACC_ScanAccessData.value(syncType, Dpt(15, 1, 5));        // index of access identification code (used as type)
}

bool AccessControl::searchForFinger()
{
    if (!finger->hasFinger())
    {
        if (ParamACC_ScanMode == 1 &&
            KoACC_FingerTouched.value(DPT_Switch))
            KoACC_FingerTouched.value(false, DPT_Switch);

        hasLastFoundLocation = false;
        return false;
    }

    if (ParamACC_ScanMode == 1 &&
        !KoACC_FingerTouched.value(DPT_Switch))
        KoACC_FingerTouched.value(true, DPT_Switch);
    
    Fingerprint::FindFingerResult findFingerResult = finger->findFingerprint();

    if (findFingerResult.found)
    {
        if (ParamACC_ScanMode == 1 &&
            hasLastFoundLocation && lastFoundLocation == findFingerResult.location)
        {
            logDebugP("Same finger found in location %d and ignored", findFingerResult.location);
            resetFingerLedTimer = delayTimerInit();
            return true;
        }

        logInfoP("Finger found in location %d", findFingerResult.location);
        processFingerScanSuccess(findFingerResult.location);

        hasLastFoundLocation = true;
        lastFoundLocation = findFingerResult.location;
    }
    else
    {
        hasLastFoundLocation = false;
        finger->setLed(Fingerprint::ScanNoMatch);

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

void AccessControl::resetRingLed()
{
    finger->setLed(KoACC_FingerLedRingColor.value(Dpt(5, 10)), KoACC_FingerLedRingControl.value(Dpt(5, 10)), KoACC_FingerLedRingSpeed.value(Dpt(5, 10)), KoACC_FingerLedRingCount.value(Dpt(5, 10)));
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
            finger->setLed(Fingerprint::ScanMatch);
    }
    else
    {
        if (!external)
            finger->setLed(Fingerprint::ScanMatchNoAction);
        
        KoACC_FingerTouchedNoAction.value(true, DPT_Switch);
    }
}

bool AccessControl::enrollFinger(uint16_t location)
{
    logInfoP("Enroll request:");
    logIndentUp();

    bool success = switchFingerprintPower(true);
    if (success)
    {
        success = finger->createTemplate();
        if (success)
        {
            success = finger->storeTemplate(location);
            if (!success)
            {
                logInfoP("Storing template failed.");
            }
        }
        else
        {
            logInfoP("Creating template failed.");
        }
    }

    if (success)
    {
        logInfoP("Enrolled to location %d.", location);

        //###ToDo: remote management status feedback

        finger->setLed(Fingerprint::State::Success);
    }
    else
    {
        logInfoP("Enrolling template failed.");
        
        //###ToDo: remote management status feedback

        finger->setLed(Fingerprint::State::Failed);
    }

    logIndentDown();
    resetFingerLedTimer = delayTimerInit();

    return success;
}

bool AccessControl::deleteFinger(uint16_t location, bool sync)
{
    logInfoP("Delete request:");
    logIndentUp();

    bool success = switchFingerprintPower(true);
    if (success)
        success = finger->deleteTemplate(location);

    if (success)
    {
        logInfoP("Template deleted from location %d.", location);
        
        //###ToDo: remote management status feedback

        if (sync)
            startSyncDelete(SyncType::FINGER, location);
    }
    else
    {
        logInfoP("Deleting template failed.");
        
        //###ToDo: remote management status feedback
    }

    logIndentDown();
    resetFingerLedTimer = delayTimerInit();

    return success;
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
            finger->setLed(Fingerprint::State::Locked);
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
        success = switchFingerprintPower(true);
        if (success)
        {
            location = finger->getNextFreeLocation();
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

void AccessControl::startSyncDelete(SyncType syncType, uint16_t deleteId)
{
    if (!ParamACC_EnableSync ||
        syncReceiving)
        return;

    logInfoP("Sync-Send (syncType=%u): delete: deleteId=%u", syncType, deleteId);

    /*
    Sync Delete Packet Layout:
    -   0: 1 byte : sequence number (0: control packet)
    -   1: 1 byte : sync type (0: new finger, 1: delete finger, 10: new NFC, 11: delete NFC)
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

void AccessControl::startSyncSend(SyncType syncType, uint16_t syncId, bool loadModel)
{
    if (!ParamACC_EnableSync ||
        syncReceiving)
        return;

    logInfoP("Sync-Send (syncType=%u): started: syncId=%u, loadModel=%u, syncDelay=%u", syncId, loadModel, ParamACC_SyncDelay);

    uint8_t syncTypeCode = 0;
    uint8_t syncSendBufferTemp[SYNC_BUFFER_SIZE];
    uint32_t storageOffset = 0;
    uint8_t syncData[max(OPENKNX_ACC_FLASH_FINGER_DATA_SIZE, OPENKNX_ACC_FLASH_NFC_DATA_SIZE)] = {};
    switch (syncType)
    {
        case SyncType::FINGER:
            syncTypeCode = 0;

            if (!switchFingerprintPower(true))
            {
                logErrorP("Sync-Send (syncType=%u): powering scanner on failed", syncType);
                return;
            }
    
            finger->setLed(Fingerprint::State::Busy);
    
            bool success;
            if (loadModel)
            {
                success = finger->loadTemplate(syncId);
                if (!success)
                {
                    logErrorP("Sync-Send (syncType=%u): loading template failed", syncType);
                    return;
                }
            }
    
            success = finger->retrieveTemplate(syncSendBufferTemp);
            if (!success)
            {
                logErrorP("Sync-Send (syncType=%u): retrieving template failed", syncType);
                return;
            }
    
            resetRingLed();

            storageOffset = ACC_CalcFingerStorageOffset(syncId);
            _fingerprintStorage.read(storageOffset, syncData, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);
            memcpy(syncSendBufferTemp + TEMPLATE_SIZE, syncData, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);        
            break;
        case SyncType::NFC:
            syncTypeCode = 10;

            storageOffset = ACC_CalcNfcStorageOffset(syncId);
            _nfcStorage.read(storageOffset, syncData, OPENKNX_ACC_FLASH_NFC_DATA_SIZE);
            memcpy(syncSendBufferTemp, syncData, OPENKNX_ACC_FLASH_NFC_DATA_SIZE);        
            break;
        default:
            logErrorP("Sync-Send (syncType=%u): delete: Unsupported sync type", syncType);
            return;
    }

    const int maxDstSize = LZ4_compressBound(SYNC_BUFFER_SIZE);
    const int compressedDataSize = LZ4_compress_default((char*)syncSendBufferTemp, (char*)syncSendBuffer, SYNC_BUFFER_SIZE, maxDstSize);

    syncSendBufferLength = compressedDataSize;
    syncSendPacketCount = ceil(syncSendBufferLength / (float)SYNC_SEND_PACKET_DATA_LENGTH) + 1; // currently separated control packet
    uint16_t checksum = crc16.ccitt(syncSendBuffer, syncSendBufferLength);

    logDebugP("Sync-Send (syncType=%u, 1/%u): control packet: bufferLength=%u, lengthPerPacket=%u, checksum=%u, fingerId=%u%", syncType, syncSendPacketCount, syncSendBufferLength, SYNC_SEND_PACKET_DATA_LENGTH, checksum, syncId);

    /*
    Sync Control Packet Layout:
    -    0: 1 byte : sequence number (0: control packet)
    -    1: 1 byte : sync type (0: new finger, 1: delete finger, 10: new NFC, 11: delete NFC)
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
    if (syncSending)
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
        uint16_t syncDeleteFingerId;
        uint16_t syncDeleteNfcId;
        switch (data[1]) // sync type
        {
            case 0: // new finger
            case 10: // new NFC
                syncReceiveType = data[1] == 0 ? SyncType::FINGER : SyncType::NFC;
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

                return;
            case 1: // delete finger
                if (data[2] != 0)
                {
                    logInfoP("Sync-Receive (delete finger): Unsupported sync version: %u", data[2]);
                    return;
                }

                syncDeleteFingerId = (data[3] << 8) | data[4];
                logDebugP("Sync-Receive (delete finger): fingerId=%u", syncDeleteFingerId);

                deleteFinger(syncDeleteFingerId, false);

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
                return;
            }

            finger->setLed(Fingerprint::State::Busy);
        }

        uint16_t checksum = crc16.ccitt(syncReceiveBuffer, syncReceiveBufferLength);
        if (syncReceiveBufferChecksum == checksum)
            logDebugP("Sync-Receive (syncType=%u): finished (checksum=%u)", syncReceiveType, syncReceiveBufferChecksum);
        else
        {
            logErrorP("Sync-Receive (syncType=%u): finished failed (checksum expected=%u, calculated=%u)", syncReceiveType, syncReceiveBufferChecksum, checksum);

            if (syncReceiveType == SyncType::FINGER)
            {
                finger->setLed(Fingerprint::State::Failed);
                resetFingerLedTimer = delayTimerInit();
            }

            return;
        }

        uint8_t syncSendBufferTemp[SYNC_BUFFER_SIZE];
        const int decompressedSize = LZ4_decompress_safe((char*)syncReceiveBuffer, (char*)syncSendBufferTemp, syncReceiveBufferLength, SYNC_BUFFER_SIZE);
        if (decompressedSize != SYNC_BUFFER_SIZE)
        {
            logErrorP("Sync-Receive (syncType=%u): decompression failed (size expected=%u, received=%u)", syncReceiveType, SYNC_BUFFER_SIZE, decompressedSize);

            if (syncReceiveType == SyncType::FINGER)
            {
                finger->setLed(Fingerprint::State::Failed);
                resetFingerLedTimer = delayTimerInit();
            }

            return;
        }

        uint32_t storageOffset = 0;
        switch (syncReceiveType)
        {
            case SyncType::FINGER:
                if (!finger->sendTemplate(syncSendBufferTemp))
                {
                    logErrorP("Sync-Receive (syncType=%u): sending finger template failed", syncReceiveType);
                    finger->setLed(Fingerprint::State::Failed);
                    resetFingerLedTimer = delayTimerInit();
                    return;
                }

                if (!finger->storeTemplate(syncReceiveSyncId))
                {
                    logErrorP("Sync-Receive (syncType=%u): storing finger template failed", syncReceiveType);
                    finger->setLed(Fingerprint::State::Failed);
                    resetFingerLedTimer = delayTimerInit();
                    return;
                }

                storageOffset = ACC_CalcFingerStorageOffset(syncReceiveSyncId);
                _fingerprintStorage.write(storageOffset, syncSendBufferTemp + TEMPLATE_SIZE, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);
                _fingerprintStorage.commit();

                finger->setLed(Fingerprint::State::Success);
                resetFingerLedTimer = delayTimerInit();
                break;
            case SyncType::NFC:
                storageOffset = ACC_CalcNfcStorageOffset(syncReceiveSyncId);
                _nfcStorage.write(storageOffset, syncSendBufferTemp, OPENKNX_ACC_FLASH_NFC_DATA_SIZE);
                _nfcStorage.commit();
                break;
        }

        logInfoP("Sync-Receive (syncType=%u): data stored", syncReceiveType);
        syncReceiving = false;
    }
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
        // as long as enroll is not finished, return progress
        resultData[1] = finger->enrollProgress;
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
        if (finger->hasLocation(fingerId))
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
        if (finger->hasLocation(fingerId))
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

    bool success = deleteFinger(fingerId);
    
    resultData[0] = success ? 0 : 1;
    resultLength = 1;
    logIndentDown();
}

void AccessControl::handleFunctionPropertyResetFingerScanner(uint8_t *data, uint8_t *resultData, uint8_t &resultLength)
{
    logInfoP("Function property finger: Reset scanner");
    logIndentUp();

    bool success = false;
    if (switchFingerprintPower(true))
    {
        char fingerData[OPENKNX_ACC_FLASH_FINGER_DATA_SIZE] = {}; // empty
        for (uint16_t i = 0; i < MAX_FINGERS; i++)
        {
            uint32_t storageOffset = ACC_CalcFingerStorageOffset(i);
            _fingerprintStorage.write(storageOffset, *fingerData, OPENKNX_ACC_FLASH_FINGER_DATA_SIZE);
        }
        _fingerprintStorage.commit();

        success = finger->emptyDatabase();
        resetFingerLedTimer = delayTimerInit();
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
        if (!finger->hasLocation(fingerId))
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
        uint16_t* fingerIds = finger->getLocations();
        uint16_t templateCount = finger->getTemplateCount();

        uint32_t storageOffset = 0;
        uint8_t personFinger = 0;
        uint8_t personName[28] = {};
        for (uint16_t i = 0; i < templateCount; i++)
        {
            uint16_t fingerId = fingerIds[i];
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

        logInfoP("Setting new fingerprint scanner password.");
        logIndentUp();

        if (switchFingerprintPower(true))
            success = finger->setPassword(newPasswordCrc);
        
        resetFingerLedTimer = delayTimerInit();
        logInfoP(success ? "Success." : "Failed.");
        logIndentDown();
        
        if (success)
        {
            logDebugP("Saving new password in flash.");
            _fingerprintStorage.writeInt(FLASH_FINGER_SCANNER_PASSWORD_OFFSET, newPasswordCrc);
            _fingerprintStorage.commit();

            finger->close();
            initFingerprintScanner();
            finger->start();
        }

        resetFingerLedTimer = delayTimerInit();
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

void AccessControl::processAfterStartupDelay()
{
}

void AccessControl::delayCallback(uint32_t period)
{
    uint32_t start = delayTimerInit();
    delayCallbackActive = true;

    while (!delayCheck(start, period))
        openknx.loop();

    openknx.common.skipLooptimeWarning();
    delayCallbackActive = false;
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
    else if (cmd.length() == 13 && cmd.substr(4, 9) == "test mode")
    {
        runTestMode(0);
        result = true;
    }
    else if (cmd.length() == 13 && cmd.substr(4, 9) == "test nfc1")
    {
        runTestMode(1);
        result = true;
    }
    else if (cmd.length() == 13 && cmd.substr(4, 9) == "test nfc2")
    {
        runTestMode(2);
        result = true;
    }

    return result;
}

void AccessControl::runTestMode(uint8_t testModeNfc)
{
    logInfoP("Starting test mode");
    logIndentUp();

    logInfoP("Testing scanner:");
    logIndentUp();
#ifdef SCANNER_PWR_PIN
    pinMode(SCANNER_PWR_PIN, OUTPUT);
#endif
    if (switchFingerprintPower(true, true))
        finger->logSystemParameters();
    finger->setLed(Fingerprint::State::Success);
    logIndentDown();
    delay(1000);
    finger->setLed(Fingerprint::State::None);

    logInfoP("Testing LEDs:");
    logIndentUp();

    if (testModeNfc < 2)
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
    openknx.gpio.digitalWrite(testModeNfc < 2 ? DIRECT_LED_RED_PIN : EXTERN_LED_RED_PIN, HIGH);
    delay(1000);
    openknx.gpio.digitalWrite(testModeNfc < 2 ? DIRECT_LED_RED_PIN : EXTERN_LED_RED_PIN, LOW);

    logInfoP("Touch buttons green");
    openknx.gpio.digitalWrite(testModeNfc < 2 ? DIRECT_LED_GREEN_PIN : EXTERN_LED_GREEN_PIN, HIGH);
    delay(1000);
    openknx.gpio.digitalWrite(testModeNfc < 2 ? DIRECT_LED_GREEN_PIN : EXTERN_LED_GREEN_PIN, LOW);
    logIndentDown();

#ifdef OPENKNX_SWA_SET_PINS
    logInfoP("Testing relay:");
    logIndentUp();
    logInfoP("Relay off");
    pinMode(OPENKNX_SWA_SET_PINS, OUTPUT);
    pinMode(OPENKNX_SWA_RESET_PINS, OUTPUT);
    digitalWrite(OPENKNX_SWA_SET_PINS, OPENKNX_SWA_SET_ACTIVE_ON == HIGH ? LOW : HIGH);
    digitalWrite(OPENKNX_SWA_RESET_PINS, OPENKNX_SWA_RESET_ACTIVE_ON == HIGH ? LOW : HIGH);
    for (uint8_t i = 0; i < 2; i++)
    {
        logInfoP("Relay set");
        digitalWrite(OPENKNX_SWA_SET_PINS, OPENKNX_SWA_SET_ACTIVE_ON == HIGH ? HIGH : LOW);
        delay(OPENKNX_SWA_BISTABLE_IMPULSE_LENGTH);
        digitalWrite(OPENKNX_SWA_SET_PINS, OPENKNX_SWA_SET_ACTIVE_ON == HIGH ? LOW : HIGH);
        delay(1000);
        logInfoP("Relay reset");
        digitalWrite(OPENKNX_SWA_RESET_PINS, OPENKNX_SWA_RESET_ACTIVE_ON == HIGH ? HIGH : LOW);
        delay(OPENKNX_SWA_BISTABLE_IMPULSE_LENGTH);
        digitalWrite(OPENKNX_SWA_RESET_PINS, OPENKNX_SWA_RESET_ACTIVE_ON == HIGH ? LOW : HIGH);
        delay(1000);
    }
    logIndentDown();
#endif

    if (testModeNfc > 0)
    {
        logInfoP("Waiting for NFC tag:");
        logIndentUp();
        initNfc(true, testModeNfc);
        u_int32_t nfcWaitTimer = delayTimerInit();
        while (!testModeNfcFound && !delayCheck(nfcWaitTimer, 10000))
            loopNfc(true);
        logIndentDown();
    }

    logInfoP("Testing finished.");
    logIndentDown();
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