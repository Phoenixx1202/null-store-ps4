#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// A small JSON reader for the answers of the Debrid services (src/debrid.cpp).
// parse() never throws; on malformed text it returns a null value. Strings
// are decoded to UTF-8; numbers are kept as double (and as text, for ids too
// large for a double to hold exactly).
// -----------------------------------------------------------------------------
namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string text; // a string, or a number as written
    std::vector<Value> items;
    std::map<std::string, Value> fields;

    bool isNull() const { return type == Null; }
    bool isObject() const { return type == Object; }
    bool isArray() const { return type == Array; }

    // A field of an object, or a null value.
    const Value& operator[](const std::string& key) const
    {
        static const Value none;
        if (type != Object) return none;
        const auto found = fields.find(key);
        return found == fields.end() ? none : found->second;
    }
    const Value& at(size_t index) const
    {
        static const Value none;
        return type == Array && index < items.size() ? items[index] : none;
    }
    size_t size() const { return type == Array ? items.size() : 0; }

    // The value as text: a string, or a number as written ("12345").
    std::string str() const { return type == String || type == Number ? text : type == Bool ? (boolean ? "true" : "false") : ""; }
    double num() const { return type == Number ? number : type == String ? atof(text.c_str()) : type == Bool ? (boolean ? 1 : 0) : 0; }
    uint64_t u64() const { return type == Number || type == String ? strtoull(text.c_str(), nullptr, 10) : 0; }
    bool truthy() const { return type == Bool ? boolean : type == Number ? number != 0 : type == String ? !text.empty() : type != Null; }
};

namespace detail {

inline bool isdigitChar(char c)
{
    return c >= '0' && c <= '9';
}

inline void space(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

inline void utf8(std::string& out, uint32_t c)
{
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

inline bool string(const std::string& s, size_t& i, std::string& out)
{
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') return true;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= s.size()) return false;
        const char e = s[i++];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
            if (i + 4 > s.size()) return false;
            uint32_t code = static_cast<uint32_t>(strtoul(s.substr(i, 4).c_str(), nullptr, 16));
            i += 4;
            if (code >= 0xD800 && code < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                const uint32_t low = static_cast<uint32_t>(strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
                if (low >= 0xDC00 && low < 0xE000) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    i += 6;
                }
            }
            utf8(out, code);
            break;
        }
        default: out += e; break; // \" \\ \/
        }
    }
    return false;
}

inline bool value(const std::string& s, size_t& i, Value& v, int depth)
{
    if (depth > 64) return false;
    space(s, i);
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '{') {
        v.type = Value::Object;
        ++i;
        space(s, i);
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            space(s, i);
            std::string key;
            if (!string(s, i, key)) return false;
            space(s, i);
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            Value item;
            if (!value(s, i, item, depth + 1)) return false;
            v.fields[key] = std::move(item);
            space(s, i);
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            return false;
        }
    }
    if (c == '[') {
        v.type = Value::Array;
        ++i;
        space(s, i);
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        for (;;) {
            Value item;
            if (!value(s, i, item, depth + 1)) return false;
            v.items.push_back(std::move(item));
            space(s, i);
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            return false;
        }
    }
    if (c == '"') {
        v.type = Value::String;
        return string(s, i, v.text);
    }
    if (s.compare(i, 4, "true") == 0) {
        v.type = Value::Bool;
        v.boolean = true;
        i += 4;
        return true;
    }
    if (s.compare(i, 5, "false") == 0) {
        v.type = Value::Bool;
        i += 5;
        return true;
    }
    if (s.compare(i, 4, "null") == 0) {
        i += 4;
        return true;
    }
    const size_t start = i;
    while (i < s.size() && (isdigitChar(s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
    if (i == start) return false;
    v.type = Value::Number;
    v.text = s.substr(start, i - start);
    v.number = atof(v.text.c_str());
    return true;
}

} // namespace detail

inline Value parse(const std::string& text)
{
    Value v;
    size_t i = 0;
    if (!detail::value(text, i, v, 0)) {
        return Value();
    }
    return v;
}

} // namespace json
