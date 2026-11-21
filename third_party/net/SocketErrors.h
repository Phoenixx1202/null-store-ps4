#pragma once
#include <cerrno>
#if defined(DUSK_PLATFORM_PS4)
#include <orbis/_types/errors.h>
#endif
namespace dusk::net::socketerrors {
// libkernel sockets report Orbis/FreeBSD errno values. This OpenOrbis toolchain
// ships musl errno macros (EINPROGRESS=115, EAGAIN=11), which are not those
// runtime values. Keep the correction local to sockets, not all libc calls.
#if defined(DUSK_PLATFORM_PS4)
constexpr int again = ORBIS_KERNEL_ERROR_EAGAIN & 0xffff;
constexpr int pending = ORBIS_KERNEL_ERROR_EINPROGRESS & 0xffff;
constexpr int interrupted = ORBIS_KERNEL_ERROR_EINTR & 0xffff;
inline bool wouldBlock(int error) { return error == again; }
#else
constexpr int pending = EINPROGRESS;
constexpr int interrupted = EINTR;
inline bool wouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
#endif
inline bool connecting(int error) { return error == pending || error == interrupted; }
}
