# =============================================================================
# OpenMoHAA - Nintendo Switch (libnx) toolchain file
#
# Usage:
#   cmake -S . -B build \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/devkita64-libnx.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# This delegates to devkitPro's official Switch toolchain (which configures the
# aarch64-none-elf compilers, the Switch ABI flags, the libnx specs file and the
# CMAKE_FIND_ROOT_PATH for libnx + portlibs) and additionally exposes the
# helper functions nx_generate_nacp() / nx_create_nro() used for packaging.
#
# It only requires the DEVKITPRO environment variable to be set
# (e.g. /opt/devkitpro), which the devkitPro installer configures for you.
# =============================================================================

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR
        "DEVKITPRO is not set in the environment.\n"
        "Install devkitPro (https://devkitpro.org/wiki/Getting_Started) and make "
        "sure DEVKITPRO points at it, e.g. export DEVKITPRO=/opt/devkitpro")
endif()

set(DEVKITPRO "$ENV{DEVKITPRO}")

# The canonical Switch toolchain shipped with devkitPro. It sets:
#   - CMAKE_SYSTEM_NAME / PROCESSOR
#   - the aarch64-none-elf gcc/g++/ar/ranlib/objcopy
#   - the Switch ABI flags (-march=armv8-a+crc+crypto -mtune=cortex-a57 ...)
#   - -D__SWITCH__ , the libnx specs and sysroot
#   - CMAKE_FIND_ROOT_PATH = ${DEVKITPRO}/libnx + ${DEVKITPRO}/portlibs/switch
#   - the packaging helpers (nx_create_nro, nx_generate_nacp, dkp_add_asset_target)
include("${DEVKITPRO}/cmake/Switch.cmake")

# Marker our CMakeLists.txt keys off of.
set(PLATFORM_SWITCH ON CACHE BOOL "Building for the Nintendo Switch" FORCE)

# The static module localization step (ld -r + objcopy --localize-hidden) needs
# these explicitly; some devkitPro versions leave CMAKE_LINKER/OBJCOPY unset.
set(_DKA64_PREFIX "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-")
if(NOT CMAKE_LINKER OR CMAKE_LINKER STREQUAL "")
    set(CMAKE_LINKER "${_DKA64_PREFIX}ld" CACHE FILEPATH "" FORCE)
endif()
if(NOT CMAKE_OBJCOPY OR CMAKE_OBJCOPY STREQUAL "")
    set(CMAKE_OBJCOPY "${_DKA64_PREFIX}objcopy" CACHE FILEPATH "" FORCE)
endif()
if(NOT CMAKE_NM OR CMAKE_NM STREQUAL "")
    set(CMAKE_NM "${_DKA64_PREFIX}nm" CACHE FILEPATH "" FORCE)
endif()

# Make sure portlib headers/libs (SDL2, mesa, codecs, OpenAL) are searchable.
list(APPEND CMAKE_PREFIX_PATH
    "${DEVKITPRO}/portlibs/switch"
    "${DEVKITPRO}/libnx")
