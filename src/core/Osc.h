// Open Sound Control 1.0 messages and bundles: encoding and decoding, no networking.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rme {

using Bytes = std::vector<uint8_t>;

struct OscArg {
    enum class Type { Int, Float, String, True, False, Nil, Blob, Other };
    Type type = Type::Nil;
    int32_t i = 0;
    float f = 0.0f;
    std::string s;  // String (also Blob bytes)

    static OscArg makeFloat(float v);
    static OscArg makeInt(int32_t v);
    static OscArg makeString(std::string v);

    bool isNumber() const { return type == Type::Int || type == Type::Float || type == Type::True || type == Type::False; }
    float asFloat() const;
};

struct OscMessage {
    std::string address;
    std::vector<OscArg> args;

    OscMessage() = default;
    explicit OscMessage(std::string addr) : address(std::move(addr)) {}
    static OscMessage withFloat(std::string addr, float v);

    // First numeric argument (or 'fallback'), and first string argument (or empty).
    bool hasNumber() const;
    float number(float fallback = 0.0f) const;
    bool hasString() const;
    std::string text() const;
};

Bytes encode(const OscMessage& m);

// Decodes one UDP packet: a message or a (possibly nested) bundle. Messages are appended to
// 'out'. Returns false if the packet is malformed; messages decoded before the error are kept.
bool decodePacket(const uint8_t* data, size_t size, std::vector<OscMessage>& out);
inline bool decodePacket(const Bytes& b, std::vector<OscMessage>& out) { return decodePacket(b.data(), b.size(), out); }

// "/1/volume1 0.731" style text for the log.
std::string toString(const OscMessage& m);

}  // namespace rme
