libwebp 1.4.0 (https://chromium.googlesource.com/webm/libwebp), decoder only.
Taken from the official release libwebp-1.4.0.tar.gz
(sha256 61f873ec69e3be1b99535634340d5bde750b2e4447caa1db9f61be3fd49ab1e5):
the sources of the libwebpdecoder target in makefile.unix, without the NEON, MIPS
and MSA variants, and the headers of src/dec, src/dsp, src/utils, src/webp and
src/enc (decoder headers include some encoder declarations).
No file was changed. Licence: COPYING (BSD 3-clause) and PATENTS.
