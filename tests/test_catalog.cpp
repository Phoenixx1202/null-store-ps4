#include "../src/ui/StoreState.h"
#include "test_source_setup.h"
using namespace ns;
#include <cassert>
#include <iostream>
#include <sys/stat.h>
int main(){
    replaceSources({});
    registerTestSource("one", "https://one.example.org");
    registerTestSource("two", "https://two.example.org");
    std::vector<Game> games;
    for (int i = 1; i <= 2 * StoreState::kGridPage + 4; ++i) {
        Game game;
        game.title = "Game " + std::to_string(i / 2);
        game.url = sourceOrigin(1 + i % 2) + "/" + std::to_string(i);
        game.id = stableGameId(game.url);
        games.push_back(game);
    }
    {Game g;g.title="[PS4] Dying Light: Definitive Edition (CUSA02010) [1.54]";auto t=describe(g);assert(t.title=="Dying Light: Definitive Edition"&&t.titleId=="CUSA02010"&&t.version=="1.54");g.title="Assassin\xE2\x80\x99s Creed Mirage [1.11] [Repack] (\xD1\x80\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB0\xD1\x8F)";t=describe(g);assert(t.title=="Assassin\xE2\x80\x99s Creed Mirage"&&t.version=="1.11");g.title="Bubsy 4D";assert(describe(g).title=="Bubsy 4D");assert(cleanName("Romancing SaGa 2: Revenge of the Seven - PS4&PS5")=="Romancing SaGa 2: Revenge of the Seven");g.title="Red Dead Redemption (\xD0\x9F\xD0\x9A) ( )";assert(describe(g).title=="Red Dead Redemption");assert(withoutEmptyBrackets("A ( ) B []")=="A B");assert(cleanName("Assassin's Creed\xC2\xAE Mirage")=="Assassin's Creed Mirage");}
    mkdir("build",0777);mkdir("build/test-data",0777); // settings and favorites are written here
    remove("build/test-data/settings.txt");remove("build/test-data/settings.txt.tmp");remove("build/test-data/debrid-keys.txt");
    assert(writeFile("build/test-data/catalog.tsv", serialize(games)));
    StoreState ui("assets","build/test-data");auto all=[&](StoreState&u){size_t n=0;assert(u.visible.size()==u.groups.size());for(size_t g=0;g<u.groups.size();++g){assert(!u.groups[g].empty()&&u.visible[g]==u.groups[g][0]);n+=u.groups[g].size();}return n;};assert(all(ui)==games.size()&&ui.groups.size()<games.size());
    // Entries of one game share a tile, newest version first; the game page chooses one of them.
    assert(StoreState::newerVersion("1.10","1.9")&&!StoreState::newerVersion("1.9","1.10")&&StoreState::newerVersion("1.0","")&&!StoreState::newerVersion("","1.0"));
    {StoreState g("assets","build/test-data");g.games.clear();Game x;x.id=1;x.url="https://one.example.org/a";x.title="Dying Light (CUSA02010) [1.20]";g.games.push_back(x);x.id=2;x.title="Dying Light [1.54]";g.games.push_back(x);x.id=3;x.title="Bubsy 4D";g.games.push_back(x);assert(g.shareTitleIds());g.selected=0;g.filter();assert(g.groups.size()==2&&g.groups[0].size()==2&&g.games[g.visible[0]].id==2);g.action(CONFIRM);assert(g.detail&&g.variantSelected==0);g.action(DOWN);g.action(DOWN);assert(g.variantSelected==1&&g.variant()->id==1);g.action(CONFIRM);assert(g.downloadGame.id==1);g.sourcePicker=false;g.action(BACK);g.favorites.clear();g.action(FAVORITE);assert(g.favorites.count(2));g.favorites.clear();g.favorites.insert(1);g.action(FAVORITE);assert(g.favorites.empty());}
    {
        StoreState ambiguous("assets","build/test-data");
        ambiguous.games = {
            {100, "Remake (CUSA12345)", "2010", "", "", ""},
            {101, "Remake (CUSA67890)", "2025", "", "", ""},
            {102, "Remake", "", "", "", ""}
        };
        ambiguous.shareTitleIds();
        ambiguous.filter();
        assert(ambiguous.groups.size()==3 && !ambiguous.displayTitleIds.count(102));
    }
    // OPTIONS opens Settings; a source chosen there filters the catalogue and is kept.
    ui.action(SETTINGS);assert(ui.settings);ui.action(CONFIRM);assert(ui.sourceFilterPicker);ui.sourceChoice=2;ui.action(CONFIRM);assert(ui.sourceFilter==2&&!ui.sourceFilterPicker&&ui.settings&&!ui.visible.empty());for(auto&group:ui.groups)for(int n:group)assert(sourceForURL(ui.games[n].url)==2);
    {StoreState again("assets","build/test-data");assert(again.sourceFilter==2);}
    ui.action(CONFIRM);ui.sourceChoice=0;ui.action(CONFIRM);assert(all(ui)==games.size());
    ui.action(DOWN);ui.action(CONFIRM);assert(ui.sortPicker);ui.action(DOWN);ui.action(CONFIRM);assert(ui.sort==1&&!ui.sortPicker);ui.action(CONFIRM);ui.action(UP);ui.action(CONFIRM);assert(ui.sort==0);
    ui.settingsSelected=SETTING_LOCATION;ui.action(CONFIRM);assert(ui.downloadLocationPicker);ui.action(BACK);assert(!ui.downloadLocationPicker&&ui.settings);
    // Debrid: a service, then its key (asked for at once), both kept between runs.
    ui.settingsSelected=SETTING_DEBRID_KEY;ui.action(CONFIRM);assert(ui.debridPicker&&ui.debridChoice==1);ui.action(BACK);ui.settingsSelected=SETTING_DEBRID;ui.action(CONFIRM);assert(ui.debridPicker);ui.action(DOWN);ui.action(CONFIRM);assert(ui.debrid==1&&!ui.debridPicker&&ui.debridKeyRequest&&ui.settingsSelected==SETTING_DEBRID_KEY);ui.debridKeyRequest=false;
    ui.setDebridKey("  ABC123xyz \n");assert(ui.debridKey()=="ABC123xyz");{StoreState again("assets","build/test-data");assert(again.debrid==1&&again.debridKey()=="ABC123xyz");}
    ui.setDebridKey("");assert(ui.debridKey().empty());ui.settingsSelected=SETTING_DEBRID;ui.action(CONFIRM);ui.action(UP);ui.action(CONFIRM);assert(ui.debrid==0&&!ui.debridKeyRequest);
    ui.settingsSelected=SETTING_ARTWORK;ui.action(CONFIRM);assert(!ui.officialArt&&ui.artworkChanged);ui.artworkChanged=false;ui.action(CONFIRM);assert(ui.officialArt);
    ui.settingsSelected=SETTING_REFRESH;ui.action(CONFIRM);assert(ui.refreshRequest);ui.refreshRequest=false;ui.action(BACK);assert(!ui.settings);
    // Catalogue grid, search and the game page.
    ui.action(PAGE_NEXT);assert(ui.selected==StoreState::kGridPage);ui.action(PAGE_PREV);assert(ui.selected==0);ui.action(SEARCH);assert(ui.keyboard);ui.key=0;ui.action(CONFIRM);assert(ui.searchDraft=="A"&&ui.query.empty());ui.key=39;ui.action(CONFIRM);assert(!ui.keyboard);ui.action(BACK);assert(ui.query.empty());
    ui.action(CONFIRM);assert(ui.detail);ui.action(SETTINGS);assert(!ui.settings);ui.action(CONFIRM);assert(ui.sourcePicker&&ui.downloadRequest&&ui.downloadGame.id==ui.current()->id);ui.action(BACK);assert(!ui.sourcePicker&&ui.detail);ui.action(BACK);assert(!ui.detail);
    // Download queue: Settings stays closed, only finished downloads install, Square cancels and deletes.
    ui.action(TAB_PREV);assert(ui.tab==TAB_DOWNLOADS);ui.action(SETTINGS);assert(!ui.settings);ui.action(SEARCH);assert(!ui.installPicker);
    ui.action(MANAGE);for(int i=0;i<4;i++)ui.action(DOWN);ui.action(CONFIRM);
    assert(ui.confirmWorkerRemoval && !ui.uninstallWorker && ui.menuChoice==0);
    ui.action(CONFIRM);assert(ui.workerMenu && !ui.uninstallWorker);
    ui.action(CONFIRM);ui.action(DOWN);ui.action(CONFIRM);
    assert(ui.uninstallWorker && !ui.confirmWorkerRemoval);ui.uninstallWorker=false;
    DownloadRow running;running.id=2;running.state=DownloadState::Active;ui.jobs.push_back(running);ui.action(SEARCH);assert(!ui.installPicker);ui.jobs.clear();
    DownloadRow completed;completed.id=1;completed.state=DownloadState::Complete;ui.jobs.push_back(completed);ui.action(SEARCH);assert(ui.installPicker&&ui.installScanRequest&&ui.installJob==1);InstallPackage package;package.path="/generated/base.pkg";ui.packages.push_back(package);ui.installLoading=false;ui.action(CONFIRM);assert(ui.installSubmitRequest);ui.action(BACK);assert(!ui.installPicker);
    ui.action(FAVORITE);ui.menuChoice=1;ui.action(CONFIRM);assert(ui.cancelRequest&&ui.cancelJob==1&&ui.jobs.empty()&&ui.deletingJobs.count(1));ui.cancelRequest=false;
    ui.jobs.push_back(completed);ui.hideDeletedJobs();assert(ui.jobs.empty());
    DownloadRow failed=completed;failed.state=DownloadState::Failed;failed.error="Could not delete downloaded files: busy";ui.jobs.push_back(failed);ui.hideDeletedJobs();assert(ui.jobs.size()==1);
    ui.jobs.clear();ui.hideDeletedJobs();assert(ui.deletingJobs.empty());
    ui.action(BACK);assert(ui.tab==TAB_ALL);ui.action(TAB_NEXT);assert(ui.tab==TAB_FAVORITES&&ui.visible.empty());ui.action(TAB_PREV);assert(ui.tab==TAB_ALL);ui.action(SORT);assert(ui.sort==1);for(int i=0;i<3;++i)ui.action(SORT);assert(ui.sort==0);
    remove("build/test-data/settings.txt");remove("build/test-data/settings.txt.tmp");remove("build/test-data/debrid-keys.txt");
    std::cout<<"Catalog parser, round trip, source restriction, settings, search, navigation, sorting and the download queue passed ("<<games.size()<<" games).\n";
}
