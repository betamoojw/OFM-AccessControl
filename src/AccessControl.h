#define _GNU_SOURCE

#include "OpenKNX.h"
#include "hardware.h"
#include "FingerprintInterface.h"
#include "KeypadBase.h"
#include "ActionChannel.h"
#include "FastCRC.h"
#include "lz4.h"
#include "pn7160interface/pn7160interface.hpp"
#include "logging/logging.hpp"
#include "nci/nci.hpp"

#define INIT_RESET_TIMEOUT 1000
#define LED_RESET_TIMEOUT 1000
#define LED_RESET_FAST_TIMEOUT 250
#define ENROLL_REQUEST_DELAY 100
#define CAPTURE_RETRIES_TOUCH_TIMEOUT 500
// pacing of the asynchronous finger removal probes; probing on every loop pass would hammer the UART
#define SCAN_REMOVE_PROBE_DELAY 100
// throttle of the debug logs which report an activity waiting for a foreign scanner owner
#define FP_DEFER_LOG_DELAY 1000
// upper bound for an enroll request waiting for the scanner (bring-up, running scan, locked
// module); after that the request is failed so the ETS enroll poll always gets a final answer.
// Generous on purpose, same class as the other start timeouts: a running enrollment, a template
// transfer or a flash operation can legitimately hold the scanner for about a minute.
#define ENROLL_START_TIMEOUT 90000
#define CHECK_SENSOR_DELAY 1000
#define SHUTDOWN_SENSOR_DELAY 3000
// safety net for a queued maintenance operation which never gets the scanner (dead scanner, an
// owner which never lets go). Generous on purpose: a running enrollment may hold the scanner for
// about a minute.
#define MAINT_START_TIMEOUT 90000
// depth of the queue for the deletes which arrive as a sync broadcast from another device.
// They are not answered to anybody, so they must not be lost when a second one arrives while the
// first is still waiting for the scanner (the ETS slot is one deep and answers optimistically).
#define BUS_DELETE_QUEUE_SIZE 4
// upper bound for the asynchronous scanner bring-up of the test mode sequencer; the driver
// itself gives a missing scanner up after about 5.5 s
#define TEST_MODE_POWER_TIMEOUT 8000
// pacing of the test mode sequencer steps and of its relay impulses
#define TEST_MODE_STEP_DELAY 1000
#define TEST_MODE_NFC_TIMEOUT 10000
#define TEST_MODE_KEYPAD_TIMEOUT 100000
// the "acc fpi ..." scanner diagnostics. They run against the productive driver, so they wait for
// the ownership arbiter (a running scan, enrollment or template transfer keeps them waiting for a
// moment) and bring a powered off or faulted scanner up themselves.
#define FPI_TEST_START_TIMEOUT 8000
#define FPI_TEST_POWER_TIMEOUT 8000
// window in which a finger is expected on the sensor, and the pacing of the search attempts
#define FPI_TEST_SCAN_WINDOW 5000
#define FPI_TEST_SCAN_POLL 200

#define ACC_ID_INVALID 65535

#define MAX_FINGERS 1500
#define OPENKNX_ACC_FLASH_FINGER_MAGIC_WORD 2912744758
#define OPENKNX_ACC_FLASH_FINGER_DATA_SIZE 29 // 1 byte: which finger, 28 bytes: person name
#define ACC_CalcFingerStorageOffset(fingerId) fingerId * OPENKNX_ACC_FLASH_FINGER_DATA_SIZE + 4096 + 1 // first byte free for finger info storage format version
#define FLASH_FINGER_SCANNER_PASSWORD_OFFSET 5

#define MAX_NFCS 1500
#define OPENKNX_ACC_FLASH_NFC_MAGIC_WORD 1983749238
#define OPENKNX_ACC_FLASH_NFC_DATA_SIZE 38 // 10 byte: NFC tag UID, 28 bytes: person name
#define ACC_CalcNfcStorageOffset(nfcId) nfcId * OPENKNX_ACC_FLASH_NFC_DATA_SIZE + 4096 + 1 // first byte free for NFC info storage format version
#define NFC_ENROLL_TIMEOUT 10000
#define NFC_ENROLL_LED_BLINK_INTERVAL 250

