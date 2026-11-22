#pragma once

#include <cctype>
#include <cstring>
#include <string>

#include "catalog.h"

// -----------------------------------------------------------------------------
// What a catalogue entry says about its game, pulled apart for display.
//
// Some sources write everything into the title:
//     "[PS4] Dying Light: Definitive Edition (CUSA02010) [1.54]"
//     "Assassin’s Creed Mirage [1.11] [Repack] (русская озвучка)"
// The screens show the name on its own and the title ID and version as facts
// beside it; the official name replaces it once it is known (StoreView).
// Searching still matches the whole original title.
// -----------------------------------------------------------------------------
namespace ns {

struct GameText {
    std::string title;   // "Dying Light: Definitive Edition"
    std::string titleId; // "CUSA02010", empty when the entry does not say
    std::string version; // "1.54", empty when the entry does not say
};

// True when `text` holds a title ID (four capitals, five digits) at `at`.
inline bool titleIdAt(const std::string& text, size_t at)
{
    if (at + 9 > text.size()) {
        return false;
    }
    for (size_t i = 0; i < 9; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[at + i]);
        if (i < 4 ? !(c >= 'A' && c <= 'Z') : !isdigit(c)) {
            return false;
        }
    }
    return true;
}

// The first PlayStation 4 title ID in a page of text ("CUSA12345"), or "".
inline std::string findPs4TitleId(const std::string& text)
{
    static const char* const kPrefixes[] = { "CUSA", "CUSH", "PCAS", "PLAS", "PCJS", "PLJS", "PLJM", "PCKS" };
    size_t best = std::string::npos;
    for (const char* prefix : kPrefixes) {
        for (size_t at = text.find(prefix); at != std::string::npos; at = text.find(prefix, at + 1)) {
            const bool wordStart = at == 0 || !isalnum(static_cast<unsigned char>(text[at - 1]));
            const bool wordEnd = at + 9 >= text.size() || !isdigit(static_cast<unsigned char>(text[at + 9]));
            if (wordStart && wordEnd && titleIdAt(text, at)) {
                best = std::min(best, at);
                break;
            }
        }
    }
    return best == std::string::npos ? std::string() : text.substr(best, 9);
}

// The name reduced to its letters and digits, to recognise the same game in
// the listings of two sources ("Bubsy 4D" and "BUBSY 4D!").
inline std::string titleKey(const std::string& title)
{
    std::string key;
    for (char raw : title) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c >= 128 || isalnum(c)) {
            key += static_cast<char>(c < 128 ? tolower(c) : c);
        }
    }
    return key;
}

// True when the text holds letters of a script other than Latin: Cyrillic,
// Greek, CJK... (the font of the store has none of them). Typographic
// punctuation such as ’ – … is not counted.
inline bool hasNonLatin(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c >= 0xCD && c <= 0xDF) || (c >= 0xE3 && c <= 0xEF) || c >= 0xF0) {
            return true;
        }
    }
    return false;
}

// Removes the letters of other scripts (whole UTF-8 sequences).
inline std::string latinOnly(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        const std::string sequence = text.substr(i, length);
        if (!hasNonLatin(sequence)) {
            out += sequence;
        }
        i += length;
    }
    return out;
}

inline bool isVersionText(const std::string& text)
{
    bool version = !text.empty() && isdigit(static_cast<unsigned char>(text[0]));
    for (char c : text) {
        version = version && (isdigit(static_cast<unsigned char>(c)) || c == '.');
    }
    return version;
}

// Empty brackets left behind once their contents were removed: "( )", "[]".
inline std::string withoutEmptyBrackets(std::string text)
{
    for (size_t open = text.find_first_of("(["); open != std::string::npos;
         open = text.find_first_of("([", open + 1)) {
        const char close = text[open] == '(' ? ')' : ']';
        size_t at = open + 1;
        while (at < text.size() && text[at] == ' ') {
            ++at;
        }
        if (at < text.size() && text[at] == close) {
            text.erase(open, at - open + 1);
            open = open == 0 ? 0 : open - 1;
        }
    }
    for (size_t at = text.find("  "); at != std::string::npos; at = text.find("  ", at)) {
        text.erase(at, 1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

// An official name as the screens show it: without trade marks, and without
// the platforms the store adds ("Romancing SaGa 2: Revenge of the Seven - PS4&PS5").
inline std::string cleanName(std::string name)
{
    for (const char* mark : { "\xC2\xAE", "\xE2\x84\xA2", "\xC2\xA9" }) { // ® ™ ©
        for (size_t at = name.find(mark); at != std::string::npos; at = name.find(mark, at)) {
            name.erase(at, strlen(mark));
        }
    }
    for (const char* tail : { " - PS4&PS5", " - PS4 & PS5", " PS4&PS5", " PS4 & PS5", " (PS4 & PS5)", " (PS4&PS5)",
                              " - PS4", " (PS4)", " PS4" }) {
        const size_t length = strlen(tail);
        if (name.size() > length + 2 && name.compare(name.size() - length, length, tail) == 0) {
            name.erase(name.size() - length);
            break;
        }
    }
    for (size_t at = name.find("  "); at != std::string::npos; at = name.find("  ", at)) {
        name.erase(at, 1);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '\n' || name.back() == '\r')) {
        name.pop_back();
    }
    while (!name.empty() && name[0] == ' ') {
        name.erase(0, 1);
    }
    return name;
}

inline GameText describe(const Game& game)
{
    GameText out;
    std::string title = game.title;

    // Everything in square brackets is a tag: "[PS4]", "[Repack]", "[FW 9.00]";
    // a version such as "[1.54]" is kept as the version.
    for (size_t open = title.find('['); open != std::string::npos; open = title.find('[', open)) {
        const size_t close = title.find(']', open);
        if (close == std::string::npos) {
            break;
        }
        const std::string inside = title.substr(open + 1, close - open - 1);
        if (out.version.empty() && isVersionText(inside)) {
            out.version = inside;
        }
        title.erase(open, close - open + 1);
    }

    // In parentheses: the title ID ("(CUSA02010)", sometimes with more IDs),
    // and notes in other languages ("(русская озвучка)") go; other remarks stay.
    for (size_t open = title.find('('); open != std::string::npos;) {
        const size_t close = title.find(')', open);
        if (close == std::string::npos) {
            break;
        }
        const std::string inside = title.substr(open + 1, close - open - 1);
        const std::string id = findPs4TitleId(inside);
        if (!id.empty() || titleIdAt(inside, 0) || hasNonLatin(inside)) {
            if (out.titleId.empty() && !id.empty()) {
                out.titleId = id;
            }
            title.erase(open, close - open + 1);
        } else {
            open = title.find('(', close);
        }
    }

    // Whatever else the font cannot show, and the brackets it leaves empty.
    title = withoutEmptyBrackets(latinOnly(title));

    // Removing parts can leave double spaces and loose separators behind.
    for (size_t at = title.find("  "); at != std::string::npos; at = title.find("  ", at)) {
        title.erase(at, 1);
    }
    const auto loose = [](char c) { return c == ' ' || c == '-' || c == ':' || c == ',' || c == '/' || c == '|'; };
    while (!title.empty() && loose(title.back())) {
        title.pop_back();
    }
    while (!title.empty() && loose(title[0])) {
        title.erase(0, 1);
    }
    out.title = title.empty() ? game.title : title;
    return out;
}

} // namespace ns
