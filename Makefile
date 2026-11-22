# =============================================================================
# NULL_store - PS4 homebrew (OpenOrbis toolchain), builds on Windows, Linux
# and macOS. See BUILDING.md.
#
#   make                 store package      build/$(CONTENT_ID).pkg
#   make eboot           just the store executable (eboot.bin)
#   make service-package optional Background Service setup package
#   make test-package    store package with the optional download worker embedded
#   make preview         PC previews of every screen -> build/preview/*.png
#   make test            host checks (macOS / Linux)
#   make validate        inspect the built package
#   make clean
# =============================================================================
OO_PS4_TOOLCHAIN ?= $(abspath ../toolchain/OpenOrbis/PS4Toolchain)
TOOLCHAIN := $(subst \,/,$(OO_PS4_TOOLCHAIN))
export OO_PS4_TOOLCHAIN := $(TOOLCHAIN)

# --- Host tools ---------------------------------------------------------------
# LLVM: optional directory holding clang, clang++ and ld.lld (default: PATH).
ifeq ($(OS),Windows_NT)
  HOST_OS := windows
  EXE := .exe
  PYTHON ?= python
  FSELF := $(TOOLCHAIN)/bin/windows/create-fself.exe
  # The recipes are POSIX shell: use Git for Windows' sh when make was started
  # from PowerShell or cmd.
  GIT_SH := $(firstword $(wildcard C:/PROGRA~1/Git/bin/sh.exe C:/PROGRA~2/Git/bin/sh.exe))
  ifneq ($(GIT_SH),)
    SHELL := $(GIT_SH)
    # make runs simple commands without the shell: its tools must be on PATH.
    export PATH := $(patsubst %/bin/sh.exe,%/usr/bin,$(GIT_SH));$(PATH)
  endif
else ifeq ($(shell uname -s),Darwin)
  HOST_OS := macos
  EXE :=
  PYTHON ?= python3
  LLVM ?= /opt/homebrew/opt/llvm/bin
  PS4_LD ?= /opt/homebrew/opt/lld/bin/ld.lld
  FSELF := $(TOOLCHAIN)/bin/macos/create-fself-macos
else
  HOST_OS := linux
  EXE :=
  PYTHON ?= python3
  FSELF := $(TOOLCHAIN)/bin/linux/create-fself
endif
LLVM_DIR := $(if $(LLVM),$(LLVM)/,)
PS4_CXX ?= $(LLVM_DIR)clang++
PS4_CC ?= $(LLVM_DIR)clang
PS4_LD ?= $(LLVM_DIR)ld.lld
HOST_CXX ?= clang++
HOST_CC ?= clang
PKGTOOL := $(TOOLCHAIN)/bin/$(HOST_OS)/PkgTool.Core$(EXE)

# --- Package metadata -----------------------------------------------------------
CONTENT_ID := IV0000-NULL00001_00-NULLSTOR00000000
SERVICE_CONTENT_ID := IV0000-NULL00002_00-NULLSTORSERVICE0
# Fake-signing profile of the store. The permissions are the ones of flatz's
# ps4_remote_pkg_installer; they also allow loading the system's Piglet
# (OpenGL ES) module the renderer draws with. PAID can be overridden to try
# another program authority on a console (make PAID=0x3800000000000011).
PAID := 0x3100000000000002
# 1 asks the PlayStation Store for the icons and names orbispatches.com does
# not have (src/sony_art.h). Its crashes came from libSceHttp, which no
# download uses any more (src/network_bypass.cpp).
SONY_ART ?= 1
INSTALLER_AUTH_INFO := $(shell cat tools/installer-auth.hex)

# --- PS4 flags ------------------------------------------------------------------
PS4_COMMON := -D__ORBIS__ --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -mstackrealign -MMD -MP \
              -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
              -Wno-missing-field-initializers -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include \
              -ffile-prefix-map=$(TOOLCHAIN)=/openorbis -DNS_PLATFORM_PS4 -DDUSK_PLATFORM_PS4 -DNS_SONY_ART=$(SONY_ART) -Isrc -Ithird_party -Ithird_party/libwebp \
              -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -Ithird_party/net
