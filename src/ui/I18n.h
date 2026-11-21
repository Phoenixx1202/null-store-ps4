#pragma once

#include <string>

// -----------------------------------------------------------------------------
// The store's texts in the console's language. English is written in the
// code and is the default; src/ui/I18n.cpp holds a row per text with its
// Brazilian Portuguese, Spanish, Turkish, French and Italian versions.
//
// A text whose parts vary is written with {0}, {1}, {2}:
//     trf("{0} versions", "3")              -> "3 versões"
// trMessage() also recognises such texts once their parts are filled in, for
// the messages the download engine and the workers write in English:
//     trMessage("Downloading from TorBox (1/2): game.pkg")
// A text without a row is shown as it is.
// -----------------------------------------------------------------------------
namespace ns {

enum class Language { English, Portuguese, Spanish, Turkish, French, Italian, Count };

void setLanguage(Language language);
Language currentLanguage();
// The console's language setting (ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG).
Language languageFromConsole(int code);
// "en", "pt", "es", "tr", "fr", "it" (the PC preview).
Language languageFromName(const std::string& name);

const char* tr(const char* english);
std::string tr(const std::string& english);
std::string trf(const char* english, const std::string& a, const std::string& b = std::string(),
                const std::string& c = std::string());
std::string trMessage(const std::string& english);

} // namespace ns
