/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

nx_platform.h

Forced-include header (passed to the compiler with `-include nx_platform.h`)
so that it is processed at the very top of EVERY translation unit, before the
engine's own headers. This lets us teach code/qcommon/q_platform.h about the
Switch target WITHOUT editing that upstream file: q_platform.h has no
`__SWITCH__` branch, so without this it would hit:

    #ifndef OS_STRING
    #error "Operating system not supported"

By pre-defining the small set of platform macros a q_platform.h "OS block"
would normally provide (all guarded with #ifndef), the catch-all #error
guards are satisfied and the rest of q_platform.h (endianness helpers, byte
swap macros, PLATFORM_STRING, ...) resolves correctly for little-endian
AArch64.
===========================================================================
*/

#ifndef NX_PLATFORM_FORCED_INCLUDE_H
#define NX_PLATFORM_FORCED_INCLUDE_H

#ifdef __SWITCH__

#ifndef OS_STRING
#define OS_STRING "switch"
#endif

#ifndef ID_INLINE
#define ID_INLINE inline
#endif

#ifndef PATH_SEP
#define PATH_SEP '/'
#endif

#ifndef ARCH_STRING
#define ARCH_STRING "arm64"
#endif

/* AArch64 on the Switch runs little-endian. */
#ifndef Q3_LITTLE_ENDIAN
#define Q3_LITTLE_ENDIAN
#endif

/* No runtime dynamic loading on the console; these only feed the (unused on
   Switch) DLL filename/extension helpers, but must be defined to avoid the
   q_platform.h #error guards. */
#ifndef DLL_EXT
#define DLL_EXT ".so"
#endif

#ifndef EXE_EXT
#define EXE_EXT ""
#endif

#endif /* __SWITCH__ */

#endif /* NX_PLATFORM_FORCED_INCLUDE_H */