PS4_CXXFLAGS := $(PS4_COMMON) -std=gnu++17 -isystem $(TOOLCHAIN)/include/c++/v1
PS4_CFLAGS := $(PS4_COMMON) -Wno-misleading-indentation
LIBS := -lc++ -lc++abi -lc -lm -lpthread -lkernel -lSceVideoOut -lScePad -lSceUserService -lSceSystemService \
        -lSceSysmodule -lSceNet -lSceNetCtl -lSceSsl -lSceHttp -lSceBgft -lSceLncUtil
GL_LIBS := -lScePigletv2VSH
PS4_LINK = $(PYTHON) tools/link_ps4.py $(PS4_LD) --no-mmap-output-file -m elf_x86_64 -pie \
           --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(TOOLCHAIN)/lib/crt1.o

# --- Sources ----------------------------------------------------------------------
OBJ := build/ps4
# Rendering: the Piglet context, the batched renderer and what it draws with.
GFX_SRC := $(wildcard src/graphics/*.cpp) src/core/Log.cpp src/platform/ps4/GLContextPs4.cpp src/ui/Widgets.cpp
# libwebp's decoder: the artwork of the game pages is WebP.
WEBP_C := $(wildcard third_party/libwebp/src/dec/*.c third_party/libwebp/src/dsp/*.c third_party/libwebp/src/utils/*.c)
GFX_C := $(wildcard third_party/piglet_shaders/*.c) $(WEBP_C)
# The store: application loop, screens, input.
STORE_SRC := src/sources_server.cpp src/qrcodegen.c src/main.cpp $(wildcard src/app/*.cpp src/input/*.cpp) $(filter-out src/ui/Widgets.cpp,$(wildcard src/ui/*.cpp)) \
             src/platform/ps4/PadPs4.cpp \
             src/text_input_ps4.cpp src/download_controller.cpp src/service_client.cpp src/packaged_worker_launch.cpp src/installer.cpp \
             src/installer_ps4.cpp
# Shared with the service daemon and the setup application.
ENGINE_SRC := src/download_manager.cpp src/debrid.cpp src/service_protocol.cpp
SETUP_SRC := src/service_installer.cpp src/installer.cpp src/installer_ps4.cpp src/packaged_worker_launch.cpp src/service_protocol.cpp src/network_ps4.cpp
DAEMON_SRC := src/service_main.cpp src/service_server.cpp src/network_ps4.cpp src/service_log.cpp
# The PlayStation Store DNS-bypass client (src/network_bypass.cpp) and its
# vendored transport: BearSSL TLS and the net/Socket helpers, from
# third_party/ (see third_party/net/upstream.json and BUILDING.md).
BEARSSL_C := $(wildcard third_party/bearssl/src/*.c third_party/bearssl/src/*/*.c)
BYPASS_NET_SRC := third_party/net/Socket.cpp third_party/net/Url.cpp third_party/net/TrustStore.cpp
BYPASS_SRC := src/network_bypass.cpp

objs = $(patsubst %,$(OBJ)/%.o,$(1))
STORE_OBJS := $(call objs,$(STORE_SRC) $(GFX_SRC) $(GFX_C) $(ENGINE_SRC) src/network_ps4.cpp src/torrent.c \
              $(BYPASS_SRC) $(BYPASS_NET_SRC) $(BEARSSL_C))
SETUP_OBJS := $(call objs,$(SETUP_SRC) $(GFX_SRC) $(GFX_C) $(BYPASS_SRC) $(BYPASS_NET_SRC) $(BEARSSL_C))
DAEMON_OBJS := $(patsubst %,build/daemon/%.o,$(DAEMON_SRC)) $(call objs,$(ENGINE_SRC) src/torrent.c \
               $(BYPASS_SRC) $(BYPASS_NET_SRC) $(BEARSSL_C))

