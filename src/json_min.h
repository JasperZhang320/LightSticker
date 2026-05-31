// Tiny header-only JSON library for LightSticker.
//
// Intentionally minimal: handles objects, arrays, strings, integers, booleans
// and null. Numbers are stored as 64-bit signed integers (we have no use for
// floats). Strings round-trip through UTF-8 with standard JSON escapes.
//
// This exists so we don't have to vendor nlohmann/json (~24k lines) into a
// project whose data schema fits in a few dozen scalars.
//
// Public API:
//   json_min::Value v = json_min::Value::make_obj();
//   v["x"] = json_min::Value::make_int(120);
//   std::string text = json_min::write(v);
//   json_min::Value parsed; bool ok = json_min::parse(text, parsed);

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace json_min {

struct Value;
using Object = std::vector<std::pair<std::string, Value>>;
using Array  = std::vector<Value>;

struct Value {
    enum class Type { Null, Bool, Int, Str, Arr, Obj };

    Type        type = Type::Null;
    bool        b = false;
    long long   i = 0;
    std::string s;
    Array       a;
    Object      o;

    static Value make_null() { return Value{}; }
    static Value make_obj()  { Value v; v.type = Type::Obj;  return v; }
    static Value make_arr()  { Value v; v.type = Type::Arr;  return v; }
    static Value make_int(long long n) { Value v; v.type = Type::Int;  v.i = n; return v; }
    static Value make_bool(bool x)     { Value v; v.type = Type::Bool; v.b = x; return v; }
    static Value make_str(std::string x) {
        Value v; v.type = Type::Str; v.s = std::move(x); return v;
    }

    // Object accessors. operator[] returns a slot; auto-creates if missing.
    Value& operator[](const std::string& k) {
        for (auto& kv : o) {
            if (kv.first == k) return kv.second;
        }
        o.push_back({k, Value{}});
        return o.back().second;
    }

    const Value* find(const std::string& k) const {
        for (const auto& kv : o) {
            if (kv.first == k) return &kv.second;
        }
        return nullptr;
    }

    long long as_int(long long fallback = 0) const {
        return type == Type::Int ? i : fallback;
    }
    bool as_bool(bool fallback = false) const {
        return type == Type::Bool ? b : fallback;
    }
    const std::string& as_str() const {
        static const std::string empty;
        return type == Type::Str ? s : empty;
    }
};

// ----- writer ----------------------------------------------------------------

namespace detail {

inline void write_string(std::string& out, const std::string& s) {
    out += '"';
    for (size_t k = 0; k < s.size(); ++k) {
        const unsigned char ch = static_cast<unsigned char>(s[k]);
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(ch));
                    out += buf;
                } else {
                    out += static_cast<char>(ch);
                }
                break;
        }
    }
    out += '"';
}

inline void write_value(std::string& out, const Value& v, bool pretty, int depth) {
    auto indent = [&](int d) {
        if (pretty) for (int k = 0; k < d; ++k) out += "  ";
    };
    auto newline = [&]() { if (pretty) out += '\n'; };

    switch (v.type) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.b ? "true" : "false"; break;
        case Value::Type::Int: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld", v.i);
            out += buf;
            break;
        }
        case Value::Type::Str: write_string(out, v.s); break;
        case Value::Type::Arr: {
            if (v.a.empty()) { out += "[]"; break; }
            out += '['; newline();
            for (size_t k = 0; k < v.a.size(); ++k) {
                indent(depth + 1);
                write_value(out, v.a[k], pretty, depth + 1);
                if (k + 1 < v.a.size()) out += ',';
                newline();
            }
            indent(depth); out += ']';
            break;
        }
        case Value::Type::Obj: {
            if (v.o.empty()) { out += "{}"; break; }
            out += '{'; newline();
            for (size_t k = 0; k < v.o.size(); ++k) {
                indent(depth + 1);
                write_string(out, v.o[k].first);
                out += pretty ? ": " : ":";
                write_value(out, v.o[k].second, pretty, depth + 1);
                if (k + 1 < v.o.size()) out += ',';
                newline();
            }
            indent(depth); out += '}';
            break;
        }
    }
}

}  // namespace detail

