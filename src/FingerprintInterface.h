#pragma once

#include "FingerprintProtocol.h"
#include "FingerprintTypes.h"
#include "OpenKNX.h"

#include <string>

// Strictly non-blocking driver for GROW R503/R503S/R503Pro fingerprint modules.
// One operation is in flight at a time; its result is delivered exactly once through a callback
// which is only ever invoked from loop(). The only fire-and-forget path is the LED latch, which
// is flushed at command boundaries.
class FingerprintInterface
{
  public:
    // KNX facing LED semantics. The numeric values are part of the external behaviour (they are
    // accepted by "acc fpi led <n>" and mirror the documented LED states), so they must not be
    // renumbered.
    enum LedState : uint8_t
    {
        None = 0,
        ScanFinger = 1,
        ScanMatch,
        ScanMatchNoAction,
        ScanNoMatch,
        EnrollCreateModel,
        WaitForFinger,
        RemoveFinger,
        DeleteNotFound,
        Success,
        Failed,
        Locked,
        Busy
    };

    std::string logPrefix();

    // ---- lifecycle ------------------------------------------------------------------
    // setModel() and setPassword() have to be called before powerOn(): the bring-up verifies the
    // password and checks the library size the sensor reports against the configured model.
    // powerOn() only starts the bring-up, its outcome is published by the ready callback.
    void init();
    void loop();
    bool powerOn();
    void powerOff();
    bool isPoweredOn() const { return _poweredOn; }
    void setPassword(uint32_t password);
    uint32_t password() const { return _password; }
    void setModel(FpModel model);
    FpModel model() const { return _model; }
    bool isReady() const { return _bringUp == BringUp::Ready; }
    bool isBusy() const;
    void setReadyCallback(FpReadyCallback callback) { _readyCallback = callback; }
    void cancelOperation();
    FpStatus lastStartError() const { return _startError; }
    const FpResult &lastResult() const { return _lastResult; }

    // ---- LED latch (never fails, latest wins) ---------------------------------------
    void setLed(LedState state);
    void setLedRaw(uint8_t control, uint8_t speed, uint8_t color, uint8_t count);
    LedState ledState() const { return _ledState; }

    // ---- model profile and capabilities --------------------------------------------
    uint16_t templateSize() const;
    uint16_t libraryCapacity() const;
    bool supportsCommand(FpCommand command) const;
    bool supportsLedColor(uint8_t color) const;
    const FpSysPara &sysPara() const { return _sysPara; }
    const FpProdInfo &prodInfo() const { return _prodInfo; }
    const char *algorithmVersion() const { return _algVersion; }
    const char *firmwareVersion() const { return _fwVersion; }
    void logSystemParameters();

    // ---- index cache (answers without wire traffic) ---------------------------------
    uint16_t getTemplateCount() const;
    bool hasLocation(uint16_t location) const;
    uint16_t getNextFreeLocation() const;
    void forEachLocation(std::function<void(uint16_t location)> visitor) const;
    uint16_t indexValidUpTo() const { return _indexValidUpTo; }
    bool startRefreshIndex(FpCallback callback);

    // ---- raw commands ---------------------------------------------------------------
    // Every startX() returns true when the operation was accepted - its callback then fires exactly
    // once from loop() - and false when it was rejected (not ready, busy, invalid parameter, not
    // supported by the model); the reason is available from lastStartError().
    bool startGenImg(FpCallback callback);
    bool startGenChar(uint8_t bufferId, FpCallback callback);
    bool startMatch(FpCallback callback);
    bool startSearch(uint8_t bufferId, uint16_t startId, uint16_t num, FpCallback callback);
    bool startRegModel(FpCallback callback);
    bool startStore(uint8_t bufferId, uint16_t location, FpCallback callback);
    bool startLoadChar(uint8_t bufferId, uint16_t location, FpCallback callback);
    bool startUpChar(uint8_t bufferId, uint8_t *destination, uint16_t destinationSize, FpCallback callback);
    bool startDownChar(uint8_t bufferId, const uint8_t *source, uint16_t length, FpCallback callback);
    bool startUpImage(FpDataSink sink, FpCallback callback);
    bool startDownImage(FpDataSource source, uint32_t length, FpCallback callback);
    bool startDeletChar(uint16_t startId, uint16_t count, FpCallback callback);
    bool startEmpty(FpCallback callback);
    bool startSetSysPara(uint8_t parameter, uint8_t value, FpCallback callback);
    bool startReadSysPara(FpCallback callback);
    bool startSetPwd(uint32_t password, FpCallback callback);
    bool startVfyPwd(uint32_t password, FpCallback callback);
    bool startGetRandomCode(uint8_t *destination, FpCallback callback);
    bool startSetAdder(uint32_t address, FpCallback callback);
    bool startReadInfPage(uint8_t *destination, uint16_t destinationSize, FpCallback callback);
    bool startWriteNotepad(uint8_t page, const uint8_t *content, FpCallback callback);
    bool startReadNotepad(uint8_t page, uint8_t *destination, FpCallback callback);
    bool startTempleteNum(FpCallback callback);
    bool startReadIndexTable(uint8_t page, uint8_t *destination, FpCallback callback);
    bool startGetImageEx(FpCallback callback);
    bool startCancel(FpCallback callback);
    bool startAuraLedConfig(uint8_t control, uint8_t speed, uint8_t color, uint8_t count, FpCallback callback);
    bool startCheckSensor(FpCallback callback);
    bool startGetAlgVer(uint8_t *destination, FpCallback callback);
    bool startGetFwVer(uint8_t *destination, FpCallback callback);
    bool startReadProdInfo(FpCallback callback);
    bool startSoftRst(FpCallback callback);
    bool startHandShake(FpCallback callback);