ICONS := assets/brand/store-icon.png assets/brand/service-icon.png sce_sys/icon0.png
SERVICE_NOTICES := assets/service/cacert.pem assets/service/TLS-LICENSE.txt assets/service/TLS-PROVENANCE.txt \
                   assets/service/GOLDHEN-SDK-LICENSE.txt assets/service/ITEMZFLOW-LICENSE.txt
RUNTIME_MODULES := sce_module/libc.prx sce_module/libSceFios2.prx

.PHONY: all eboot icons preview validate service-package clean check-toolchain test-package
.DEFAULT_GOAL := all
all: build/$(CONTENT_ID).pkg
eboot: eboot.bin

# The store needs declarations (internal module ids) that old toolchain
# releases do not have.
check-toolchain:
	@test -f "$(TOOLCHAIN)/lib/crt1.o" || { echo "OO_PS4_TOOLCHAIN does not point to an OpenOrbis toolchain: $(TOOLCHAIN)"; exit 1; }
	@grep -q "ORBIS_SYSMODULE_INTERNAL_PAD" "$(TOOLCHAIN)/include/orbis/_types/sysmodule.h" || { echo "This OpenOrbis toolchain is too old (no ORBIS_SYSMODULE_INTERNAL_PAD): $(TOOLCHAIN)"; exit 1; }

# --- Compile ----------------------------------------------------------------------
build/trusted_roots.h: tools/embed_trust.py assets/service/cacert.pem
	$(PYTHON) tools/embed_trust.py
build/sony_ca_bundle.h: build/trusted_roots.h
	@test -s $@ || $(PYTHON) tools/embed_trust.py

$(OBJ)/%.cpp.o: %.cpp Makefile | check-toolchain build/trusted_roots.h build/sony_ca_bundle.h
	@mkdir -p $(dir $@)
	@echo "[ps4] $<"
	@$(PS4_CXX) $(PS4_CXXFLAGS) -c $< -o $@

# What SONY_ART switches is rebuilt when its value changes.
SONY_ART_STAMP := build/sony-art-$(SONY_ART).stamp
$(SONY_ART_STAMP):
	@mkdir -p build
	@rm -f build/sony-art-*.stamp
	@touch $@
$(OBJ)/src/ui/CoverCache.cpp.o: $(SONY_ART_STAMP)

# Vendored code is built without our warnings.
$(OBJ)/third_party/libwebp/%.o: PS4_CFLAGS += -w
$(OBJ)/third_party/bearssl/%.o: PS4_CFLAGS += -w
$(OBJ)/third_party/net/%.o: PS4_CXXFLAGS += -w

$(OBJ)/%.c.o: %.c Makefile | check-toolchain
	@mkdir -p $(dir $@)
	@echo "[ps4] $<"
	@$(PS4_CC) $(PS4_CFLAGS) -c $< -o $@

build/daemon/%.cpp.o: %.cpp Makefile | check-toolchain build/trusted_roots.h build/sony_ca_bundle.h
	@mkdir -p $(dir $@)
	@echo "[daemon] $<"
	@$(PS4_CXX) $(PS4_CXXFLAGS) -DPORTAL_SERVICE_DAEMON -c $< -o $@

# --- Store --------------------------------------------------------------------------
build/nullstore.elf: $(STORE_OBJS) tools/link_ps4.py
	$(PS4_LINK) $(STORE_OBJS) $(LIBS) $(GL_LIBS) -lSceImeDialog -lSceCommonDialog -o $@

eboot.bin: build/nullstore.elf tools/installer-auth.hex Makefile
	$(FSELF) -in=$< -out=build/nullstore.oelf --eboot $@ --ptype fake --paid $(PAID) --authinfo $(INSTALLER_AUTH_INFO)

# A worker is an independently installed application using ordinary app permissions.
WORKER_PKG := build/IV0000-NULL00003_00-NULLSTOREWORKER0.pkg
WORKER_ID_INPUTS := $(filter-out build/daemon/src/service_server.cpp.o,$(DAEMON_OBJS)) src/service_server.cpp src/service_protocol.h Makefile tools/package_worker.py
build/daemon/src/service_main.cpp.o: PS4_CXXFLAGS += -DNULLSTORE_PACKAGED_WORKER
build/daemon/src/service_server.cpp.o: build/service_build_id.h
build/service_build_id.h: tools/service_build_id.py $(WORKER_ID_INPUTS)
	$(PYTHON) $< $(WORKER_ID_INPUTS)
