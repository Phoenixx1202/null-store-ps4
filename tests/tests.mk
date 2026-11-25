# Host checks (macOS / Linux): the torrent engine, the download queue, the
# service protocol, the installer adapter and the store's state, built with the
# host compiler under ASan/UBSan. Included by the Makefile.
build/test_worker_entry: tests/test_worker_entry.cpp src/worker_entry.cpp src/worker_entry.h src/worker_launch_attempt.h
	@mkdir -p build
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -idirafter $(TOOLCHAIN)/include $< -o $@
.PHONY: test-worker-entry
test: test-worker-entry
test-worker-entry: build/test_worker_entry
	./build/test_worker_entry

build/test_network_ps4: tests/test_network_ps4.cpp src/network_ps4.cpp src/network_ps4.h build/trusted_roots.h
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -idirafter $(TOOLCHAIN)/include $< -lpthread -o $@
.PHONY: test-network
test: test-network
test-network: build/test_network_ps4
	./build/test_network_ps4
build/test_sony_art: tests/test_sony_art.cpp src/sony_art.h src/json_lite.h
	@mkdir -p build
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -Isrc $< -o $@
.PHONY: test-sony-art
test: test-sony-art
test-sony-art: build/test_sony_art
	./build/test_sony_art
build/torrent_host.o: src/torrent.c src/data_root.h src/torrent.h src/torrent_resume.h src/torrent_checkpoint.h src/path_info.h
	$(HOST_CC) -D_DEFAULT_SOURCE -DPORTAL_HOST -D_POSIX_C_SOURCE=200809L -std=c11 -O1 -g -fsanitize=address,undefined -c $< -o $@
build/torrent_driver: tests/torrent_driver.c build/torrent_host.o
	$(HOST_CC) -D_DEFAULT_SOURCE -std=c11 -O1 -g -fsanitize=address,undefined $^ -o $@
build/test_torrent_unit: tests/test_torrent_unit.c src/torrent.c src/torrent.h src/torrent_resume.h src/torrent_checkpoint.h src/path_info.h
	$(HOST_CC) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 -O1 -g -fsanitize=address,undefined $< -o $@
build/test_torrent_socket: tests/test_torrent_socket.c src/torrent.c src/torrent.h src/net_compat.h src/torrent_resume.h src/torrent_checkpoint.h src/path_info.h
	$(HOST_CC) -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 -O1 -g -fsanitize=address,undefined $< -o $@
build/test_download_manager: tests/test_download_manager.cpp src/download_manager.cpp src/download_manager.h src/download_model.h src/queue_store.h build/torrent_host.o
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined tests/test_download_manager.cpp src/download_manager.cpp src/debrid.cpp build/torrent_host.o -lpthread -o $@
build/queue_driver: tests/queue_driver.cpp src/download_manager.cpp src/download_manager.h src/queue_store.h build/torrent_host.o
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined tests/queue_driver.cpp src/download_manager.cpp src/debrid.cpp build/torrent_host.o -lpthread -o $@
build/test_installer: tests/test_installer.cpp src/installer.cpp src/installer.h src/queue_store.h src/path_info.h src/install_log.h src/bgft_abi.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined tests/test_installer.cpp src/installer.cpp -lpthread -o $@
test: build/controller-driver build/test_service_permissions build/test_sources build/test_sources_server build/test_qr build/torrent_driver build/test_torrent_unit build/test_torrent_socket build/test_download_manager build/queue_driver build/test_installer build/portal-service-host build/service-client-driver build/test_catalog
	$(PYTHON) -m unittest discover -s tests -v
	./build/test_service_permissions
	./build/test_sources
	./build/test_sources_server
	./build/test_qr
	./build/test_torrent_unit
	./build/test_torrent_socket
	./build/test_download_manager
	./build/test_installer
	./build/test_catalog
build/portal-service-host: src/service_main.cpp src/service_server.cpp src/service_protocol.cpp src/download_manager.cpp src/providers.h src/catalog.h src/download_model.h src/service_protocol.h src/service_server.h src/download_manager.h src/queue_store.h build/torrent_host.o
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined src/service_main.cpp src/service_server.cpp src/service_protocol.cpp src/download_manager.cpp src/debrid.cpp build/torrent_host.o -lpthread -o $@
build/service-client-driver: tests/service_client_driver.cpp src/service_client.cpp src/service_client.h src/service_protocol.cpp src/service_protocol.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined tests/service_client_driver.cpp src/service_client.cpp src/service_protocol.cpp -lpthread -o $@
build/test_sources: tests/test_sources.cpp src/sources.h src/source_model.h
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined $< -o $@
build/test_sources_server: tests/test_sources_server.cpp src/sources_server.cpp src/sources.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined $< src/sources_server.cpp -lpthread -o $@
build/test_qr: tests/test_qr.cpp src/qr.h src/qrcodegen.c
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined $< src/qrcodegen.c -o $@
build/test_service_permissions: tests/test_service_permissions.cpp src/service_start_ps4.cpp src/service_protocol.cpp src/service_files.h src/service_setup_log.h src/path_info.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined -idirafter $(TOOLCHAIN)/include tests/test_service_permissions.cpp src/service_protocol.cpp -lpthread -o $@
build/controller-driver: tests/controller_driver.cpp src/download_controller.cpp src/download_controller.h src/service_client.cpp src/download_manager.cpp src/service_protocol.cpp src/queue_store.h src/catalog.h src/providers.h build/torrent_host.o
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined tests/controller_driver.cpp src/download_controller.cpp src/service_client.cpp src/download_manager.cpp src/debrid.cpp src/service_protocol.cpp build/torrent_host.o -lpthread -o $@
build/test_provider_cache: tests/test_provider_cache.cpp src/providers.h src/queue_store.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined $< -o $@
test: test-provider-cache
.PHONY: test-provider-cache
test-provider-cache: build/test_provider_cache
	./build/test_provider_cache
