#include "Osc.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace rme {

OscArg OscArg::makeFloat(float v) {
    OscArg a;
    a.type = Type::Float;
    a.f = v;
    return a;
}

OscArg OscArg::makeInt(int32_t v) {
    OscArg a;
    a.type = Type::Int;
    a.i = v;
    return a;
}

OscArg OscArg::makeString(std::string v) {
    OscArg a;
    a.type = Type::String;
    a.s = std::move(v);
    return a;
}

float OscArg::asFloat() const {
    switch (type) {
        case Type::Float: return f;
        case Type::Int: return static_cast<float>(i);
        case Type::True: return 1.0f;
        default: return 0.0f;
    }
}

OscMessage OscMessage::withFloat(std::string addr, float v) {
    OscMessage m(std::move(addr));
    m.args.push_back(OscArg::makeFloat(v));
    return m;
}

bool OscMessage::hasNumber() const {
    for (const auto& a : args)
        if (a.isNumber()) return true;
    return false;
}

float OscMessage::number(float fallback) const {
    for (const auto& a : args)
        if (a.isNumber()) return a.asFloat();
    return fallback;
}

bool OscMessage::hasString() const {
    for (const auto& a : args)
        if (a.type == OscArg::Type::String) return true;
    return false;
}

std::string OscMessage::text() const {
    for (const auto& a : args)
        if (a.type == OscArg::Type::String) return a.s;
    return {};
}

// ---- Encoding --------------------------------------------------------------------------
namespace {

void putString(Bytes& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
    while (b.size() % 4) b.push_back(0);
}

void putU32(Bytes& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

}  // namespace

Bytes encode(const OscMessage& m) {
    Bytes b;
    putString(b, m.address);
    std::string tags = ",";
    for (const auto& a : m.args) {
        switch (a.type) {
            case OscArg::Type::Int: tags += 'i'; break;
            case OscArg::Type::Float: tags += 'f'; break;
            case OscArg::Type::String: tags += 's'; break;
            case OscArg::Type::True: tags += 'T'; break;
            case OscArg::Type::False: tags += 'F'; break;
            case OscArg::Type::Blob: tags += 'b'; break;
            default: tags += 'N'; break;
        }
    }
    putString(b, tags);
    for (const auto& a : m.args) {
        switch (a.type) {
            case OscArg::Type::Int: putU32(b, static_cast<uint32_t>(a.i)); break;
            case OscArg::Type::Float: {
                uint32_t u;
                std::memcpy(&u, &a.f, 4);
                putU32(b, u);
                break;
            }
            case OscArg::Type::String: putString(b, a.s); break;
            case OscArg::Type::Blob:
                putU32(b, static_cast<uint32_t>(a.s.size()));
                b.insert(b.end(), a.s.begin(), a.s.end());
                while (b.size() % 4) b.push_back(0);
                break;
            default: break;
        }
    }
    return b;
}

// ---- Decoding --------------------------------------------------------------------------
namespace {

struct Reader {
    const uint8_t* p;
    size_t size;
    size_t pos = 0;