inline std::string write(const Value& v, bool pretty = true) {
    std::string out;
    out.reserve(64);
    detail::write_value(out, v, pretty, 0);
    return out;
}

// ----- parser ----------------------------------------------------------------

namespace detail {

struct Parser {
    const char* p;
    const char* end;

    void skip_ws() {
        while (p < end) {
            const char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++p;
            } else {
                break;
            }
        }
    }

    bool eof() { skip_ws(); return p >= end; }
    bool peek(char c) { skip_ws(); return p < end && *p == c; }
    bool consume(char c) { skip_ws(); if (p < end && *p == c) { ++p; return true; } return false; }

    bool consume_literal(const char* literal) {
        skip_ws();
        const size_t n = std::strlen(literal);
        if (static_cast<size_t>(end - p) < n) return false;
        if (std::memcmp(p, literal, n) != 0) return false;
        p += n;
        return true;
    }

    static void encode_utf8(unsigned cp, std::string& out) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parse_hex4(unsigned& out) {
        if (end - p < 4) return false;
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = p[k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        p += 4;
        out = v;
        return true;
    }

    bool parse_string(std::string& out) {
        if (!consume('"')) return false;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (p + 1 >= end) return false;
                const char esc = p[1];
                p += 2;
                switch (esc) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!parse_hex4(cp)) return false;
                        // Surrogate pair handling.
                        if (cp >= 0xD800 && cp <= 0xDBFF && p + 1 < end && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned low = 0;
                            if (!parse_hex4(low)) return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        encode_utf8(cp, out);
                        break;
                    }
                    default: return false;
                }
            } else {
                out += *p++;
            }
        }
        if (p >= end) return false;
        ++p;  // consume closing quote
        return true;
    }

    bool parse_number(Value& v) {
        skip_ws();
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end && *p >= '0' && *p <= '9') ++p;
        // Reject fractions / exponents (we never write them).
        if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) return false;
        if (p == start) return false;
        char buf[32];
        const size_t n = static_cast<size_t>(p - start);
        if (n >= sizeof(buf)) return false;
        std::memcpy(buf, start, n);
        buf[n] = 0;
        v.type = Value::Type::Int;
        v.i = std::strtoll(buf, nullptr, 10);
        return true;
    }

    bool parse_value(Value& v) {
        skip_ws();
        if (p >= end) return false;
        const char c = *p;
        if (c == '"') {
            v.type = Value::Type::Str;
            return parse_string(v.s);
        }
        if (c == '{') return parse_object(v);
        if (c == '[') return parse_array(v);
        if (c == 't' || c == 'f') {
            if (consume_literal("true"))  { v.type = Value::Type::Bool; v.b = true;  return true; }
            if (consume_literal("false")) { v.type = Value::Type::Bool; v.b = false; return true; }
            return false;
        }
        if (c == 'n') {
            if (consume_literal("null")) { v.type = Value::Type::Null; return true; }
            return false;
        }
        return parse_number(v);
    }

    bool parse_object(Value& v) {
        if (!consume('{')) return false;
        v.type = Value::Type::Obj;
        v.o.clear();
        if (consume('}')) return true;
        while (true) {
            std::string key;
            if (!parse_string(key)) return false;
            if (!consume(':')) return false;
            Value child;
            if (!parse_value(child)) return false;
            v.o.push_back({std::move(key), std::move(child)});
            if (consume(',')) continue;
            if (consume('}')) return true;
            return false;
        }
    }

    bool parse_array(Value& v) {
        if (!consume('[')) return false;
        v.type = Value::Type::Arr;
        v.a.clear();
        if (consume(']')) return true;
        while (true) {
            Value child;
            if (!parse_value(child)) return false;
            v.a.push_back(std::move(child));
            if (consume(',')) continue;
            if (consume(']')) return true;
            return false;
        }
    }
};

}  // namespace detail

inline bool parse(const std::string& src, Value& out) {
    detail::Parser p{ src.data(), src.data() + src.size() };
    if (!p.parse_value(out)) return false;
    p.skip_ws();
    return p.p == p.end;
}

}  // namespace json_min
