<div align="center">
  <img src="screenshots/nullstore-logo.png" width="160" alt="NULL_store logo">
  <h1>NULL_store</h1>
  <p><strong>PS4 homebrew game catalogue and BitTorrent download manager for HEN-enabled consoles.</strong></p>
  <p>Browse game listings, queue torrent downloads, install compatible PKG files, or keep transfers running with the optional background service.</p>
  <p>YOUR SYSTEM. YOUR RULES.</p>
  <p>
    <a href="../../releases/latest"><img alt="Download PS4 packages" src="https://img.shields.io/badge/Download-PS4%20PKGs-087652?style=for-the-badge"></a>
    <a href="https://www.youtube.com/watch?v=04Qp9h_wZIk"><img alt="Watch the video" src="https://img.shields.io/badge/Watch-Video-087652?style=for-the-badge"></a>
    <a href="ROADMAP.md"><img alt="View the roadmap" src="https://img.shields.io/badge/View-Roadmap-12332A?style=for-the-badge"></a>
    <a href="TESTING.md"><img alt="Read the test guide" src="https://img.shields.io/badge/Read-Test%20guide-12332A?style=for-the-badge"></a>
  </p>
</div>

![PS4 homebrew game catalogue in NULL_store](screenshots/catalogue.png)

## Watch NULL_store in action

[![Watch the NULL_store video on YouTube](https://img.youtube.com/vi/04Qp9h_wZIk/hqdefault.jpg)](https://www.youtube.com/watch?v=04Qp9h_wZIk)

[Watch the video on YouTube](https://www.youtube.com/watch?v=04Qp9h_wZIk).

> **Keep downloads running after you close NULL_store.** Install and start the optional Background Service, then confirm Downloads shows **Connected**. Transfers can continue after the store closes while the PS4 stays awake and HEN remains active. Without the connected service, keep NULL_store open. Rest mode is not supported.

## Install a downloaded package from NULL_store

When a torrent is complete, highlight it in **Downloads**, press **Triangle**, choose a compatible complete `.pkg`, and confirm. NULL_store starts and tracks the PS4's native install task. Check PS4 Notifications for the system result. The console may not provide a percentage, so the status can move from **Installing** to **Installed** without showing intermediate progress.

Only compatible, complete PKG files can be installed this way. Archives and split packages need to be extracted or joined first. Install a base game before its updates or DLC.

## Get started

1. Download and install the [NULL_store PKG](../../releases/latest/download/NULL_store-PS4-01.01.pkg) with HEN active.
2. Open **NULL_store** from the home screen. Browse the catalogue, search with **Triangle**, or save favorites with **Square**.
3. Open a listing and choose **Download** to add its torrent to the queue.
4. Press **R1** until **Downloads** is selected. Pause or resume jobs with **X**, press **Square** to remove a job and choose whether to keep its files, or use **Triangle** on a completed job to install a compatible PKG.
5. Press **Options** on the catalogue to open **Settings**: catalogue source, sort order, download location, Debrid service and its API key (Real-Debrid, AllDebrid, Premiumize, TorBox, Debrid-Link), official artwork and catalogue refresh.
6. The store follows the console's language: English (the default), Brazilian Portuguese, Spanish, Turkish, French or Italian.

## Keep downloads running in the background

The main app downloads while it is open. For transfers to continue after you close it:

1. In **Downloads**, press **L3** and choose **Install included worker**. Confirm the package installation and wait for it to finish.
2. In the same menu, choose **Start worker** and wait for Downloads to show **Connected** before closing the store. When the worker is stopped, start it from this menu again. The store connects to an already running matching worker when you reopen it.
3. Close NULL_store. The service can continue the transfer while the PS4 remains awake and HEN stays active.

Use **Downloads → L3 → Uninstall worker** to remove the background worker. The store keeps your queue and files. When the installed build matches the included worker, the menu also offers **Reinstall worker**. While the worker is running, choose **Stop worker** to return downloads to the open store.

The service does not download in rest mode or start automatically after a reboot. Reopen NULL_store after restarting the console.

## PS4 game catalogue and BitTorrent downloads

PS4 Torrent, GameTorrent and Love Games are ready on first launch. To add another site, open **Options → Catalog source** and scan the QR code with your phone. You can also import [sources.json](SOURCES.md) from USB on first launch. The store opens with a bundled catalogue and cover thumbnails, then refreshes its sources in the background. A tile marked **2 versions** or **3 versions** opens a list of releases and their sources. Use **Up/Down** to choose one and **X** to see its download links. Open **Settings** with **Options** and choose **Refresh catalogue** to refresh it now. Starting a download fetches the torrent from its source and contacts BitTorrent trackers to find peers. Peer availability depends on the torrent and your network, so some transfers may start slowly or find no usable peers.

With a Debrid service and its API key set in **Settings**, the torrent is sent to that service instead: it downloads the game on its own servers (the queue shows its progress), then NULL_store downloads the files from the service over HTTPS, resuming a file where it stopped. The transfer's id at the service is kept next to the torrent, so pausing and resuming does not add the torrent again.

[See the PS4 test guide.](TESTING.md)

## Known limitations and test status

- The store groups matching listings. Titles with different names and no shared title ID can still appear separately.
- Transfers can be slow or fail when trackers are unavailable or no compatible peers can be reached.
- Package installation supports complete compatible `.pkg` files; it does not extract archives or join split packages.
- The Background Service requires HEN to remain active and the console to stay awake. Rest mode and automatic startup are not supported.
- PS4 may not report install percentages; check Notifications for the final system result.

Console testing has been performed on PS4 system software **13.00** with HEN active. The exact HEN/GoldHEN build was not recorded, and other firmware/HEN combinations have not been verified. Catalogue browsing, foreground transfers, and in-app PKG installation have been tried on-console. The Background Service's close-app behavior still needs further testing across console setups.

<details>
<summary>See the Downloads and Background Service screens</summary>

![Downloads queue with a sample transfer](screenshots/downloads.png)

![Background Service options](screenshots/background-service.png)

</details>

## Frequently asked questions

### Do I need to install the Background Service package separately?

The store includes the download worker. Install and start it from **Downloads → L3** to continue a transfer after closing the store. The separate Background Service setup app offers another way to install and start the same worker. Foreground downloads work with the store alone.

## Disclaimer

NULL_store is a BitTorrent client for listings from third-party sites. This project publishes the app and its optional background service; it does not host, store, or distribute game files through this repository or a NULL_store server.

The app saves downloads on your PS4 and may upload pieces of those files to other BitTorrent peers during a transfer. Check your local laws and only download or share content you have permission to use.

## Source and building

The source code and host tests are available in this repository. See [BUILDING.md](BUILDING.md) for build requirements, [ROADMAP.md](ROADMAP.md) for planned improvements, and [CONTRIBUTING.md](CONTRIBUTING.md) to get involved. NULL_store is released under [GPL-3.0](LICENSE). Third-party components retain their respective licenses.

## Third-party software


The store renders with the precompiled Piglet shader binaries of the SDL2 PS4 port by cpasjuste, under the zlib license ([notice](third_party/piglet_shaders/LICENSE-SDL.txt)), and uses the public-domain [stb](third_party/stb/LICENSE) libraries for images and fonts. WebP artwork is decoded with [libwebp](third_party/libwebp/README.nullstore.txt) 1.4.0, under its [BSD license](third_party/libwebp/COPYING). Letters the store's ABeeZee font lacks (Turkish ğ, ş, İ) come from the Inter font, under the SIL Open Font License ([license](assets/LICENSE-font-fallback.txt)).

[Download checksums](SHA256SUMS.txt)