assets/service/build-id.txt: build/service_build_id.h
	@test -s $@ || $(PYTHON) tools/service_build_id.py $(WORKER_ID_INPUTS)
build/packaged-worker.elf: $(DAEMON_OBJS) tools/link_ps4.py
	$(PS4_LINK) $(DAEMON_OBJS) $(LIBS) -o $@
build/packaged-worker.bin: build/packaged-worker.elf
	$(FSELF) -in=$< -out=build/packaged-worker.oelf --eboot $@ --paid 0x3800000000000011
$(WORKER_PKG): build/packaged-worker.bin assets/service/build-id.txt $(ICONS) $(RUNTIME_MODULES) $(SERVICE_NOTICES) tools/package_worker.py tools/package.py
	PKGTOOL="$(PKGTOOL)" $(PYTHON) tools/package_worker.py
assets/worker/download-worker.pkg: $(WORKER_PKG)
	@mkdir -p $(@D)
	cp $< $@
assets/worker/build-id.txt: assets/service/build-id.txt
	@mkdir -p $(@D)
	cp $< $@
WORKER_PAYLOAD := assets/worker/download-worker.pkg assets/worker/build-id.txt
worker-package: $(WORKER_PKG)

# --- Package ----------------------------------------------------------------------
# Icons are committed; `make icons` redraws them from the bag artwork.
icons:
	$(PYTHON) tools/build_icons.py
$(ICONS):
	$(PYTHON) tools/build_icons.py

# Runtime modules every OpenOrbis application ships.
sce_module/%.prx:
	@mkdir -p sce_module
	@test -f "$(TOOLCHAIN)/samples/piglet/sce_module/$*.prx" || { echo "Missing $@: copy it from an OpenOrbis toolchain (see BUILDING.md)"; exit 1; }
	cp "$(TOOLCHAIN)/samples/piglet/sce_module/$*.prx" $@

sce_sys/param.sfo: tools/package.py
	$(PYTHON) tools/package.py sfo

assets/covers.idx: assets/covers.pack
	@test -f $@ || $(PYTHON) tools/pack_covers.py
