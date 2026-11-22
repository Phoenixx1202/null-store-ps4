// The store's texts: exact rows, filled-in rows, and messages recognised
// after their parts were filled in by the engine.
#include "../src/ui/I18n.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace ns;

int main()
{
    assert(languageFromConsole(17) == Language::Portuguese && languageFromConsole(19) == Language::Turkish &&
           languageFromConsole(1) == Language::English && languageFromConsole(20) == Language::Spanish);

    setLanguage(Language::English);
    assert(std::string(tr("Settings")) == "Settings");
    assert(trf("{0} versions", "3") == "3 versions");
    assert(trMessage("Downloading from TorBox (1/2): game.pkg") == "Downloading from TorBox (1/2): game.pkg");

    setLanguage(Language::Portuguese);
    assert(std::string(tr("Settings")) == "Configurações");
    assert(std::string(tr("Unknown text")) == "Unknown text");
    assert(trf("{0} versions", "3") == "3 versões");
    assert(trf("SEARCH: \"{0}\"", "GOD") == "BUSCA: \"GOD\"");
    assert(trMessage("Added to Downloads.") == "Adicionado aos downloads.");
    assert(trMessage("Downloading from TorBox (1/2): game.pkg") == "Baixando do TorBox (1/2): game.pkg");
    assert(trMessage("TorBox is downloading the torrent: 45% (downloading)") ==
           "TorBox está baixando o torrent: 45% (downloading)");
    assert(trMessage("Connecting to peers: 3 connected, 40 known") == "Conectando a peers: 3 conectados, 40 conhecidos");
    assert(trMessage("3 updated / 0 cached / 0 unavailable") == "3 atualizados / 0 em cache / 0 indisponíveis");
    assert(trMessage("3 updated / 0 cached / 0 unavailable | Saved catalogues retained") ==
           "3 atualizados / 0 em cache / 0 indisponíveis | Catálogos salvos mantidos");
    assert(trMessage("Something new") == "Something new");

    setLanguage(Language::Turkish);
    assert(trMessage("TorBox refused the API key. Check it in Settings.") ==
           "TorBox API anahtarını reddetti. Ayarlar'dan kontrol et.");
    setLanguage(Language::French);
    assert(trMessage("Verifying saved pieces: 50%") == "Vérification des parties enregistrées : 50 %");

    std::cout << "Translations: rows, filled rows and engine messages passed.\n";
}