#define MAX_KEYS 1500
#define MAX_KEY_LEN 10
#define OPENKNX_ACC_FLASH_KEY_MAGIC_WORD 4021287134
#define OPENKNX_ACC_FLASH_KEY_DATA_SIZE 38 // 10 byte: key code, 28 bytes: person name
#define ACC_CalcKeyStorageOffset(keyId) keyId * OPENKNX_ACC_FLASH_KEY_DATA_SIZE + 4096 + 1 // first byte free for keypad info storage format version

// the KNX sync wire format is frozen to the largest template of all supported models: the person
// data always starts at offset FP_TEMPLATE_SIZE_MAX (1536), also for a 512 byte R503Pro template
#define SYNC_BUFFER_SIZE FP_TEMPLATE_SIZE_MAX + OPENKNX_ACC_FLASH_FINGER_DATA_SIZE
#define SYNC_SEND_PACKET_DATA_LENGTH 13
#define SYNC_AFTER_ENROLL_DELAY 500
#define SYNC_IGNORE_DELAY 500
// safety net for a received finger template whose import never gets the scanner (dead scanner,
// an owner which never lets go); the import is failed after that so the sync path cannot deadlock.
// Generous on purpose: a running enrollment may hold the scanner for about a minute.
#define SYNC_IMPORT_START_TIMEOUT 90000
// the same safety net for the other direction: a finger broadcast which never gets the scanner must
// not stay armed forever, that would block every later sync send behind it as well.
#define SYNC_SEND_START_TIMEOUT 90000
// safety net for an incoming broadcast which never completes (a sender which is power cycled or
// reprogrammed mid transfer, packets lost on the bus). It is refreshed on every received packet, so it
// only has to cover the gap between two packets of the same broadcast, not the whole transfer: the
// sender paces them with its own ParamACC_SyncDelay, whose parameter type allows at most 255 ms. Ten
// times that maximum, rounded up to a 5 s floor, leaves room for bus retransmissions and for a sender
// whose loop is busy with a flash commit, while still freeing the sync path within a few seconds.
#define SYNC_RECEIVE_PACKET_TIMEOUT 5000

#ifdef SCANNER_PWR_PIN
  #define FINGER_PWR_ON    SCANNER_PWR_PIN_ACTIVE_ON == HIGH ? HIGH : LOW
  #define FINGER_PWR_OFF   SCANNER_PWR_PIN_ACTIVE_ON == HIGH ? LOW : HIGH
#endif


/*
Flash Storage Layout:
- 0-3: 4 bytes: int magic word
-   4: 1 byte main storage version format (currently 0)
- 5-8: 4 bytes: int fingerprint scanner password
*/

class AccessControl : public OpenKNX::Module
{
  public:
    void dispatchAuthAction(bool started); // global dispatcher, replaces pointer in each action instance
    // the scanner driver, the "acc fpi" diagnostics and the test mode sequencer have to run on an
    // unconfigured device as well, so the module hooks into the loop which the framework calls
    // unconditionally; the productive pipeline below only runs when knx.configured() is true
    void loop(bool configured) override;
    void loop() override;
    void setup() override;
    void processAfterStartupDelay() override;
    void processInputKo(GroupObject &ko) override;
		bool processFunctionProperty(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength) override;
		// bool processFunctionPropertyState(uint8_t objectIndex, uint8_t propertyId, uint8_t length, uint8_t *data, uint8_t *resultData, uint8_t &resultLength) override;
    bool sendReadRequest(GroupObject &ko);

