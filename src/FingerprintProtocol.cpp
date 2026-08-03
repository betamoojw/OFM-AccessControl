#include "FingerprintProtocol.h"

#include <cstring>

void FingerprintLink::reset()
{
    _state = State::Hunt1;
    _pid = 0;
    _wireLength = 0;
    _payloadLength = 0;
    _payloadIndex = 0;
    _checksumCalc = 0;
    _checksumRecv = 0;
}

void FingerprintLink::setAddress(uint32_t address)
{
    _address = address;
}

uint8_t FingerprintLink::addressByte(uint8_t index) const
{
    return (uint8_t)(_address >> (24 - 8 * index));
}

uint16_t FingerprintLink::buildFrame(uint8_t pid, const uint8_t *payload, uint16_t payloadLength, uint8_t *dst, uint16_t dstSize) const
{
    if (payloadLength > FpProto::MaxPayload)
        return 0;

    uint16_t total = FpProto::HeaderSize + payloadLength + 2;
    if (dst == nullptr || dstSize < total)
        return 0;

    uint16_t wireLength = payloadLength + 2;

    dst[0] = FpProto::HeaderHi;
    dst[1] = FpProto::HeaderLo;
    dst[2] = addressByte(0);
    dst[3] = addressByte(1);
    dst[4] = addressByte(2);
    dst[5] = addressByte(3);
    dst[6] = pid;
    dst[7] = (uint8_t)(wireLength >> 8);
    dst[8] = (uint8_t)(wireLength & 0xFF);

    uint16_t sum = pid + dst[7] + dst[8];
    for (uint16_t i = 0; i < payloadLength; i++)
    {
        dst[FpProto::HeaderSize + i] = payload[i];
        sum += payload[i];
    }

    dst[FpProto::HeaderSize + payloadLength] = (uint8_t)(sum >> 8);
    dst[FpProto::HeaderSize + payloadLength + 1] = (uint8_t)(sum & 0xFF);
    return total;
}

uint16_t FingerprintLink::buildCommand(uint8_t command, const uint8_t *params, uint16_t paramLength, uint8_t *dst, uint16_t dstSize) const
{
    if (paramLength + 1 > FpProto::MaxPayload)
        return 0;

    uint8_t payload[FpProto::MaxPayload];
    payload[0] = command;
    if (paramLength > 0)
        memcpy(payload + 1, params, paramLength);

    return buildFrame(FpProto::PidCommand, payload, paramLength + 1, dst, dstSize);
}

FingerprintLink::Event FingerprintLink::feed(uint8_t byte, uint32_t nowMs)
{
    _lastByte = nowMs;

    switch (_state)
    {
        case State::Hunt1:
            if (byte == FpProto::HeaderHi)
                _state = State::Hunt2;
            else if (byte == FpProto::BootByte)
                return Event::BootByte;
            return Event::None;

        case State::Hunt2:
            if (byte == FpProto::HeaderLo)
                _state = State::Addr0;
            else if (byte != FpProto::HeaderHi) // "EF EF 01" keeps hunting for the low header byte
                _state = State::Hunt1;
            return Event::None;

        case State::Addr0:
        case State::Addr1:
        case State::Addr2:
        case State::Addr3:
        {
            uint8_t index = (uint8_t)_state - (uint8_t)State::Addr0;
            if (byte != addressByte(index))
            {
                _state = State::Hunt1;
                return Event::ErrAddress;
            }
            _state = (State)((uint8_t)_state + 1);
            return Event::None;
        }

        case State::Pid:
            if (byte != FpProto::PidCommand && byte != FpProto::PidData &&
                byte != FpProto::PidAck && byte != FpProto::PidDataEnd)
            {
                _state = State::Hunt1;
                return Event::ErrFraming;
            }
            _pid = byte;
            _checksumCalc = byte;
            _state = State::LenHi;
            return Event::None;

        case State::LenHi:
            _wireLength = (uint16_t)byte << 8;
            _checksumCalc += byte;
            _state = State::LenLo;
            return Event::None;

        case State::LenLo:
            _wireLength |= byte;
            _checksumCalc += byte;
            if (_wireLength < 2 || _wireLength > FpProto::MaxPayload + 2)
            {
                _state = State::Hunt1;
                return Event::ErrFraming;
            }
            _payloadLength = _wireLength - 2;
            _payloadIndex = 0;
            _state = _payloadLength > 0 ? State::Payload : State::CkHi;
            return Event::None;

        case State::Payload:
            _payload[_payloadIndex++] = byte;
            _checksumCalc += byte;
            if (_payloadIndex >= _payloadLength)
                _state = State::CkHi;
            return Event::None;

        case State::CkHi:
            _checksumRecv = (uint16_t)byte << 8;
            _state = State::CkLo;
            return Event::None;

        case State::CkLo:
        {
            _checksumRecv |= byte;
            _state = State::Hunt1;
            if (_checksumRecv != _checksumCalc)
                return Event::ErrChecksum;

            switch (_pid)
            {
                case FpProto::PidAck:
                    return Event::FrameAck;
                case FpProto::PidData:
                    return Event::FrameData;
                case FpProto::PidDataEnd:
                    return Event::FrameDataEnd;
                default:
                    return Event::ErrFraming;
            }
        }
    }

    _state = State::Hunt1;
    return Event::ErrFraming;
}

bool FingerprintLink::checkInterByteTimeout(uint32_t nowMs)
{
    if (_state == State::Hunt1)
        return false;

    if (nowMs - _lastByte < FpProto::InterByteTimeoutMs)
        return false;

    reset();
    return true;
}
