#pragma once

#include <cstdint>
#include <functional>

// largest template of all supported models (R503/R503S); the KNX sync wire format is frozen to it
#define FP_TEMPLATE_SIZE_MAX 1536
// index bitmap covering the largest library (R503Pro: 1500 slots)
#define FP_INDEX_BITMAP_SIZE 192
#define FP_LOCATION_NONE 0xFFFF
#define FP_NOTEPAD_PAGE_SIZE 32
#define FP_VERSION_STRING_SIZE 32

enum class FpModel : uint8_t
{
    R503 = 0,   // R503 and R503S: 1536 byte templates, 200 slots, GetImageEx/CheckSensor/ReadProdInfo/SoftRst
    R503Pro = 1 // 512 byte templates, 1500 slots, extended LED colors
};

enum class FpCommand : uint8_t
{
    GenImg = 0x01,
    GenChar = 0x02,
    Match = 0x03,
    Search = 0x04,
    RegModel = 0x05,
    Store = 0x06,
    LoadChar = 0x07,
    UpChar = 0x08,
    DownChar = 0x09,
    UpImage = 0x0A,
    DownImage = 0x0B,
    DeletChar = 0x0C,
    Empty = 0x0D,
    SetSysPara = 0x0E,
    ReadSysPara = 0x0F,
    SetPwd = 0x12,
    VfyPwd = 0x13,
    GetRandomCode = 0x14,
    SetAdder = 0x15,
    ReadInfPage = 0x16,
    WriteNotepad = 0x18,
    ReadNotepad = 0x19,
    TempleteNum = 0x1D,
    ReadIndexTable = 0x1F,
    GetImageEx = 0x28,
    Cancel = 0x30,
    AutoEnroll = 0x31,
    AutoIdentify = 0x32,
    AuraLedConfig = 0x35,
    CheckSensor = 0x36,
    GetAlgVer = 0x39,
    GetFwVer = 0x3A,
    ReadProdInfo = 0x3C,
    SoftRst = 0x3D,
    HandShake = 0x40,
    NoCommand = 0xFF
};

// sensor confirmation codes keep their wire values, driver errors live in 0xE0-0xEF
enum class FpStatus : uint8_t
{
    Ok = 0x00,
    PacketReceiveError = 0x01,
    NoFinger = 0x02,
    ImageFail = 0x03,
    ImageMessy = 0x06,
    FeatureFail = 0x07,
    NoMatch = 0x08,
    NotFound = 0x09,
    EnrollMismatch = 0x0A,
    BadLocation = 0x0B,
    DbRangeFail = 0x0C,
    UploadFeatureFail = 0x0D,
    PacketResponseFail = 0x0E,
    UploadFail = 0x0F,
    DeleteFail = 0x10,
    DbClearFail = 0x11,
    PasswordFail = 0x13,
    InvalidImage = 0x15,
    FlashError = 0x18,
    NoDefinitionError = 0x19,
    InvalidRegister = 0x1A,
    InvalidRegisterConfig = 0x1B,
    InvalidNotepadPage = 0x1C,
    CommPortFail = 0x1D,
    LibraryFull = 0x1F,
    AddressCodeError = 0x20,
    PasswordRequired = 0x21,
    TemplateEmpty = 0x22,
    LibraryEmpty = 0x24,
    SensorTimeout = 0x26,
    FingerExists = 0x27,
    SensorHardwareError = 0x29,

    ErrTimeoutAck = 0xE0,
    ErrTimeoutData = 0xE1,
    ErrInterByte = 0xE2,
    ErrChecksum = 0xE3,
    ErrFraming = 0xE4,
    ErrAddress = 0xE5,
    ErrNotReady = 0xE6,
    ErrBusy = 0xE7,
    ErrPoweredOff = 0xE8,
    ErrCancelled = 0xE9,
    ErrUnsupportedModel = 0xEA,
    ErrBufferTooSmall = 0xEB,
    ErrInvalidParam = 0xEC,
    ErrBringUpFailed = 0xED,

    UnsupportedCommand = 0xFC,
    HardwareError = 0xFD,
    ExecutionFailure = 0xFE
};