    // ---- composite operations -------------------------------------------------------
    bool startDetectFinger(FpCallback callback);
    bool startSearchFinger(FpCallback callback);
    bool startEnroll(uint16_t location, FpProgressCallback progressCallback, FpCallback callback);
    // progress of the running (or of the most recently finished) enrollment: 0 = not started,
    // 1-6 = waiting for capture n, 7 = create model, 8 = store. Reset by startEnroll(), so a poll
    // never sees the value of a previous run once a new enrollment is under way.
    uint8_t enrollProgress() const { return _enrollProgress; }
    bool startRetrieveTemplate(uint16_t location, uint8_t *destination, uint16_t destinationSize, FpCallback callback);
    bool startStoreTemplate(uint16_t location, const uint8_t *source, uint16_t length, FpCallback callback);
    bool startDeleteTemplate(uint16_t location, FpCallback callback);
    bool startEmptyDatabase(FpCallback callback);
    bool startSetPassword(uint32_t newPassword, FpCallback callback);
    bool startHealthCheck(FpCallback callback);
    bool startAutoEnroll(uint16_t location, bool allowOverwrite, bool allowDuplicate, bool requireLeave,
                         FpStepCallback stepCallback, FpCallback callback);
    bool startAutoIdentify(uint8_t securityLevel, uint16_t startId, uint16_t num, uint8_t maxErrors,
                           FpStepCallback stepCallback, FpCallback callback);

  private:
    enum class Phase : uint8_t
    {
        Idle = 0,
        TxFrame,
        WaitAck,
        RxData,
        TxData,
        PostDataGuard,
        WaitMoreAcks
    };

    enum class BringUp : uint8_t
    {
        Off = 0,
        PowerStabilize,
        BootWait,
        VerifyPwd,
        ReadParams,
        ReadProduct,
        ReadAlgVer,
        ReadFwVer,
        ReadCount,
        ReadIndex,
        Ready,
        Fault
    };

    enum class Op : uint8_t
    {
        None = 0,
        BringUp,
        Raw,
        SearchFinger,
        Enroll,
        RetrieveTemplate,
        StoreTemplate,
        DeleteTemplate,
        EmptyDatabase,
        SetPassword,
        HealthCheck,
        RefreshIndex,
        MultiAck
    };

    // ---- transport and command layer ------------------------------------------------
    void processTransport();
    void processCommand();
    void processOperation();
    void flushLedLatch();
    void finishPowerOff();
    void failBringUp(const char *reason, FpStatus status);

    void handleLinkEvent(FingerprintLink::Event event);
    void onAckFrame();
    void onDataFrame(bool last);
    void parseAckPayload();

    void flushReceive();
    void resetDataBindings();
    void stageParams(const uint8_t *params, uint8_t paramLength);
    bool issueCommand(FpCommand command, const uint8_t *params, uint8_t paramLength);
    bool issueStagedCommand();
    bool sendStagedCommand();
    bool commandTaken();
    void completeCommand(FpStatus status);
    void retryOrFail(FpStatus status);
    bool buildNextTxDataPacket();
    uint16_t packetSize() const;

    uint32_t ackTimeoutFor(FpCommand command) const;
    uint32_t dataTimeoutFor(FpCommand command) const;
    static bool isRetryable(FpCommand command);
    static bool isRxDataCommand(FpCommand command);
    static bool isTxDataCommand(FpCommand command);
    static bool isMultiAckCommand(FpCommand command);
    static bool isTraced(FpCommand command);

