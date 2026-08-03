#pragma once

#include <cstdint>

// GROW R503 / R503Pro wire protocol: 0xEF01 + address(4) + PID(1) + length(2) + content + checksum(2),
// all big endian. Length counts the content plus the two checksum bytes.
namespace FpProto
{
    constexpr uint8_t HeaderHi = 0xEF;
    constexpr uint8_t HeaderLo = 0x01;

    constexpr uint8_t PidCommand = 0x01;
    constexpr uint8_t PidData = 0x02;
    constexpr uint8_t PidAck = 0x07;
    constexpr uint8_t PidDataEnd = 0x08;

    constexpr uint8_t BootByte = 0x55;
    constexpr uint32_t DefaultAddress = 0xFFFFFFFF;

    constexpr uint16_t MaxPayload = 256;
    constexpr uint16_t HeaderSize = 9;
    constexpr uint16_t MaxFrame = HeaderSize + MaxPayload + 2;

    constexpr uint32_t InterByteTimeoutMs = 50;
} // namespace FpProto

// Allocation free framing: builds command/data frames into a caller owned buffer and consumes
// received bytes one at a time. Time is injected so this unit stays free of Arduino dependencies.
class FingerprintLink
{
  public:
    enum class Event : uint8_t
    {
        None = 0,
        FrameAck,
        FrameData,
        FrameDataEnd,
        BootByte,
        ErrChecksum,
        ErrAddress,
        ErrFraming
    };

    void reset();
    void setAddress(uint32_t address);
    uint32_t address() const { return _address; }

    uint16_t buildFrame(uint8_t pid, const uint8_t *payload, uint16_t payloadLength, uint8_t *dst, uint16_t dstSize) const;
    uint16_t buildCommand(uint8_t command, const uint8_t *params, uint16_t paramLength, uint8_t *dst, uint16_t dstSize) const;

    Event feed(uint8_t byte, uint32_t nowMs);
    bool checkInterByteTimeout(uint32_t nowMs);

    uint8_t pid() const { return _pid; }
    const uint8_t *payload() const { return _payload; }
    uint16_t payloadLength() const { return _payloadLength; }
    bool hunting() const { return _state == State::Hunt1; }

  private:
    enum class State : uint8_t
    {
        Hunt1 = 0,
        Hunt2,
        Addr0,
        Addr1,
        Addr2,
        Addr3,
        Pid,
        LenHi,
        LenLo,
        Payload,
        CkHi,
        CkLo
    };

    State _state = State::Hunt1;
    uint32_t _address = FpProto::DefaultAddress;
    uint32_t _lastByte = 0;
    uint8_t _pid = 0;
    uint16_t _wireLength = 0;
    uint16_t _payloadLength = 0;
    uint16_t _payloadIndex = 0;
    uint16_t _checksumCalc = 0;
    uint16_t _checksumRecv = 0;
    uint8_t _payload[FpProto::MaxPayload];

    uint8_t addressByte(uint8_t index) const;
};