    const std::string name() override;
    const std::string version() override;
    void savePower() override;
    bool restorePower() override;
    bool processCommand(const std::string cmd, bool diagnoseKo);
    // void writeFlash() override;
    // void readFlash(const uint8_t* data, const uint16_t size) override;
    // uint16_t flashSize() override;

  private:
    enum SyncType : uint8_t
    {
        FINGER,
        NFC,
        KEY
    };

    // asynchronous scanner bring-up: switchFingerprintPower() only kicks the driver off, the
    // result is published by the ready callback (onFingerprintReady)
    enum class FpPowerState : uint8_t
    {
        Off = 0,
        Booting,
        Ready,
        Failed
    };

    // module level ownership arbiter for the single flight scanner driver: exactly one activity
    // may drive the scanner at a time. The scan pipeline, the enrollment, both sync template
    // transfers, the ETS maintenance operations and the console test mode all acquire it.
    enum class FpOwner : uint8_t
    {
        None = 0,
        Scan,
        Enroll,
        SyncSend,
        SyncReceive,
        Maintenance,
        Test
    };

    // asynchronous scan pipeline: Scanning covers one startSearchFinger() in flight (touch mode
    // re-issues it until the capture window is over), WaitRemove polls startDetectFinger() until
    // the finger has been taken off the sensor
    enum class ScanState : uint8_t
    {
        Idle = 0,
        Scanning,
        WaitRemove
    };

    // the three ETS maintenance operations. They validate everything they can answer from local
    // state synchronously, answer the ETS function property optimistically and are then executed
    // asynchronously from loop() under FpOwner::Maintenance (converting them to a request/poll pair
    // would need knxprod changes, which are out of scope).
    enum class MaintOp : uint8_t
    {
        None = 0,
        DeleteFinger,
        EmptyDatabase,
        SetPassword
    };

    enum TerminalChar : uint8_t
    {
        None = 0,
        Hash,
        Asterisk,
        Function,
        Bell,
        Key,
        Clear
    };    

    static constexpr char keypadKeymap[] = {'\0', '#', '*', 'F', 'B', 'K', 'C'};

