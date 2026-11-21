#!/bin/sh
# PkgTool.Core for CI runners whose OpenSSL is too new for it.
#
# The OpenOrbis PkgTool is a .NET Core 3.1 program and loads OpenSSL 1.1,
# which recent Ubuntu releases no longer ship. This wrapper runs it in an
# Ubuntu 20.04 container (OpenSSL 1.1 from its signed archive) with the
# repository and the toolchain mounted at their own paths, as the user
# running the build. Use it in place of the tool:
#
#   make test-package PKGTOOL="$PWD/tools/ci/pkgtool.sh"
set -eu

: "${OO_PS4_TOOLCHAIN:?OO_PS4_TOOLCHAIN must point to the OpenOrbis toolchain}"
image=nullstore-pkgtool:20.04

if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build -q -t "$image" - >/dev/null <<'EOF'
FROM ubuntu:20.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libssl1.1 libicu66 zlib1g \
 && rm -rf /var/lib/apt/lists/*
EOF
fi

exec docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 \
    -v "$OO_PS4_TOOLCHAIN:$OO_PS4_TOOLCHAIN:ro" \
    -v "$PWD:$PWD" \
    -w "$PWD" \
    "$image" "$OO_PS4_TOOLCHAIN/bin/linux/PkgTool.Core" "$@"