assets/covers.pack: tools/pack_covers.py $(wildcard assets/covers/*.jpg)
	$(PYTHON) tools/pack_covers.py

pkg.gp4: $(ICONS) $(SERVICE_NOTICES) $(RUNTIME_MODULES) assets/covers.pack assets/covers.idx \
         assets/brand/nullstore-bag.png $(WORKER_PAYLOAD) eboot.bin \
         sce_sys/param.sfo tools/package.py assets/font.ttf $(wildcard assets/covers/*.jpg)
	$(PYTHON) tools/package.py gp4

build/$(CONTENT_ID).pkg: pkg.gp4
	$(PYTHON) tools/package.py build $(PKGTOOL)

TEST_PKG := build/NULL_store-test.pkg
test-package: build/$(CONTENT_ID).pkg
	cp $< $(TEST_PKG)
	$(PKGTOOL) pkg_validate $(TEST_PKG)

validate: build/$(CONTENT_ID).pkg
	$(PKGTOOL) pkg_validate $<
	$(PYTHON) tools/verify_package.py $< --tool $(PKGTOOL) --catalog

# --- Background Service setup application ----------------------------------------------
build/service-installer.elf: $(SETUP_OBJS) tools/link_ps4.py
	$(PS4_LINK) $(SETUP_OBJS) $(LIBS) $(GL_LIBS) -o $@

# Loading Piglet needs the same permissions as the store.
build/service-installer.bin: build/service-installer.elf tools/installer-auth.hex
	$(FSELF) -in=$< -out=build/service-installer.oelf --eboot $@ --paid 0x3800000000000011 --authinfo $(INSTALLER_AUTH_INFO)

build/$(SERVICE_CONTENT_ID).pkg: $(ICONS) $(SERVICE_NOTICES) build/service-installer.bin $(WORKER_PAYLOAD) tools/package_service_installer.py tools/package.py
	PKGTOOL=$(PKGTOOL) $(PYTHON) tools/package_service_installer.py

service-package: build/$(SERVICE_CONTENT_ID).pkg

# --- PC preview -----------------------------------------------------------------------
# The screens drawn by the same layout code through the software backend.
PREVIEW_SRC := src/qrcodegen.c tools/preview.cpp src/graphics/Image.cpp src/graphics/Font.cpp src/graphics/Renderer.cpp \
               src/platform/host/SoftwareBackend.cpp src/ui/StoreView.cpp src/ui/Widgets.cpp src/ui/CoverCache.cpp \
               src/ui/GameArt.cpp src/ui/I18n.cpp src/core/Log.cpp
PREVIEW_FLAGS := -DPORTAL_HOST -DNS_HOST -std=c++17 -O2 -Wall -Wno-unused-function -Isrc -Ithird_party \
                 -Ithird_party/libwebp
HOST_SDK_FLAGS :=
ifeq ($(HOST_OS),windows)
  # The store's storage headers are POSIX: tools/win_compat supplies the few
  # calls the Windows runtime lacks.
  PREVIEW_FLAGS += -isystem tools/win_compat -D_CRT_SECURE_NO_WARNINGS -Wno-deprecated-declarations
  # clang picks the newest Windows SDK folder even when it is a driver kit
  # without the C runtime: name the newest one that has it.
  WINSDK_VERSION ?= $(shell for d in "/c/Program Files (x86)/Windows Kits/10/Include"/*/; do \
                      if [ -d "$$d/ucrt" ] && [ -d "$$d/um" ]; then basename "$$d"; fi; done | tail -1)
  HOST_SDK_FLAGS := $(if $(WINSDK_VERSION),-Xmicrosoft-windows-sdk-version $(WINSDK_VERSION),)
  PREVIEW_FLAGS += $(HOST_SDK_FLAGS)
endif
PREVIEW_HEADERS := $(wildcard src/*.h src/ui/*.h src/graphics/*.h src/core/*.h tools/win_compat/*.h tools/win_compat/sys/*.h)

HOST_TARGET := $(shell $(HOST_CC) -dumpmachine)
HOST_X86 := $(filter x86_64% i386% i686% amd64%,$(HOST_TARGET))
WEBP_HOST_OBJS := $(patsubst %.c,build/host/%.o,$(WEBP_C))
# With the Microsoft toolchain libwebp turns its SSE4.1 paths on by itself: those
# files need the instruction set enabled (the code still checks the CPU first).
HOST_WEBP_CONFIG := build/host/webp-config/src/webp/config.h
$(HOST_WEBP_CONFIG):
	@mkdir -p $(dir $@)
	@echo '/* Portable C decoder for host previews. */' > $@
build/host/%.o: %.c $(HOST_WEBP_CONFIG) Makefile
	@mkdir -p $(dir $@)
	@$(HOST_CC) -O2 -w -DHAVE_CONFIG_H -Ibuild/host/webp-config -Ithird_party/libwebp $(HOST_SDK_FLAGS) $(if $(and $(HOST_X86),$(findstring sse41,$<)),-msse4.1,) -c $< -o $@

build/preview-tool$(EXE): $(PREVIEW_SRC) $(PREVIEW_HEADERS) $(WEBP_HOST_OBJS)
	@mkdir -p build
	$(HOST_CXX) $(PREVIEW_FLAGS) $(PREVIEW_SRC) $(WEBP_HOST_OBJS) -o $@

preview: build/preview-tool$(EXE)
	./build/preview-tool$(EXE) all

clean:
	rm -rf build eboot.bin pkg.gp4

include tests/tests.mk

-include $(STORE_OBJS:.o=.d) $(SETUP_OBJS:.o=.d) $(DAEMON_OBJS:.o=.d)