    static void interruptDisplayTouched();
    static void interruptTouchLeft();
    static void interruptTouchRight();
    bool switchFingerprintPower(bool on, bool testMode = false);
    void onFingerprintReady(bool ready);
    bool arbiterAcquire(FpOwner who);
    void arbiterRelease(FpOwner who);
    void switchLedGreenPower(bool on);
    void switchLedRedPower(bool on);
    void initFlashFingerprint();
    void initFlashNfc();
    void initFlashKeypad();
    void initNfc(bool testMode = false, uint8_t testModeNfc = 0);
    void loopNfc(bool testMode = false);
    void onKeypadKeyPressed(char key);
    void clearKeypadBuffer(KeypadBase::FeedbackType feedbackType);
    bool checkKeypadCode(char* code, bool checkForFail);
    void processFingerScanSuccess(uint16_t location, bool external = false);
    void processNfcScanSuccess(uint16_t nfcId, bool external = false);
    void processKeypadScanSuccess(uint16_t codeId, bool external = false);
    bool startEnrollFinger(uint16_t location);
    void processEnrollResult();
    // the asynchronous delete/empty-database/set-password operations. Queueing is done by the ETS
    // handlers (one deep) and by the sync receive path (own queue), the trigger and the finalization
    // run from loop().
    bool maintenanceBusy() const;
    void queueBusDelete(uint16_t location);
    void processMaintenanceStart();
    void processMaintenanceResult();
    bool startMaintenanceOp(MaintOp op, uint16_t location, bool sendSync);
    bool deleteNfc(uint16_t nfcId, bool sync = true);
    bool deleteKey(uint16_t keyId, bool sync = true);
    void sendScanAccessData(SyncType syncType, bool success, uint16_t foundId = 0);
    void processScanStateMachine();
    bool startScanSearch();
    bool startScanRemoveProbe();
    bool processScanResult(const FpResult &result);
    void finishTouchedRemoval();
    void resetRingLed();
    // the LED ring group objects go straight to the drivers LED latch; color 0 means "the KO
    // default", which is substituted by white (part of the external group object behaviour)
    void setLedRingRaw(uint8_t color, uint8_t control, uint8_t speed, uint8_t count);
    void startSyncDelete(SyncType syncType, uint16_t deleteId);
    // true = the request has been consumed (started or definitively dropped), false = the caller has
    // to keep it armed and retry on a later pass
    bool startSyncSend(SyncType syncType, uint16_t syncId, bool loadModel = true);
    void sendSyncControlPacket(uint8_t syncTypeCode, uint16_t syncId);
    void processSyncExportResult();
    void processSyncImportStart();
    void processSyncImportResult();
    void processSyncSend();
    void processSyncReceive(uint8_t* data);
    void processInputKoLock(GroupObject &ko);
    void processInputKoTouchPcbLed(GroupObject &ko);
    void processInputKoEnrollFinger(GroupObject &ko);
    void processInputKoEnrollNfc(GroupObject &ko);
    void processInputKoKeypadBacklight(GroupObject &ko);
    void processInputKoKeypadFeedbackLed(GroupObject &ko);
    void handleFunctionPropertyEnrollFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyWaitEnrollFingerFinished(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyChangeFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySyncFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyDeleteFinger(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyResetFingerScanner(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySearchPersonByFingerId(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySearchFingerIdByPerson(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySetFingerPassword(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyEnrollNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyWaitEnrollNfcFinished(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyChangeNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySyncNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyDeleteNfc(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyResetNfcScanner(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySearchTagByNfcId(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySearchNfcIdByTag(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyChangeKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySyncKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyDeleteKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyResetKeypad(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySearchCodeNameByCodeId(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertySearchCodeIdByCodeName(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    void handleFunctionPropertyWaitSyncSending(uint8_t *data, uint8_t *resultData, uint8_t &resultLength);
    // "acc test mode" arms the sequencer and returns immediately, processTestMode() then walks the
    // test script step by step from loop()
    void startTestMode(uint8_t testModeNfc, bool testModeKeypad);
    void processTestMode();
    // scanner diagnostics ("acc fpi test|info|idx|led <n>"), running against the productive driver.
    // info/idx/led answer straight from its live state, test is a small sequencer which holds
    // FpOwner::Test for the duration of its scan window.
    bool processCommandFingerprintInterface(const std::string cmd);
    void processFingerprintTest();

    FastCRC16 crc16;
    FastCRC32 crc32;

    OpenKNX::Flash::Driver _fingerprintStorage;
    OpenKNX::Flash::Driver _nfcStorage;
    OpenKNX::Flash::Driver _keypadStorage;
    ActionChannel *_channels[ACC_ChannelCount];

    // ---- scanner driver, bring-up state and ownership arbiter ----
    // every byte on the wire and every cached answer comes from this strictly non blocking driver
    FingerprintInterface finger;
    FpPowerState fpPower = FpPowerState::Off;
    FpOwner fpOwner = FpOwner::None;

    // ---- scan pipeline state machine ----
    ScanState scanState = ScanState::Idle;
    // touch mode capture window (CAPTURE_RETRIES_TOUCH_TIMEOUT)
    uint32_t scanWindowStart = 0;
    // paces the finger removal probes
    uint32_t scanRemoveProbeTimer = 0;
    // true while one removal probe is in flight - and only then does ScanState::WaitRemove hold
    // FpOwner::Scan. The arbiter is handed back between two probes so a finger which rests on the
    // sensor cannot starve the other scanner activities (see processScanStateMachine()).
    bool scanRemoveProbing = false;
    // throttle the "deferred" logs while another activity owns the scanner
    uint32_t touchDeferLogTimer = 0;
    uint32_t enrollDeferLogTimer = 0;
    uint32_t syncDeferLogTimer = 0;
    // set by the driver completion callback, evaluated by the state machine in the next loop pass
    // (no volatile needed: the callback is invoked from the driver loop, never from an interrupt)
    bool scanDone = false;
    FpResult scanResult = {};
    bool fpPowerTestMode = false;
    bool fpScannerDisabled = false;
    bool hasLastFoundLocation = false;
    uint16_t lastFoundLocation = 0;
    uint32_t initResetTimer = 0;
    // the boot Success flash is owed until the first bring-up published its result. It cannot be
    // derived from initResetTimer alone: a bring-up which takes longer than INIT_RESET_TIMEOUT meets
    // an already cleared timer and would skip the flash silently.
    bool initBootLedPending = true;
    uint32_t resetFingerLedTimer = 0;
    uint32_t resetTouchPcbLedTimer = 0;
    uint32_t resetTouchPcbLedTimerFast = 0;
    uint32_t enrollRequestedFingerTimer = 0;
    uint16_t enrollRequestedFingerLocation = 0;

    // ---- asynchronous enrollment ----
    // The two request fields above stay armed for the whole run (the ETS wait poll reads
    // enrollRequestedFingerTimer as "in progress"), enrollActive marks the run itself and enrollDone
    // latches the completion callback for the finalization in loop().
    bool enrollActive = false;
    uint16_t enrollActiveLocation = 0;
    bool enrollDone = false;
    bool enrollSuccess = false;
    FpStatus enrollResult = FpStatus::Ok;
    uint32_t enrollNfcStarted = 0;
    uint16_t enrollNfcId = ACC_ID_INVALID;
    uint16_t enrollNfcDuplicateId = ACC_ID_INVALID;
    bool enrollNfcLedOn = false;
    uint32_t enrollNfcLedLastChanged = 0;
    uint32_t checkSensorTimer = 0;
    uint32_t searchForFingerDelayTimer = 0;
    uint32_t shutdownSensorTimer = 0;

    // ---- maintenance queue ----
    // The ETS originated slot is one deep on purpose: the ETS answer is given optimistically when
    // the operation is queued, so a second request which arrives while one is still pending has to
    // be answered with the failure code (unchanged ETS behaviour).
    MaintOp maintPending = MaintOp::None;
    uint16_t maintDeleteLocation = 0;
    bool maintDeleteSendSync = false;
    uint32_t maintNewPasswordCrc = 0;
    uint32_t maintPendingTimer = 0;
    uint32_t maintDeferLogTimer = 0;
    // the operation which is in flight; its parameters are copied out of the queue above so a
    // queued bus delete can never change them underneath a running one
    MaintOp maintActiveOp = MaintOp::None;
    uint16_t maintActiveLocation = 0;
    bool maintActiveSendSync = false;
    bool maintActive = false;
    bool maintDone = false;
    bool maintOk = false;
    FpStatus maintResult = FpStatus::Ok;
    // the deletes which arrive as a sync broadcast from another device. Nobody is waiting for an
    // answer, so they are never dropped but queued and drained ahead of the ETS slot (a delete is
    // the cheapest of the three operations).
    uint16_t busDeleteQueue[BUS_DELETE_QUEUE_SIZE] = {};
    uint8_t busDeleteQueueHead = 0;
    uint8_t busDeleteQueueCount = 0;
    uint32_t busDeletePendingTimer = 0;
    uint32_t busDeleteDeferLogTimer = 0;

    // ---- console test mode sequencer ----
    // state of the non blocking test mode sequencer (0 = not running). While it runs, the whole
    // productive pipeline is suppressed.
    uint8_t testModeStep = 0;
    uint32_t testModeTimer = 0;
    uint8_t testModeNfcType = 0;
    bool testModeKeypad = false;
    uint8_t testModeRelayIteration = 0;

    // ---- "acc fpi test" scanner diagnostics sequencer ----
    // state of the sequencer (0 = not running). Unlike the test mode it does not suppress the
    // productive pipeline - it only holds FpOwner::Test, so scanning simply defers for the length of
    // the run and resumes by itself afterwards.
    uint8_t fpiTestStep = 0;
    uint32_t fpiTestTimer = 0;
    uint32_t fpiTestWindowStart = 0;
    uint32_t fpiTestPollTimer = 0;
    // set by the search completion callback, evaluated by the sequencer in the next loop pass
    bool fpiTestScanDone = false;
    FpResult fpiTestScanResult = {};

    inline volatile static bool touched = false;
    inline volatile static bool touchLeftTouched = false;
    inline volatile static bool touchRightTouched = false;
    bool isLocked = false;

    uint32_t syncIgnoreTimer = 0;

    bool syncSending = false;
    uint32_t syncSendTimer = 0;
    uint8_t syncSendBuffer[SYNC_BUFFER_SIZE];
    uint16_t syncSendBufferLength = 0;
    uint8_t syncSendPacketCount = 0;
    uint8_t syncSendPacketSentCount = 0;
    uint32_t syncRequestedFingerTimer = 0;
    uint16_t syncRequestedFingerId = 0;
    uint32_t syncRequestedNfcTimer = 0;
    uint16_t syncRequestedNfcId = 0;
    uint32_t syncRequestedKeyTimer = 0;
    uint16_t syncRequestedKeyId = 0;

    bool syncReceiving = false;
    // armed by the control packet and refreshed by every packet of the transfer, cleared as soon as the
    // reception has ended (stored, queued for the template import, or failed); see the timeout in loop()
    uint32_t syncReceiveLastPacketTimer = 0;
    SyncType syncReceiveType;
    uint16_t syncReceiveSyncId = 0;
    uint8_t syncReceiveBuffer[SYNC_BUFFER_SIZE];
    uint16_t syncReceiveBufferLength = 0;
    uint16_t syncReceiveBufferChecksum = 0;
    uint8_t syncReceiveLengthPerPacket = 0;
    uint8_t syncReceivePacketCount = 0;
    uint8_t syncReceivePacketReceivedCount = 0;
    bool syncReceivePacketReceived[SYNC_BUFFER_SIZE] = {false};

    // ---- sync buffers ----
    // The UNCOMPRESSED sync payload of both directions; they are members (not stack buffers) because
    // the asynchronous template export/import keeps using them across loop passes. Both are zeroed
    // at the start of every fill: the person data sits at the frozen offset FP_TEMPLATE_SIZE_MAX and
    // everything a (possibly only 512 byte long, R503Pro) template or a 38 byte NFC/key record does
    // not fill has to compress away instead of being random garbage. syncSendBuffer and
    // syncReceiveBuffer above hold the compressed form.
    uint8_t syncExportBuffer[SYNC_BUFFER_SIZE];
    uint8_t syncImportBuffer[SYNC_BUFFER_SIZE];
    // asynchronous template export = sync send phase 1; syncSending is set by phase 2 only
    bool syncExportActive = false;
    bool syncExportDone = false;
    bool syncExportOk = false;
    FpStatus syncExportResult = FpStatus::Ok;
    uint16_t syncExportSyncId = 0;
    // asynchronous template import of a received sync; syncReceiving stays true until it finished
    bool syncImportPending = false;
    bool syncImportActive = false;
    bool syncImportDone = false;
    bool syncImportOk = false;
    FpStatus syncImportResult = FpStatus::Ok;
    uint32_t syncImportPendingTimer = 0;
    uint32_t syncImportDeferLogTimer = 0;

    uint32_t readRequestDelay = 0;

    bool testModeNfcFound = false;

    static KeypadBase *keypadBase;
    uint32_t keypadLastKeypressTimer = 0;
    char keypadPreviousKey = '\0';
    char keypadCode[MAX_KEY_LEN + 1] =  {0};
    int8_t keypadCodePosition = -1;
};

extern AccessControl openknxAccessControl;