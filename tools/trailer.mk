# Only the store plays trailers. FFmpeg runs on one dedicated decoding thread.
TRAILER_SDK := build/trailer-decoder
TRAILER_READY := $(TRAILER_SDK)/ready
$(TRAILER_READY): tools/build_trailer_decoder.py tools/trailer.mk
	$(PYTHON) tools/build_trailer_decoder.py $(TRAILER_SDK) "$(PS4_CC)" "$(PS4_LD)" "$(TOOLCHAIN)"
$(OBJ)/src/app/TrailerDecoder.cpp.o: PS4_CXXFLAGS += -isystem $(TRAILER_SDK)/include
$(OBJ)/src/app/TrailerDecoder.cpp.o: $(TRAILER_READY)
build/nullstore.elf: GL_LIBS += $(addprefix $(TRAILER_SDK)/lib/lib,$(addsuffix .a,avformat avcodec swresample swscale avutil))
build/nullstore.elf: $(TRAILER_READY)