    // ---- operation layer ------------------------------------------------------------
    bool beginOperation(Op op, FpCallback callback);
    bool beginRawCommand(FpCommand command, const uint8_t *params, uint8_t paramLength, FpCallback callback);
    void finishOperation(FpStatus status);
    void reportProgress(uint8_t progress);

    void processBringUp();
    void processRaw();
    void processSearchFinger();
    void processEnroll();
    void processRetrieveTemplate();
    void processStoreTemplate();
    void processDeleteTemplate();
    void processEmptyDatabase();
    void processSetPassword();
    void processHealthCheck();
    void processRefreshIndex();
    void processMultiAck();
    bool advanceIndexPage();

    void applyIndexPage(uint8_t page, const uint8_t *bitmap);
    void markLocation(uint16_t location, bool used);
    void clearIndex();
    uint8_t indexPageCount() const;
    void parseSysPara(const uint8_t *payload, uint16_t length);
    void parseProdInfo(const uint8_t *payload, uint16_t length);
    void ledParams(LedState state, uint8_t &control, uint8_t &speed, uint8_t &color, uint8_t &count) const;

    // ---- state ----------------------------------------------------------------------
    FingerprintLink _link;

    FpModel _model = FpModel::R503;
    uint32_t _password = 0;
    uint32_t _pendingPassword = 0;
    bool _poweredOn = false;
    bool _powerOffPending = false;
    uint32_t _powerOffTimer = 0;
    bool _bootByteSeen = false;
    bool _sensorMaybeRebooted = false;
    BringUp _bringUp = BringUp::Off;
    uint32_t _bringUpTimer = 0;
    uint8_t _bringUpRetries = 0;
    uint8_t _indexPage = 0;

    FpReadyCallback _readyCallback = nullptr;

    Phase _phase = Phase::Idle;
    FpCommand _cmd = FpCommand::NoCommand;
    FpCommand _stagedCmd = FpCommand::NoCommand;
    uint8_t _paramBuf[40] = {};
    uint8_t _paramLength = 0;
    uint32_t _cmdTimer = 0;
    uint32_t _cmdTimeout = 0;
    uint8_t _cmdRetries = 0;
    bool _cmdDone = false;
    bool _retryPending = false;
    FpResult _cmdResult = {};
    FpResult _lastResult = {};
    FpStatus _startError = FpStatus::Ok;
    uint32_t _pendingAddress = FpProto::DefaultAddress;
    uint32_t _previousAddress = FpProto::DefaultAddress;

    uint8_t _txBuf[FpProto::MaxFrame] = {};
    uint16_t _txLength = 0;
    uint16_t _txSent = 0;

    uint8_t *_dataDst = nullptr;
    uint16_t _dataDstSize = 0;
    uint16_t _dataReceived = 0;
    const uint8_t *_dataSrc = nullptr;
    uint32_t _dataSrcLength = 0;
    uint32_t _dataSrcSent = 0;
    FpDataSink _dataSink = nullptr;
    FpDataSource _dataSource = nullptr;
    bool _dataOverflow = false;

    uint8_t _multiAckStep = 0;
    bool _multiAckStepPending = false;
    uint32_t _multiAckStart = 0;
    bool _cancelPending = false;
    bool _cancelSent = false;

    Op _op = Op::None;
    uint8_t _opStep = 0;
    uint32_t _opTimer = 0;
    uint32_t _pollTimer = 0;
    uint8_t _opRetries = 0;
    uint16_t _opLocation = 0;
    uint8_t _opCapture = 0;
    uint8_t _enrollProgress = 0;
    FpCallback _opCallback = nullptr;
    FpProgressCallback _opProgressCallback = nullptr;
    FpStepCallback _opStepCallback = nullptr;

    bool _ledPending = false;
    bool _ledInFlight = false;
    LedState _ledState = None;
    uint8_t _ledControl = 0;
    uint8_t _ledSpeed = 0;
    uint8_t _ledColor = 0;
    uint8_t _ledCount = 0;

    FpSysPara _sysPara = {};
    FpProdInfo _prodInfo = {};
    char _algVersion[FP_VERSION_STRING_SIZE + 1] = {};
    char _fwVersion[FP_VERSION_STRING_SIZE + 1] = {};

    uint16_t _templateCount = 0;
    uint16_t _indexValidUpTo = 0;
    uint8_t _index[FP_INDEX_BITMAP_SIZE] = {};
    uint8_t _indexScratch[FP_NOTEPAD_PAGE_SIZE] = {};
};