    bool u32(uint32_t& v) {
        if (size - pos < 4) return false;
        v = (uint32_t(p[pos]) << 24) | (uint32_t(p[pos + 1]) << 16) | (uint32_t(p[pos + 2]) << 8) | uint32_t(p[pos + 3]);
        pos += 4;
        return true;
    }
    bool u64(uint64_t& v) {
        uint32_t hi, lo;
        if (!u32(hi) || !u32(lo)) return false;
        v = (uint64_t(hi) << 32) | lo;
        return true;
    }
    bool str(std::string& s) {
        size_t end = pos;
        while (end < size && p[end] != 0) ++end;
        if (end >= size) return false;  // no terminator
        s.assign(reinterpret_cast<const char*>(p + pos), end - pos);
        pos = (end + 4) & ~size_t(3);  // skip terminator and padding
        if (pos > size) return false;
        return true;
    }
};

bool decodeMessage(const uint8_t* data, size_t size, std::vector<OscMessage>& out) {
    Reader r{data, size};
    OscMessage m;
    if (!r.str(m.address) || m.address.empty() || m.address[0] != '/') return false;
    if (r.pos >= size) {  // no type tag string: message without arguments
        out.push_back(std::move(m));
        return true;
    }
    std::string tags;
    if (!r.str(tags) || tags.empty() || tags[0] != ',') return false;
    for (size_t t = 1; t < tags.size(); ++t) {
        OscArg a;
        uint32_t u = 0;
        uint64_t u8 = 0;
        switch (tags[t]) {
            case 'i':
                if (!r.u32(u)) return false;
                a.type = OscArg::Type::Int;
                a.i = static_cast<int32_t>(u);
                break;
            case 'f':
                if (!r.u32(u)) return false;
                a.type = OscArg::Type::Float;
                std::memcpy(&a.f, &u, 4);
                break;
            case 's':
            case 'S':
                if (!r.str(a.s)) return false;
                a.type = OscArg::Type::String;
                break;
            case 'b': {
                if (!r.u32(u) || size - r.pos < u) return false;
                a.type = OscArg::Type::Blob;
                a.s.assign(reinterpret_cast<const char*>(data + r.pos), u);
                r.pos += (u + 3) & ~uint32_t(3);
                if (r.pos > size) return false;
                break;
            }
            case 'h':
                if (!r.u64(u8)) return false;
                a.type = OscArg::Type::Int;
                a.i = static_cast<int32_t>(static_cast<int64_t>(u8));
                break;
            case 'd': {
                if (!r.u64(u8)) return false;
                double d;
                std::memcpy(&d, &u8, 8);
                a.type = OscArg::Type::Float;
                a.f = static_cast<float>(d);
                break;
            }
            case 't':
                if (!r.u64(u8)) return false;
                a.type = OscArg::Type::Other;
                break;
            case 'c':
            case 'r':
            case 'm':
                if (!r.u32(u)) return false;
                a.type = OscArg::Type::Other;
                break;
            case 'T': a.type = OscArg::Type::True; break;
            case 'F': a.type = OscArg::Type::False; break;
            case 'N':
            case 'I': a.type = OscArg::Type::Nil; break;
            case '[':
            case ']': continue;  // arrays: flatten
            default: return false;  // unknown type: the remaining data cannot be interpreted
        }
        m.args.push_back(std::move(a));
    }
    out.push_back(std::move(m));
    return true;
}

bool decodeAny(const uint8_t* data, size_t size, std::vector<OscMessage>& out, int depth) {
    if (size < 4 || depth > 8) return false;
    static const char kBundle[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};
    if (size >= 16 && std::memcmp(data, kBundle, 8) == 0) {
        Reader r{data, size, 16};  // skip "#bundle" and the time tag
        while (r.pos < size) {
            uint32_t len = 0;
            if (!r.u32(len) || len > size - r.pos || len % 4) return false;
            if (!decodeAny(data + r.pos, len, out, depth + 1)) return false;
            r.pos += len;
        }
        return true;
    }
    return decodeMessage(data, size, out);
}

}  // namespace

bool decodePacket(const uint8_t* data, size_t size, std::vector<OscMessage>& out) {
    if (!data) return false;
    return decodeAny(data, size, out, 0);
}

std::string toString(const OscMessage& m) {
    std::string s = m.address;
    for (const auto& a : m.args) {
        s += ' ';
        char buf[32];
        switch (a.type) {
            case OscArg::Type::Int: std::snprintf(buf, sizeof buf, "%d", a.i); s += buf; break;
            case OscArg::Type::Float: std::snprintf(buf, sizeof buf, "%.4g", static_cast<double>(a.f)); s += buf; break;
            case OscArg::Type::String: s += '"' + a.s + '"'; break;
            case OscArg::Type::True: s += "true"; break;
            case OscArg::Type::False: s += "false"; break;
            case OscArg::Type::Nil: s += "nil"; break;
            case OscArg::Type::Blob: s += "<blob " + std::to_string(a.s.size()) + " bytes>"; break;
            default: s += '?'; break;
        }
    }
    return s;
}

}  // namespace rme