enum class FpLedControl : uint8_t
{
    Breathing = 0x01,
    Flashing = 0x02,
    On = 0x03,
    Off = 0x04,
    GradualOn = 0x05,
    GradualOff = 0x06
};

enum class FpLedColor : uint8_t
{
    Red = 0x01,
    Blue = 0x02,
    Purple = 0x03,
    Green = 0x04,
    Yellow = 0x05,
    Cyan = 0x06,
    White = 0x07,
    Rgb3Color = 0x20,   // R503Pro only
    Rgb7Color = 0x30    // R503Pro only
};

struct FpSysPara
{
    uint16_t statusRegister;
    uint16_t systemId;
    uint16_t librarySize;
    uint16_t securityLevel;
    uint32_t deviceAddress;
    uint16_t packetSizeCode;
    uint16_t packetSize;
    uint16_t baudFactor;
    uint32_t baudRate;
};

struct FpProdInfo
{
    char moduleType[17];
    char batchNumber[5];
    char serialNumber[9];
    uint8_t hardwareVersionMajor;
    uint8_t hardwareVersionMinor;
    char sensorType[9];
    uint16_t sensorWidth;
    uint16_t sensorHeight;
    uint16_t templateSize;
    uint16_t templateTotal;
    bool valid;
};

struct FpResult
{
    FpStatus status;
    uint8_t rawConfirmation;
    uint16_t location;
    uint16_t score;
    uint16_t length;

    bool ok() const
    {
        return status == FpStatus::Ok;
    }
};

typedef std::function<void(const FpResult &result)> FpCallback;
// enroll progress: 1-6 = waiting for capture i, 7 = create model, 8 = store
typedef std::function<void(uint8_t progress)> FpProgressCallback;
// AutoEnroll/AutoIdentify intermediate acknowledge: step code + parameter 2
typedef std::function<void(uint8_t step, uint16_t parameter)> FpStepCallback;
typedef std::function<void(bool ready)> FpReadyCallback;
// data phase streaming: sink consumes received chunks, source fills the next chunk
typedef std::function<void(const uint8_t *data, uint16_t length)> FpDataSink;
typedef std::function<uint16_t(uint8_t *data, uint16_t maxLength)> FpDataSource;

inline const char *fpStatusText(FpStatus status)
{
    switch (status)
    {
        case FpStatus::Ok:
            return "ok";
        case FpStatus::PacketReceiveError:
            return "sensor packet receive error";
        case FpStatus::NoFinger:
            return "no finger";
        case FpStatus::ImageFail:
            return "image capture failed";
        case FpStatus::ImageMessy:
            return "image too messy";
        case FpStatus::FeatureFail:
            return "too few features";
        case FpStatus::NoMatch:
            return "no match";
        case FpStatus::NotFound:
            return "not found";
        case FpStatus::EnrollMismatch:
            return "features do not belong to one finger";
        case FpStatus::BadLocation:
            return "location beyond library";
        case FpStatus::DbRangeFail:
            return "template read error or invalid";
        case FpStatus::UploadFeatureFail:
            return "template upload error";
        case FpStatus::PacketResponseFail:
            return "sensor cannot receive data packets";
        case FpStatus::UploadFail:
            return "image upload error";
        case FpStatus::DeleteFail:
            return "delete failed";
        case FpStatus::DbClearFail:
            return "clear library failed";
        case FpStatus::PasswordFail:
            return "wrong password";
        case FpStatus::InvalidImage:
            return "no valid primary image";
        case FpStatus::FlashError:
            return "flash write error";
        case FpStatus::NoDefinitionError:
            return "undefined error";
        case FpStatus::InvalidRegister:
            return "invalid register number";
        case FpStatus::InvalidRegisterConfig:
            return "invalid register configuration";
        case FpStatus::InvalidNotepadPage:
            return "invalid notepad page";
        case FpStatus::CommPortFail:
            return "communication port error";
        case FpStatus::LibraryFull:
            return "library full";
        case FpStatus::AddressCodeError:
            return "wrong address code";
        case FpStatus::PasswordRequired:
            return "password must be verified";
        case FpStatus::TemplateEmpty:
            return "template empty";
        case FpStatus::LibraryEmpty:
            return "library empty";
        case FpStatus::SensorTimeout:
            return "sensor timeout";
        case FpStatus::FingerExists:
            return "fingerprint already exists";
        case FpStatus::SensorHardwareError:
            return "sensor hardware error";
        case FpStatus::ErrTimeoutAck:
            return "no acknowledge (timeout)";
        case FpStatus::ErrTimeoutData:
            return "data phase timeout";
        case FpStatus::ErrInterByte:
            return "inter byte timeout";
        case FpStatus::ErrChecksum:
            return "checksum mismatch";
        case FpStatus::ErrFraming:
            return "framing error";
        case FpStatus::ErrAddress:
            return "wrong module address";
        case FpStatus::ErrNotReady:
            return "driver not ready";
        case FpStatus::ErrBusy:
            return "driver busy";
        case FpStatus::ErrPoweredOff:
            return "scanner powered off";
        case FpStatus::ErrCancelled:
            return "cancelled";
        case FpStatus::ErrUnsupportedModel:
            return "not supported by this model";
        case FpStatus::ErrBufferTooSmall:
            return "buffer too small";
        case FpStatus::ErrInvalidParam:
            return "invalid parameter";
        case FpStatus::ErrBringUpFailed:
            return "bring-up failed";
        case FpStatus::UnsupportedCommand:
            return "unsupported command";
        case FpStatus::HardwareError:
            return "hardware error";
        case FpStatus::ExecutionFailure:
            return "command execution failure";
        default:
            return "unknown";
    }
}

