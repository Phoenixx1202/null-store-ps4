#define PORTAL_HOST
#include "../src/sources.h"
#include <cassert>
#include <iostream>

static void expectOne(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAILED: " << message << "\n"; assert(false); }
}

int main() {
    const auto& builtins = builtInSources();
    assert(sourceSnapshot()->items.size() == 3);
    assert(builtins.listingURL(2, 2) == "https://gametorrent.org/category/playstation-4/page/2/");
    assert(builtins.listingURL(3, 2) == "https://love-games1.net/load/igry_dlja_konsolej/igry_dlja_playstation_4/42-2");
    auto first = parseListingWith(builtins,
        R"(<a class="item grid-item" href="/746-game.html"><div class="item__meta"><span>1997</span><span>458 MB</span></div><div class="item__title">Game</div></a>)", 1);
    assert(first.size()==1 && first[0].id==746 && first[0].year=="1997" && first[0].size=="458 MB");
    auto second = parseListingWith(builtins,
        R"(<article class="shortstory-item"><a href="/category/playstation-4/137-game.html"><h2 class="title">Game</h2><div class="year">2026 •</div><img data-src="/cover.jpg"></a></article>)", 2);
    assert(second.size()==1 && second[0].id==1000000137 && second[0].year=="2026" && second[0].image=="https://gametorrent.org/cover.jpg");
    auto third = parseListingWith(builtins,
        R"(<div id="entryID93"><div class="titl"><a href="/load/igry_dlja_konsolej/igry_dlja_playstation_4/game/42-1-0-93">Game</a></div><p>Размер: 2 GB</p></div>)", 3);
    assert(third.size()==1 && third[0].id==1500000093 && third[0].size=="2 GB");
    assert(builtInLastPage("/42-5 /42-1-0-94 /42-25", SourceKind::LoveGames)==25);
    auto files = parseDownloadLinksWith(builtins,
        R"(<b class="dl__title">Base game</b><a class="dl__btn" href="/base.torrent">Get</a><a data-gt-gated="1" href="/other.torrent">Get</a>)", 1);
    assert(files.size()==1 && files.front().title=="Base game");

    SourceTable table;
    std::string error;
    expectOne(parseSourcesJSON(readFile("tests/fixtures/sources.json"), table, error), error.c_str());
    expectOne(table.items.size() == 1, "one source parsed");
    expectOne(table.items[0].id == "example", "slug kept");
    expectOne(table.items[0].shape.imageAttributes.size() == 2, "image attribute defaults kept");
    expectOne(table.items[0].shape.blocked.size() == 1, "blocked rule parsed");
    replaceSources(table);

    expectOne(sourceForURL("https://games.example.org/games/1.html") == 1, "own host recognised");
    expectOne(sourceForURL("https://www.games.example.org/games/1.html") == 1, "www host recognised");
    expectOne(sourceForURL("https://elsewhere.example/games/1.html") == 0, "foreign host rejected");
    expectOne(sourceForURL("http://games.example.org/games/1.html") == 0, "plain http rejected");
    expectOne(sourceForURL("https://games.example.org.attacker.test/x") == 0, "lookalike host rejected");
    expectOne(sourceURL("https://games.example.org/games/1.html"), "sourceURL agrees with table");

    expectOne(table.listingURL(1, 1) == "https://games.example.org/games/", "page one listing");
    expectOne(table.listingURL(1, 3) == "https://games.example.org/games/page/3/", "later page listing");
    Source single = table.items[0];
    single.pageListing.clear();
    auto singleSource = table; singleSource.items[0] = single; replaceSources(singleSource);
    expectOne(sourceSnapshot()->listingURL(1, 2).empty(), "no page template means no later pages");
    replaceSources(table);

    expectOne(table.resolve(1, "/art/1.jpg") == "https://games.example.org/art/1.jpg", "absolute path resolved");
    expectOne(table.resolve(1, "//cdn.example.org/a.jpg") == "https://cdn.example.org/a.jpg", "protocol-relative resolved");
    expectOne(table.resolve(1, "https://elsewhere.example/a.jpg") == "https://elsewhere.example/a.jpg", "absolute URL kept");

    auto games = parseListing(readFile("tests/fixtures/store-listing.html"), 1);
    expectOne(games.size() == 3, "offsite cards are skipped, artwork-less cards are kept");
    expectOne(games[0].title == "First Game", "title from selector");
    expectOne(games[0].url == "https://games.example.org/games/101-first-game.html", "card URL resolved");
    expectOne(games[0].image == "https://games.example.org/art/101.jpg", "data-src preferred");
    expectOne(games[0].size == "1.2 GB", "size from selector");
    expectOne(games[1].title == "Second Game & Friends", "entities decoded");
    expectOne(games[1].image == "https://games.example.org/art/102.jpg", "src fallback");
    expectOne(games[2].title == "No Artwork" && games[2].image.empty(), "card without artwork still lists");
    expectOne(games[0].id != games[1].id, "ids differ");
    expectOne(games[0].id == stableGameId(games[0].url), "id is stable for a URL");

    auto again = parseListing(readFile("tests/fixtures/store-listing.html"), 1);
    expectOne(again.size() == games.size() && again[0].id == games[0].id, "repeat parse is stable");

    auto links = parseDownloadLinks(readFile("tests/fixtures/store-detail.html"), 1);
    expectOne(links.size() == 2, "locked, offsite, plain and duplicate anchors dropped");
    expectOne(links[0].url == "https://games.example.org/files/first-game.torrent", "class link kept");
    expectOne(links[1].url == "https://games.example.org/index.php?do=download&id=101", "marker link kept");
    expectOne(links[1].title == "Mirror", "anchor text becomes the label");

    std::string round = sourcesToJSON(table);
    SourceTable reloaded;
    expectOne(parseSourcesJSON(round, reloaded, error), error.c_str());
    expectOne(reloaded.items.size() == table.items.size(), "round trip keeps count");
    expectOne(reloaded.items[0].shape.linkClasses == table.items[0].shape.linkClasses, "round trip keeps classes");
    expectOne(reloaded.items[0].base == table.items[0].base, "round trip keeps base");

    SourceTable empty;
    expectOne(!parseSourcesJSON("{", empty, error), "truncated JSON rejected");
    expectOne(!parseSourcesJSON("{\"sources\":{}}", empty, error), "non-array sources rejected");
    expectOne(!parseSourcesJSON("{\"sources\":[{\"base\":\"http://x.test\",\"shape\":{\"item\":\".c\"}}]}", empty, error), "http base rejected");
    expectOne(!parseSourcesJSON("{\"sources\":[{\"base\":\"https://x.test\"}]}", empty, error), "missing shape rejected");
    expectOne(parseSourcesJSON("{\"sources\":[]}", empty, error), "empty source list allowed");
    expectOne(empty.items.empty(), "empty table stays empty");

    JValue parsed;
    expectOne(jsonParse("{\"a\":[1,true,\"x\\u00e9\\n\"],\"b\":{\"c\":null}}", parsed), "json parser accepts mixed values");
    expectOne(parsed.list("a") && parsed.list("a")->items.size() == 3, "json array parsed");
    expectOne(parsed.list("a")->items[2].text == "x\xc3\xa9\n", "json unicode and escape decoded");

    replaceSources({});
    expectOne(parseListing(readFile("tests/fixtures/store-listing.html"), 1).empty(), "no sources means no games");
    std::cout << "Source model, generic listing, download detection, JSON round trip and rejection paths passed.\n";
}
