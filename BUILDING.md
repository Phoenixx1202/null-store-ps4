# Building NULL_store

The application, optional service, torrent engine, installer adapter and host tests are in this repository. The package targets build the store, download worker and optional setup app.

## Requirements

- The [OpenOrbis PS4 toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain), recent enough to declare the internal system modules (`ORBIS_SYSMODULE_INTERNAL_PAD`); the v0.5.2 installer is too old. Set `OO_PS4_TOOLCHAIN` to its folder, or pass it to `make`.
- LLVM: `clang`, `clang++` and `ld.lld` on `PATH`, or `LLVM=<folder>`. macOS defaults to Homebrew LLVM/LLD.
- GNU Make, Python 3.8 or newer, a POSIX shell and tar with xz support. Pillow is only needed by `make icons`.
- Windows: Git for Windows. The Makefile runs its recipes with Git's `sh`, so `make` works from PowerShell, cmd or Git Bash.

```
make eboot OO_PS4_TOOLCHAIN=D:/path/to/OpenOrbis-PS4-Toolchain
```

`make eboot` builds and fake-signs the store executable. `make` builds the store package and `make service-package` the setup app; both need the assets below. LibOrbisPkg comes with the toolchain.

## Source layout

| Folder | Contents |
| --- | --- |
| `src/app` | Application loop, catalogue refresh worker |
| `src/ui` | Store state (`StoreState`), screens (`StoreView`), shared widgets and controller keys (`Widgets`), game tiles (`CoverCache`: official icon, or the source's cover) and backgrounds (`GameArt`), colours and layout metrics (`Theme.h`) |
| `src/graphics` | Batched 2D renderer, glyph atlas, images, the OpenGL ES backend |
| `src/platform/ps4` | Piglet (EGL) context, DualShock 4 |
| `src/platform/host` | Software render backend for the PC preview |
| `src/input`, `src/core` | Controller actions and key-repeat, geometry, log |
| `src/*.cpp`, `src/*.h`, `src/torrent.c` | Catalogue sources, downloads, torrent engine, installer, background service |
| `third_party` | Piglet shader binaries (SDL2 PS4 port, zlib licence), stb, libwebp's decoder, BearSSL (MIT) and the `net` transport (OpenOrbis patterns) used by the PlayStation Store DNS bypass |

The store draws with Piglet, the console's OpenGL ES 2.0 implementation (`libScePigletv2VSH`). Retail firmware has no shader compiler, so the renderer links the precompiled shader pair in `third_party/piglet_shaders`. Everything on screen is a textured quad with premultiplied alpha, collected per frame and drawn with one call per texture run.

## Trailer decoder

The first store build downloads the pinned FFmpeg source archive and verifies its SHA-256. To build offline, set `NULLSTORE_FFMPEG_ARCHIVE` to a local copy of [ffmpeg-8.1.3.tar.xz](https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz). The build uses Clang and the LLVM archiver, ranlib and nm to produce the MP4, H.264 and AAC libraries in `build/trailer-decoder`. Install NASM for x86 SIMD acceleration in the PS4 player. The build checks PATH or `NULLSTORE_NASM` for the assembler. If you add it to an existing build, remove `build/trailer-decoder/ready` to rebuild the decoder. The worker and host UI preview build independently.

Run `sh tests/test_trailer_decoder.sh` with the FFmpeg CLI, its development libraries, libwebp and pkg-config installed to check playback, cancellation, silent video and invalid files on the host.

## PC preview

`make preview` builds `build/preview-tool` with the host compiler and writes one PNG per screen to `build/preview/`. It runs the same `StoreView` layout code through a software backend, so layout work can be checked without a console. Cover art is shown when `assets/covers/` or `build/preview-data/covers/` contain `<id>.jpg` files. On Windows it uses the Microsoft toolchain behind `clang++` and the small POSIX shims in `tools/win_compat`.

The preview does not exercise Piglet itself: test on a PS4 before releasing.

## Assets and package build

The repository includes the catalogue snapshot and thumbnail pack for the three built-in sources. To override or add a thumbnail, put a JPEG named after the numeric listing ID in `assets/covers/` and run `make assets/covers.pack`.

The Makefile copies `libc.prx` and `libSceFios2.prx` from the toolchain's `samples/piglet/sce_module` directory. Run `make` for the store or `make service-package` for the optional setup app.

`make worker-package` builds the standalone worker. Both the store and the optional setup app embed that package. `make test-package` also copies the store build to `build/NULL_store-test.pkg`.

Text assets are verified by hash and read by the console as they are, so `.gitattributes` keeps every text file LF on all platforms.

## GitHub Actions

`.github/workflows/build.yml` builds on every push (any branch), every pull request and on demand (Actions > Build > Run workflow). It installs LLVM on Ubuntu, downloads the OpenOrbis toolchain release named in `OPENORBIS_VERSION` (v0.5.4), runs `make test-package` and uploads `NULL_store-PS4-<version>.pkg` with its SHA-256 as the `NULL_store-PS4` artifact. The version comes from `APP_VER` in `tools/package.py`. The store artifact includes the download worker. A second job runs the host checks.

## Host checks

`make test` runs the host suite on macOS or Linux using the OpenOrbis headers (`tests/tests.mk`); it needs POSIX and is not available with the Microsoft toolchain. These checks and the preview do not replace testing on a PS4.

The certificate bundle, SDK notices and installer provenance retain their respective notices under `assets/`.