inline const char *fpCommandText(FpCommand command)
{
    switch (command)
    {
        case FpCommand::GenImg:
            return "GenImg";
        case FpCommand::GenChar:
            return "GenChar";
        case FpCommand::Match:
            return "Match";
        case FpCommand::Search:
            return "Search";
        case FpCommand::RegModel:
            return "RegModel";
        case FpCommand::Store:
            return "Store";
        case FpCommand::LoadChar:
            return "LoadChar";
        case FpCommand::UpChar:
            return "UpChar";
        case FpCommand::DownChar:
            return "DownChar";
        case FpCommand::UpImage:
            return "UpImage";
        case FpCommand::DownImage:
            return "DownImage";
        case FpCommand::DeletChar:
            return "DeletChar";
        case FpCommand::Empty:
            return "Empty";
        case FpCommand::SetSysPara:
            return "SetSysPara";
        case FpCommand::ReadSysPara:
            return "ReadSysPara";
        case FpCommand::SetPwd:
            return "SetPwd";
        case FpCommand::VfyPwd:
            return "VfyPwd";
        case FpCommand::GetRandomCode:
            return "GetRandomCode";
        case FpCommand::SetAdder:
            return "SetAdder";
        case FpCommand::ReadInfPage:
            return "ReadInfPage";
        case FpCommand::WriteNotepad:
            return "WriteNotepad";
        case FpCommand::ReadNotepad:
            return "ReadNotepad";
        case FpCommand::TempleteNum:
            return "TempleteNum";
        case FpCommand::ReadIndexTable:
            return "ReadIndexTable";
        case FpCommand::GetImageEx:
            return "GetImageEx";
        case FpCommand::Cancel:
            return "Cancel";
        case FpCommand::AutoEnroll:
            return "AutoEnroll";
        case FpCommand::AutoIdentify:
            return "AutoIdentify";
        case FpCommand::AuraLedConfig:
            return "AuraLedConfig";
        case FpCommand::CheckSensor:
            return "CheckSensor";
        case FpCommand::GetAlgVer:
            return "GetAlgVer";
        case FpCommand::GetFwVer:
            return "GetFwVer";
        case FpCommand::ReadProdInfo:
            return "ReadProdInfo";
        case FpCommand::SoftRst:
            return "SoftRst";
        case FpCommand::HandShake:
            return "HandShake";
        default:
            return "?";
    }
}

inline const char *fpModelText(FpModel model)
{
    return model == FpModel::R503Pro ? "R503Pro" : "R503/R503S";
}
