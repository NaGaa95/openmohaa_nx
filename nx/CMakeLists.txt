# =============================================================================
# OpenMoHAA - Nintendo Switch (libnx / devkitA64) port
#
# Self-contained CMake project that builds a single static client .nro from the
# upstream OpenMoHAA sources living in the sibling ../openmohaa tree. Nothing in
# the upstream tree is modified: this project only ADDS the Switch platform
# sources (openmohaa_nx/source), a forced-include platform header, and the
# static module-linking glue.
#
# Configure with the devkitPro Switch toolchain:
#   cmake -S . -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/devkita64-libnx.cmake \
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#
# See README.md for the full dependency list and asset layout.
# =============================================================================

cmake_minimum_required(VERSION 3.18)

# The toolchain file is only processed by project(), so the PLATFORM_SWITCH
# guard must come after it.
project(openmohaa_switch VERSION 0.83.0 LANGUAGES C CXX ASM)

if(NOT PLATFORM_SWITCH)
    message(FATAL_ERROR
        "This project must be configured with the Switch toolchain:\n"
        "  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/devkita64-libnx.cmake")
endif()

# -----------------------------------------------------------------------------
# Paths & options
# -----------------------------------------------------------------------------
# Locate the OpenMoHAA engine tree. Supports both layouts: a sibling
# ../openmohaa checkout, or this folder living inside the engine repo (engine at
# the parent, e.g. <repo>/nx with the code at <repo>/code).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../openmohaa/code/qcommon/q_shared.h")
    set(_NX_DEFAULT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../openmohaa")
else()
    set(_NX_DEFAULT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()
set(OPENMOHAA_ROOT "${_NX_DEFAULT_ROOT}"
    CACHE PATH "Path to the upstream OpenMoHAA checkout")
set(SRC   "${OPENMOHAA_ROOT}/code")
set(NXSRC "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(NXINC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(NOT EXISTS "${SRC}/qcommon/q_shared.h")
    message(FATAL_ERROR "Could not find OpenMoHAA sources at '${SRC}'. "
        "Set -DOPENMOHAA_ROOT=/path/to/openmohaa")
endif()

set(NX_RENDERER "gl1" CACHE STRING "Renderer to statically link: gl1 or gl2")
set_property(CACHE NX_RENDERER PROPERTY STRINGS gl1 gl2)

option(NX_ENABLE_NXLINK "Redirect stdout/stderr to an nxlink host (debugging)" OFF)
option(NX_USE_CODEC_MAD "Enable MP3 playback via libmad" ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LTO is intentionally OFF: the static module localization step uses `ld -r`,
# which does not play nicely with GCC LTO bitcode objects.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# -----------------------------------------------------------------------------
# Generated version header (configure_file replacement for cmake/version.cmake)
# -----------------------------------------------------------------------------
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(GIT_BRANCH_NAME "switch")
set(GIT_REVISION_HASH "0000000000000000000000000000000000000000")
set(GIT_REVISION_HASH_ABBREVIATED "0000000")
string(TIMESTAMP GIT_REVISION_DATE_TIME UTC)
string(TIMESTAMP GIT_REVISION_DATE "%b %d %Y UTC" UTC)
set(GIT_REVISION_BUILD_NUMBER 0)
set(PRODUCT_VERSION_STAGE "switch")
configure_file("${OPENMOHAA_ROOT}/cmake/q_version.generated.h.in"
               "${GENERATED_DIR}/q_version.generated.h")

# -----------------------------------------------------------------------------
# Global compile settings shared by every target
# -----------------------------------------------------------------------------
# Force-include the Switch platform shim so code/qcommon/q_platform.h compiles
# without an upstream edit (it has no __SWITCH__ branch).
add_compile_options(-include "${NXINC}/nx_platform.h")

# Lowest-priority fallback headers (e.g. <ifaddrs.h> for older libnx). Only used
# when the sysroot doesn't already provide them.
add_compile_options(-idirafter "${CMAKE_CURRENT_SOURCE_DIR}/compat")

add_compile_definitions(
    __SWITCH__
    BOTLIB
    APP_MODULE
    _LINUX=1   # GameSpy POSIX backend selector (gsPlatform.h). Safe on Switch:
               # q_platform.h only touches _LINUX inside its __linux__ block,
               # which is never entered here. Many client UI files include the
               # GameSpy headers, so this must be global, not per-file.
)
# NDEBUG / _DEBUG are supplied by the build-type flags.

# The engine relies on a lot of type-punning unions.
add_compile_options(-fno-strict-aliasing -Wno-comment -Wno-format-truncation)

# Global include dirs (mirrors shared_sources.cmake's include_directories()).
include_directories(
    ${SRC}/qcommon
    ${SRC}/script
    ${SRC}/sys
    ${SRC}/gamespy/common
    ${SRC}/gamespy/gcdkey
    ${GENERATED_DIR}
    ${NXINC}
)

# Portlib headers (SDL2, mesa GL/EGL, OpenAL, codecs) live here.
include_directories(SYSTEM "$ENV{DEVKITPRO}/portlibs/switch/include")

# -----------------------------------------------------------------------------
# Third-party libraries (portlibs) + vendored Recast
# -----------------------------------------------------------------------------
# switch-sdl2 does not always ship an SDL2Config.cmake, so fall back to a plain
# library+header lookup when find_package can't produce an SDL2::SDL2 target.
find_package(SDL2 QUIET)
if(NOT TARGET SDL2::SDL2)
    find_library(NX_LIB_SDL2 SDL2 REQUIRED)
    find_path(NX_SDL2_INCLUDE SDL.h PATH_SUFFIXES SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_LINK_LIBRARIES "${NX_LIB_SDL2}"
        INTERFACE_INCLUDE_DIRECTORIES "${NX_SDL2_INCLUDE}")
    set(SDL2_INCLUDE_DIRS "${NX_SDL2_INCLUDE}")
endif()

find_library(NX_LIB_OPENAL   openal)
find_library(NX_LIB_VORBIS   vorbis)
find_library(NX_LIB_VORBISF  vorbisfile)
find_library(NX_LIB_OGG      ogg)
find_library(NX_LIB_OPUS     opus)
find_library(NX_LIB_OPUSFILE opusfile)
find_library(NX_LIB_MAD      mad)
find_library(NX_LIB_Z        z)
# NOTE: jpeg comes from the vendored jpeg-9f (see below), NOT a portlib: the
# renderer compiles with JPEG_INTERNALS and needs jpegint.h, which the portlib
# libjpeg-turbo does not install.

# mesa GL/EGL stack for the Switch (nouveau)
find_library(NX_LIB_EGL      EGL)
find_library(NX_LIB_GLAPI    glapi)
find_library(NX_LIB_DRM_NOUV drm_nouveau)

foreach(req NX_LIB_OPENAL NX_LIB_VORBIS NX_LIB_VORBISF NX_LIB_OGG
            NX_LIB_Z NX_LIB_EGL NX_LIB_GLAPI NX_LIB_DRM_NOUV)
    if(NOT ${req})
        message(FATAL_ERROR "Required portlib not found: ${req}. "
            "Install the matching dkp-pacman package (see README.md).")
    endif()
endforeach()

# Recast navigation (vendored, used by the game module bot AI)
add_subdirectory(${SRC}/thirdparty/recastnavigation/Detour      recastnav_detour)
add_subdirectory(${SRC}/thirdparty/recastnavigation/DetourCrowd recastnav_crowd)
add_subdirectory(${SRC}/thirdparty/recastnavigation/Recast      recastnav_recast)
set_property(TARGET Detour DetourCrowd Recast PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(Detour DetourCrowd Recast PROPERTIES LINKER_LANGUAGE CXX)

# =============================================================================
# Source lists (transcribed from the upstream cmake/*.cmake modules, with the
# desktop/dlopen-only pieces removed)
# =============================================================================

# ---- COMMON_SOURCES (shared_sources.cmake) ----------------------------------
set(COMMON_SOURCES
    ${SRC}/qcommon/alias.c
    ${SRC}/qcommon/bg_compat.cpp
    ${SRC}/qcommon/cm_fencemask.c
    ${SRC}/qcommon/cm_load.c
    ${SRC}/qcommon/cm_patch.c
    ${SRC}/qcommon/cm_polylib.c
    ${SRC}/qcommon/cm_terrain.c
    ${SRC}/qcommon/cm_test.c
    ${SRC}/qcommon/cm_trace.c
    ${SRC}/qcommon/cm_trace_lbd.cpp
    ${SRC}/qcommon/cm_trace_obfuscation.cpp
    ${SRC}/qcommon/cmd.c
    ${SRC}/qcommon/common.c
    ${SRC}/qcommon/crc.c
    ${SRC}/qcommon/cvar.c
    ${SRC}/qcommon/files.cpp
    ${SRC}/qcommon/ioapi.c
    ${SRC}/qcommon/huffman.cpp
    ${SRC}/qcommon/md4.c
    ${SRC}/qcommon/md5.c
    ${SRC}/qcommon/memory.c
    ${SRC}/qcommon/msg.cpp
    ${SRC}/qcommon/net_chan.c
    ${SRC}/qcommon/net_ip.c
    ${SRC}/qcommon/q_math.c
    ${SRC}/qcommon/q_shared.c
    ${SRC}/qcommon/unzip.c
    ${SRC}/gamespy/q_gamespy.c
    # skeletor
    ${SRC}/skeletor/bonetable.cpp
    ${SRC}/skeletor/skeletor_imports.cpp
    ${SRC}/skeletor/skeletor_loadanimation.cpp
    ${SRC}/skeletor/skeletor_model_files.cpp
    ${SRC}/skeletor/skeletor_utilities.cpp
    ${SRC}/skeletor/skeletor.cpp
    ${SRC}/skeletor/skeletorbones.cpp
    ${SRC}/skeletor/tokenizer.cpp
    # tiki
    ${SRC}/tiki/tiki_anim.cpp
    ${SRC}/tiki/tiki_cache.cpp
    ${SRC}/tiki/tiki_commands.cpp
    ${SRC}/tiki/tiki_files.cpp
    ${SRC}/tiki/tiki_frame.cpp
    ${SRC}/tiki/tiki_imports.cpp
    ${SRC}/tiki/tiki_parse.cpp
    ${SRC}/tiki/tiki_skel.cpp
    ${SRC}/tiki/tiki_surface.cpp
    ${SRC}/tiki/tiki_tag.cpp
    ${SRC}/tiki/tiki_utility.cpp
    ${SRC}/corepp/tiki_main.cpp
    ${SRC}/corepp/tiki_script.cpp
)

# net_ip.c references a few BSD bits libnx lacks (struct ipv6_mreq, inet_addr,
# if_nametoindex). Force-include a compat header into just this file.
set_source_files_properties(${SRC}/qcommon/net_ip.c PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/nx_net_compat.h")

# ---- SCRIPT_SYSTEM_SOURCES (shared_script.cmake) ----------------------------
set(SCRIPT_SYSTEM_SOURCES
    ${SRC}/corepp/class.cpp
    ${SRC}/corepp/con_set.cpp
    ${SRC}/corepp/con_timer.cpp
    ${SRC}/corepp/delegate.cpp
    ${SRC}/corepp/lightclass.cpp
    ${SRC}/corepp/listener.cpp
    ${SRC}/corepp/lz77.cpp
    ${SRC}/corepp/mem_blockalloc.cpp
    ${SRC}/corepp/mem_tempalloc.cpp
    ${SRC}/corepp/script.cpp
    ${SRC}/corepp/str.cpp
    ${SRC}/script/scriptexception.cpp
    ${SRC}/script/scriptvariable.cpp
)

# ---- SERVER_SOURCES ---------------------------------------------------------
set(SERVER_SOURCES
    ${SRC}/server/sv_client.c
    ${SRC}/server/sv_ccmds.c
    ${SRC}/server/sv_game.c
    ${SRC}/server/sv_init.c
    ${SRC}/server/sv_main.c
    ${SRC}/server/sv_net_chan.c
    ${SRC}/server/sv_snapshot.c
    ${SRC}/server/sv_snd.c
    ${SRC}/server/sv_world.c
    ${SRC}/gamespy/sv_gamespy.c
    ${SRC}/gamespy/sv_gqueryreporting.c
)

# ---- CLIENT_SOURCES (client.cmake) ------------------------------------------
set(CLIENT_SOURCES
    ${SRC}/client/cl_avi.cpp
    ${SRC}/client/cl_cgame.cpp
    ${SRC}/client/cl_cin.cpp
    ${SRC}/client/cl_consolecmds.cpp
    ${SRC}/client/cl_curl.c
    ${SRC}/client/cl_input.cpp
    ${SRC}/client/cl_instantAction.cpp
    ${SRC}/client/cl_inv.cpp
    ${SRC}/client/cl_invrender.cpp
    ${SRC}/client/cl_keys.cpp
    ${SRC}/client/cl_main.cpp
    ${SRC}/client/cl_net_chan.cpp
    ${SRC}/client/cl_parse.cpp
    ${SRC}/client/cl_scrn.cpp
    ${SRC}/client/cl_ui.cpp
    ${SRC}/client/cl_uibind.cpp
    ${SRC}/client/cl_uidmbox.cpp
    ${SRC}/client/cl_uifilepicker.cpp
    ${SRC}/client/cl_uigamespy.cpp
    ${SRC}/client/cl_uigmbox.cpp
    ${SRC}/client/cl_uilangame.cpp
    ${SRC}/client/cl_uiloadsave.cpp
    ${SRC}/client/cl_uimaprotationsetup.cpp
    ${SRC}/client/cl_uimaprunner.cpp
    ${SRC}/client/cl_uiminicon.cpp
    ${SRC}/client/cl_uimpmappicker.cpp
    ${SRC}/client/cl_uiplayermodelpicker.cpp
    ${SRC}/client/cl_uiradar.cpp
    ${SRC}/client/cl_uiserverlist.cpp
    ${SRC}/client/cl_uisoundpicker.cpp
    ${SRC}/client/cl_uistd.cpp
    ${SRC}/client/cl_uiview3d.cpp
    # libmumblelink.c omitted: needs <sys/mman.h> (absent on libnx) and is only
    # used when USE_MUMBLE is enabled, which it is not on the console build.
    ${SRC}/client/qal.c
    ${SRC}/client/snd_codec_mp3.c
    ${SRC}/client/snd_codec_ogg.c
    ${SRC}/client/snd_codec_opus.c
    ${SRC}/client/snd_codec_wav.c
    ${SRC}/client/snd_codec.c
    ${SRC}/client/snd_dma_new.cpp
    ${SRC}/client/snd_info.cpp
    ${SRC}/client/snd_mem_new.cpp
    ${SRC}/client/snd_miles_new.cpp
    ${SRC}/client/snd_openal_new.cpp
    ${SRC}/client/usignal.cpp
    ${SRC}/sdl/sdl_input.c
    ${SRC}/sdl/sdl_mouse.c
    ${SRC}/gamespy/cl_gamespy.c
)

file(GLOB_RECURSE UI_SOURCES ${SRC}/uilib/*.c ${SRC}/uilib/*.cpp)

# ---- RENDERER (static, GL1 or GL2) ------------------------------------------
set(RENDERER_COMMON_SOURCES
    ${SRC}/renderercommon/tr_font.c
    ${SRC}/renderercommon/tr_image_bmp.c
    ${SRC}/renderercommon/tr_image_jpg.c
    ${SRC}/renderercommon/tr_image_pcx.c
    ${SRC}/renderercommon/tr_image_png.c
    ${SRC}/renderercommon/tr_image_pvr.c
    ${SRC}/renderercommon/tr_image_tga.c
    ${SRC}/renderercommon/tr_noise.c
    ${SRC}/renderercommon/puff.c
    ${SRC}/tiki/tiki_mesh.cpp
    ${SRC}/sdl/sdl_gamma.c
    ${SRC}/sdl/sdl_glimp.c
)

if(NX_RENDERER STREQUAL "gl1")
    file(GLOB RENDERER_BACKEND_SOURCES ${SRC}/renderergl1/*.c ${SRC}/renderergl1/*.cpp)
else()
    file(GLOB RENDERER_BACKEND_SOURCES ${SRC}/renderergl2/*.c ${SRC}/renderergl2/*.cpp)
    message(WARNING
        "NX_RENDERER=gl2: the GL2 renderer embeds GLSL via a build-time "
        "stringify step that is NOT wired up in this project yet. The default "
        "and fully-supported choice is -DNX_RENDERER=gl1. Use GL2 only if you "
        "add the shader stringification (see cmake/utils/stringify_shader.cmake "
        "upstream).")
endif()

# Vendored libjpeg (jpeg-9f) - the renderer uses JPEG_INTERNALS / jpegint.h,
# which the portlib turbojpeg does not provide. jpeg-9f ships jconfig.h and a
# single (non-conflicting) memory manager, so the j*.c glob is safe.
file(GLOB JPEG_SOURCES ${SRC}/thirdparty/jpeg-9f/j*.c)
set_source_files_properties(${JPEG_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

set(RENDERER_SOURCES ${RENDERER_COMMON_SOURCES} ${RENDERER_BACKEND_SOURCES} ${JPEG_SOURCES})

# ---- SYSTEM_SOURCES (Switch flavour) ----------------------------------------
# Reuses upstream con_log.c / sys_main.c / win_bounds.cpp / win_localization.cpp
# and the upstream passive console; everything platform-specific is the NX set.
set(SYSTEM_SOURCES
    ${SRC}/sys/con_log.c
    ${SRC}/sys/con_passive.c
    ${SRC}/sys/sys_main.c
    ${SRC}/sys/win_bounds.cpp
    ${SRC}/sys/win_localization.cpp
    # --- Switch platform layer (openmohaa_nx/source) ---
    ${NXSRC}/nx_app.c
    ${NXSRC}/sys_nx.c
    ${NXSRC}/sys_nx_new.c
    ${NXSRC}/sys_nx_modules.c
    ${NXSRC}/nx_compat.c
    # Guarded global operator new/delete; quarantine swallows the MP double-free
    # (active only when NX_HEAPGUARD=ON)
    ${NXSRC}/nx_heapguard.cpp
    # Real update-checker impl: its HTTP path is #ifdef USE_HTTP (off here), so
    # CheckNewVersion() just returns false. Provides the `updateChecker` global
    # that cl_main.cpp's CL_VerifyUpdate references.
    ${SRC}/sys/sys_update_checker.cpp
)

# =============================================================================
# Static game / cgame modules
#
# Built as static libraries with hidden visibility, then merged into a single
# relocatable object whose internal symbols are localized (only GetGameAPI /
# GetCGameAPI stay global). This mirrors the symbol isolation each .so has on
# desktop and sidesteps all q_shared/q_math/script ODR collisions.
# =============================================================================

# Generate the script lexer/parser (flex/bison) for the game module.
# Several engine sources include the generated headers via the hard-coded
# relative path "../parser/generated/yyParser.hpp", so they must be emitted
# into code/parser/generated/ exactly like upstream basegame.cmake does.
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)
set(PARSER_GEN_DIR "${SRC}/parser/generated")
file(MAKE_DIRECTORY "${PARSER_GEN_DIR}")
flex_target(nx_fgame_lexer
    ${SRC}/parser/lex_source.txt
    ${PARSER_GEN_DIR}/yyLexer.cpp
    DEFINES_FILE ${PARSER_GEN_DIR}/yyLexer.h
    COMPILE_FLAGS "-Cem --nounistd")
bison_target(nx_fgame_parser
    ${SRC}/parser/bison_source.txt
    ${PARSER_GEN_DIR}/yyParser.cpp
    DEFINES_FILE ${PARSER_GEN_DIR}/yyParser.hpp)
add_flex_bison_dependency(nx_fgame_lexer nx_fgame_parser)

# Several nx_game sources #include the generated parser headers by path but are
# not the bison-output object themselves, so CMake won't order the generator
# before them. This target forces flex+bison to run first.
add_custom_target(nx_parser_gen DEPENDS
    ${FLEX_nx_fgame_lexer_OUTPUTS}
    ${BISON_nx_fgame_parser_OUTPUTS})

set(BG_SOURCES
    ${SRC}/fgame/bg_misc.cpp
    ${SRC}/fgame/bg_pmove.cpp
    ${SRC}/fgame/bg_slidemove.cpp
    ${SRC}/fgame/bg_voteoptions.cpp
)

file(GLOB_RECURSE CGAME_SOURCES ${SRC}/cgame/*.c ${SRC}/cgame/*.cpp)
# The game module globs in the whole script/ directory (which already contains
# scriptexception.cpp / scriptvariable.cpp), so it only needs the corepp half
# of the script system added separately - adding SCRIPT_SYSTEM_SOURCES whole
# would list those two files twice.
file(GLOB_RECURSE GAME_SOURCES   ${SRC}/fgame/*.c ${SRC}/fgame/*.cpp
                                 ${SRC}/script/*.c ${SRC}/script/*.cpp)
list(APPEND GAME_SOURCES ${SRC}/parser/parsetree.cpp)

# Per-module copies of the stateless shared code. These appear once in the
# engine too; the localization step keeps the module copies isolated.
set(MODULE_QSHARED
    ${SRC}/qcommon/q_math.c
    ${SRC}/qcommon/q_shared.c
)
set(MODULE_COREPP
    ${SRC}/corepp/class.cpp
    ${SRC}/corepp/con_set.cpp
    ${SRC}/corepp/con_timer.cpp
    ${SRC}/corepp/delegate.cpp
    ${SRC}/corepp/lightclass.cpp
    ${SRC}/corepp/listener.cpp
    ${SRC}/corepp/lz77.cpp
    ${SRC}/corepp/mem_blockalloc.cpp
    ${SRC}/corepp/mem_tempalloc.cpp
    ${SRC}/corepp/script.cpp
    ${SRC}/corepp/str.cpp
)

# ---- game module static lib ----
add_library(nx_game STATIC
    ${GAME_SOURCES}            # fgame/* + script/* (incl. scriptexception/variable)
    ${BG_SOURCES}
    ${MODULE_QSHARED}
    ${MODULE_COREPP}
    ${FLEX_nx_fgame_lexer_OUTPUTS}
    ${BISON_nx_fgame_parser_OUTPUTS}
)
target_compile_definitions(nx_game PRIVATE GAME_DLL WITH_SCRIPT_ENGINE ARCHIVE_SUPPORTED)
target_compile_options(nx_game PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
target_include_directories(nx_game PRIVATE
    ${SRC}/fgame ${SRC}/script ${PARSER_GEN_DIR} ${SRC}/parser)
target_link_libraries(nx_game PRIVATE Detour DetourCrowd Recast)
add_dependencies(nx_game nx_parser_gen)

# ---- cgame module static lib ----
# cgame only globs cgame/*, so it needs the full script system (corepp + the
# two script/ base files) added explicitly.
add_library(nx_cgame STATIC
    ${CGAME_SOURCES}
    ${BG_SOURCES}
    ${MODULE_QSHARED}
    ${MODULE_COREPP}
    ${SRC}/script/scriptexception.cpp
    ${SRC}/script/scriptvariable.cpp
)
target_compile_definitions(nx_cgame PRIVATE CGAME_DLL)
target_compile_options(nx_cgame PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
target_include_directories(nx_cgame PRIVATE ${SRC}/cgame ${SRC}/script)

# ---- isolate each module into a single symbol-renamed relocatable object ----
set(NX_ISOLATE_SH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/nx_isolate_module.sh")
function(nx_localize_module target keep_symbol prefix out_var)
    set(iso "${CMAKE_BINARY_DIR}/${target}_iso.o")
    add_custom_command(
        OUTPUT "${iso}"
        COMMAND bash "${NX_ISOLATE_SH}"
                "${CMAKE_LINKER}" "${CMAKE_OBJCOPY}" "${CMAKE_NM}"
                "$<TARGET_FILE:${target}>" "${iso}" "${keep_symbol}" "${prefix}"
        DEPENDS ${target} "${NX_ISOLATE_SH}"
        COMMENT "Isolating ${target} symbols (prefix ${prefix}, keeping ${keep_symbol})"
        VERBATIM)
    add_custom_target(${target}_local_tgt DEPENDS "${iso}")
    set_source_files_properties("${iso}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    set(${out_var} "${iso}" PARENT_SCOPE)
endfunction()

nx_localize_module(nx_game  GetGameAPI  nxg_ NX_GAME_OBJ)
nx_localize_module(nx_cgame GetCGameAPI nxc_ NX_CGAME_OBJ)

# =============================================================================
# The client executable (.elf) -> packaged into .nro
# =============================================================================
set(CLIENT_BINARY openmohaa)

add_executable(${CLIENT_BINARY}
    ${SERVER_SOURCES}
    ${CLIENT_SOURCES}
    ${UI_SOURCES}
    ${COMMON_SOURCES}
    ${SCRIPT_SYSTEM_SOURCES}
    ${SYSTEM_SOURCES}
    ${RENDERER_SOURCES}
    ${NX_GAME_OBJ}
    ${NX_CGAME_OBJ}
)

add_dependencies(${CLIENT_BINARY} nx_game_local_tgt nx_cgame_local_tgt)

target_include_directories(${CLIENT_BINARY} PRIVATE
    ${SRC}/thirdparty/jpeg-9f             # vendored jpeg (must beat portlib turbojpeg)
    ${SRC}/client
    ${SRC}/renderercommon
    ${SRC}/renderer${NX_RENDERER}         # renderergl1 or renderergl2 (own headers)
    "$ENV{DEVKITPRO}/portlibs/switch/include/opus"   # opusfile.h lives here
    "${SDL2_INCLUDE_DIRS}"
)

target_compile_definitions(${CLIENT_BINARY} PRIVATE
    USE_OPENAL
    USE_CODEC_VORBIS
    USE_CODEC_OPUS
    USE_INTERNAL_JPEG
)
if(NX_USE_CODEC_MAD)
    target_compile_definitions(${CLIENT_BINARY} PRIVATE USE_CODEC_MAD=1)
endif()
if(NX_ENABLE_NXLINK)
    target_compile_definitions(${CLIENT_BINARY} PRIVATE NXLINK_ENABLE)
endif()
# Guarded global operator new/delete. Keep ON: the quarantine swallows the str
# double-free that otherwise crashes when entering a multiplayer game.
option(NX_HEAPGUARD "Guarded operator new/delete (fixes MP double-free)" ON)
if(NX_HEAPGUARD)
    target_compile_definitions(${CLIENT_BINARY} PRIVATE NX_HEAPGUARD)
endif()

# ---- GameSpy SDK static libs (server browser / master server) ----
file(GLOB GSPY_COMMON ${SRC}/gamespy/common/*.c
     ${SRC}/gamespy/darray.c ${SRC}/gamespy/gserver.c ${SRC}/gamespy/gserverlist.c
     ${SRC}/gamespy/hashtable.c ${SRC}/gamespy/md5c.c ${SRC}/gamespy/gutil.c)
file(GLOB GSPY_GCDKEY ${SRC}/gamespy/gcdkey/*.c)
file(GLOB GSPY_NATNEG ${SRC}/gamespy/natneg/*.c)
file(GLOB GSPY_QR2    ${SRC}/gamespy/qr2/*.c)
add_library(gcd_common STATIC ${GSPY_COMMON})
add_library(gcd_key    STATIC ${GSPY_GCDKEY})
add_library(gcd_natneg STATIC ${GSPY_NATNEG})
add_library(gcd_qr2    STATIC ${GSPY_QR2})
foreach(g gcd_common gcd_key gcd_natneg gcd_qr2)
    target_include_directories(${g} PRIVATE ${SRC}/gamespy ${SRC}/gamespy/common ${SRC}/gamespy/gcdkey)
endforeach()
# (_LINUX is defined globally above, so it reaches the GameSpy libs and every
#  client UI translation unit that includes the GameSpy headers.)
target_link_libraries(gcd_key    PUBLIC gcd_common gcd_qr2)
target_link_libraries(gcd_qr2    PUBLIC gcd_common gcd_natneg)
target_link_libraries(gcd_natneg PUBLIC gcd_common)

# ---- Link everything ----
# --start-group/--end-group resolves the circular references between the
# engine objects, the localized game/cgame blobs and the GameSpy libs.
target_link_libraries(${CLIENT_BINARY} PRIVATE
    -Wl,--start-group
        gcd_key gcd_natneg gcd_qr2 gcd_common
        Detour DetourCrowd Recast
    -Wl,--end-group
    SDL2::SDL2
    ${NX_LIB_OPENAL}
    ${NX_LIB_VORBISF} ${NX_LIB_VORBIS} ${NX_LIB_OGG}
    ${NX_LIB_OPUSFILE} ${NX_LIB_OPUS}
    ${NX_LIB_Z}
    ${NX_LIB_EGL} ${NX_LIB_GLAPI} ${NX_LIB_DRM_NOUV}
)
if(NX_USE_CODEC_MAD AND NX_LIB_MAD)
    target_link_libraries(${CLIENT_BINARY} PRIVATE ${NX_LIB_MAD})
endif()

# The .elf is what elf2nro consumes.
set_target_properties(${CLIENT_BINARY} PROPERTIES SUFFIX ".elf")

# =============================================================================
# Packaging: ELF -> NACP -> NRO (+ romfs)
# =============================================================================
set(NX_APP_TITLE   "OpenMoHAA"        CACHE STRING "NACP application title")
set(NX_APP_AUTHOR  "naga"             CACHE STRING "NACP author")
set(NX_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "NACP version string")
set(NX_APP_ICON    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.jpg" CACHE FILEPATH "256x256 JPEG icon")
set(NX_ROMFS_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/romfs"           CACHE PATH "romfs directory to bake in")

if(COMMAND nx_create_nro)
    # IMPORTANT: devkitPro's nx_create_nro() does NOT accept NAME/AUTHOR/VERSION
    # (it only takes ICON/NACP/ROMFS/...), so if we let it auto-generate the
    # NACP it falls back to CMAKE_PROJECT_NAME ("openmohaa_switch") + "Unspecified
    # Author". Generate the NACP ourselves with the real title/author and hand it
    # to nx_create_nro via NACP.
    set(NACP_FILE "${CMAKE_BINARY_DIR}/${CLIENT_BINARY}.nacp")
    nx_generate_nacp(OUTPUT "${NACP_FILE}"
        NAME    "${NX_APP_TITLE}"
        AUTHOR  "${NX_APP_AUTHOR}"
        VERSION "${NX_APP_VERSION}")
    nx_create_nro(${CLIENT_BINARY}
        NACP  "${NACP_FILE}"
        ICON  "${NX_APP_ICON}"
        ROMFS "${NX_ROMFS_DIR}")
else()
    # Fallback: invoke nacptool + elf2nro directly.
    find_program(NX_ELF2NRO  elf2nro  PATHS "$ENV{DEVKITPRO}/tools/bin")
    find_program(NX_NACPTOOL nacptool PATHS "$ENV{DEVKITPRO}/tools/bin")
    if(NOT NX_ELF2NRO OR NOT NX_NACPTOOL)
        message(FATAL_ERROR "elf2nro/nacptool not found and nx_create_nro() unavailable.")
    endif()
    set(NACP_FILE "${CMAKE_BINARY_DIR}/${CLIENT_BINARY}.nacp")
    set(NRO_FILE  "${CMAKE_BINARY_DIR}/${CLIENT_BINARY}.nro")
    add_custom_command(TARGET ${CLIENT_BINARY} POST_BUILD
        COMMAND "${NX_NACPTOOL}" --create "${NX_APP_TITLE}" "${NX_APP_AUTHOR}" "${NX_APP_VERSION}" "${NACP_FILE}"
        COMMAND "${NX_ELF2NRO}" "$<TARGET_FILE:${CLIENT_BINARY}>" "${NRO_FILE}"
                --nacp="${NACP_FILE}" --icon="${NX_APP_ICON}" --romfsdir="${NX_ROMFS_DIR}"
        COMMENT "Packaging ${CLIENT_BINARY}.nro"
        VERBATIM)
endif()

message(STATUS "")
message(STATUS "OpenMoHAA Switch build configured:")
message(STATUS "  Renderer        : ${NX_RENDERER}")
message(STATUS "  OpenMoHAA root  : ${OPENMOHAA_ROOT}")
message(STATUS "  romfs dir       : ${NX_ROMFS_DIR}")
message(STATUS "  MP3 (libmad)    : ${NX_USE_CODEC_MAD}")
message(STATUS "")
