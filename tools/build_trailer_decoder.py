"""Cross-build the local-file MP4 decoder used by the store."""
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from urllib.request import urlopen

VERSION = "8.1.3"
ARCHIVE = f"ffmpeg-{VERSION}.tar.xz"
DIGEST = "7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3"
LIBS = ("avformat", "avcodec", "swresample", "swscale", "avutil")


def build(destination, cc, ld, sdk):
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    archive = Path(os.environ.get("NULLSTORE_FFMPEG_ARCHIVE", destination / ARCHIVE))
    if not archive.exists():
        with urlopen("https://ffmpeg.org/releases/" + ARCHIVE, timeout=60) as response:
            with tempfile.NamedTemporaryFile(dir=destination, delete=False) as out:
                shutil.copyfileobj(response, out)
        os.replace(out.name, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != DIGEST:
        raise ValueError(f"Checksum mismatch for {archive}")

    with tempfile.TemporaryDirectory(prefix="nullstore-decoder-") as folder:
        subprocess.run(["tar", "-xf", str(archive.resolve()), "-C", folder], check=True)
        src = Path(folder, f"ffmpeg-{VERSION}")
        clang = Path(shutil.which(cc) or cc).absolute()
        sdk = Path(sdk).absolute().as_posix()
        linker = Path(shutil.which(ld) or ld).absolute().as_posix()
        target = "--target=x86_64-pc-freebsd12-elf"
        cflags = [target, "-D__ORBIS__", "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE", "-fPIC",
                  "-mstackrealign", "-isystem", sdk + "/include", f"-ffile-prefix-map={src}=ffmpeg"]
        ldflags = [target, "-fuse-ld=" + linker, "-nostdlib", "-Wl,-e,main", "-L" + sdk + "/lib"]
        args = ["sh", "./configure", "--enable-cross-compile", "--arch=x86_64", "--target-os=freebsd",
                "--cc=" + clang.as_posix(), "--ld=" + clang.as_posix(), "--sysroot=" + sdk,
                "--prefix=" + destination.as_posix(), "--extra-cflags=" + shlex.join(cflags),
                "--extra-ldflags=" + shlex.join(ldflags), "--extra-libs=-lc -lm -lkernel -lpthread",
                "--disable-everything", "--disable-autodetect", "--disable-programs", "--disable-doc",
                "--disable-debug", "--disable-network", "--disable-avdevice", "--disable-avfilter",
                "--disable-pthreads", "--disable-iconv", "--disable-symver",
                "--enable-pic", "--enable-small", "--enable-decoder=h264,aac", "--enable-demuxer=mov",
                "--enable-parser=h264,aac", "--enable-protocol=file"]
        # Keep the SIMD paths that the PS4's x86 CPU supports. Other builds can
        # still compile the portable decoder when NASM is unavailable.
        nasm = shutil.which(os.environ.get("NULLSTORE_NASM", "nasm"))
        if nasm:
            args.extend(["--x86asmexe=" + Path(nasm).absolute().as_posix(),
                         "--disable-avx", "--disable-avx2", "--disable-avx512", "--disable-avx512icl",
                         "--disable-fma3", "--disable-fma4", "--disable-xop"])
        else:
            args.append("--disable-x86asm")
        for name in ("ar", "ranlib", "nm"):
            exe = "llvm-" + name + (".exe" if os.name == "nt" else "")
            tool = clang.with_name(exe)
            args.append("--" + name + "=" + (tool.as_posix() if tool.exists() else exe))
        subprocess.run(args, cwd=src, check=True)
        config = src / "config.h"
        content = config.read_text()
        # The PS4 stubs export these names, but OpenOrbis has no matching system headers.
        content = re.sub(r"(#define HAVE_(?:SYSCTL|GETAUXVAL)) 1", r"\1 0", content)
        content = re.sub(r'^#define FFMPEG_CONFIGURATION .+$',
                         '#define FFMPEG_CONFIGURATION "NULL_store MP4 H.264 AAC"', content, flags=re.M)
        config.write_text(content)
        libraries = [f"lib{x}/lib{x}.a" for x in LIBS]
        subprocess.run(["make", "-j" + str(min(8, os.cpu_count() or 2)), *libraries], cwd=src, check=True)
        subprocess.run(["make", "install-headers"], cwd=src, check=True)
        (destination / "lib").mkdir(exist_ok=True)
        for lib in libraries:
            shutil.copyfile(src / lib, destination / "lib" / Path(lib).name)
    (destination / "ready").touch()


if __name__ == "__main__":
    build(*sys.argv[1:])