build/test_service_files: tests/test_service_files.cpp src/service_files.h src/path_info.h src/queue_store.h
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined $< -o $@
build/test_file_info: tests/test_file_info.c src/path_info.h
	$(HOST_CC) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -std=c11 -O1 -g -fsanitize=address,undefined $< -o $@
test: test-service-files
.PHONY: test-service-files
test-service-files: build/test_service_files build/test_file_info
	./build/test_service_files
	./build/test_file_info
build/test_installer_ps4: tests/test_installer_ps4.cpp src/installer_ps4.cpp src/installer.h src/bgft_abi.h src/install_log.h src/worker_package.h
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -idirafter $(TOOLCHAIN)/include $< -o $@
build/test_installer_retry: tests/test_installer_retry.cpp src/installer.cpp src/installer.h src/bgft_abi.h src/queue_store.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined tests/test_installer_retry.cpp src/installer.cpp -lpthread -o $@
test: test-installer-adapter
build/test_startup_ps4: tests/test_startup_ps4.cpp src/startup_ps4.h src/install_log.h
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -idirafter $(TOOLCHAIN)/include $< -o $@
build/test_installer_progress: tests/test_installer_progress.cpp src/installer.cpp src/installer.h src/queue_store.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined tests/test_installer_progress.cpp src/installer.cpp -lpthread -o $@
test-installer-adapter: build/test_installer_ps4 build/test_installer_retry build/test_startup_ps4 build/test_installer_progress
	./build/test_installer_ps4
	./build/test_installer_retry
	./build/test_startup_ps4
	./build/test_installer_progress
build/qr_test.o: src/qrcodegen.c src/qrcodegen.h
	$(HOST_CC) -std=c11 -O1 -g -fsanitize=address,undefined -c $< -o $@
build/test_catalog: tests/test_catalog.cpp src/ui/StoreState.h src/installer.h src/providers.h src/path_info.h build/qr_test.o
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined -Isrc -idirafter $(TOOLCHAIN)/include $< build/qr_test.o -o $@
build/test_queue_handoff: tests/test_queue_handoff.cpp src/download_controller.cpp src/download_controller.h src/service_client.cpp src/service_client.h src/download_manager.cpp src/download_manager.h src/service_protocol.cpp src/queue_store.h build/torrent_host.o
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined -idirafter $(TOOLCHAIN)/include tests/test_queue_handoff.cpp src/download_controller.cpp src/service_client.cpp src/download_manager.cpp src/debrid.cpp src/service_protocol.cpp build/torrent_host.o -lpthread -o $@
.PHONY: test-queue-handoff
test: test-queue-handoff
test-queue-handoff: build/test_queue_handoff build/portal-service-host
	./build/test_queue_handoff "$(CURDIR)/build/portal-service-host"
build/test_text_input: tests/test_text_input.cpp src/text_input_ps4.cpp src/text_input_ps4.h
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined -idirafter $(TOOLCHAIN)/include $< -o $@
.PHONY: test-text-input
test: test-text-input
test-text-input: build/test_text_input
	./build/test_text_input
build/test_debrid: tests/test_debrid.cpp src/debrid.cpp src/debrid.h src/json_lite.h src/catalog.h
	@mkdir -p build
	$(HOST_CXX) -DPORTAL_HOST -std=c++17 -O1 -g -fsanitize=address,undefined -Isrc tests/test_debrid.cpp src/debrid.cpp -o $@
.PHONY: test-debrid
test: test-debrid
test-debrid: build/test_debrid
	./build/test_debrid
build/test_i18n: tests/test_i18n.cpp src/ui/I18n.cpp src/ui/I18n.h
	@mkdir -p build
	$(HOST_CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -Isrc tests/test_i18n.cpp src/ui/I18n.cpp -o $@
.PHONY: test-i18n
test: test-i18n
test-i18n: build/test_i18n
	./build/test_i18n
