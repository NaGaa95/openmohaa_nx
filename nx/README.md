# OpenMoHAA — Nintendo Switch (libnx) port

A homebrew Nintendo Switch port of [OpenMoHAA](https://github.com/openmoh/openmohaa)
(an open-source reimplementation of *Medal of Honor: Allied Assault*), built with
**devkitPro / devkitA64 / libnx** and packaged as a single `.nro`.

This folder (`openmohaa_nx/`) is a **self-contained build harness that sits next
to an unmodified `openmohaa/` checkout**. It does not fork or edit the engine
source: it only *adds* a Switch platform layer, a forced-include compatibility
header, the static module-linking glue, and the CMake/packaging pipeline.

```
Android_Port/Openmohaa/
├── openmohaa/        ← upstream engine sources (unmodified)
└── openmohaa_nx/     ← this port (build system + Switch platform code)
```

---

## 1. How the port works

The Switch has no runtime dynamic linker, fixed POSIX paths, no process model,
and an ARM64 Tegra GPU. OpenMoHAA, like its ioquake3 ancestor, loads three
modules at runtime via `dlopen`/`SDL_LoadObject`. The port closes every one of
those seams and maps the platform layer onto libnx.

| Concern | Desktop behaviour | Switch solution |
|---|---|---|
| **Renderer** (`GetRefAPI`) | loaded as `renderer_opengl*.so` | `USE_RENDERER_DLOPEN` **off** → statically linked (upstream already has this path) |
| **Game module** (`GetGameAPI`) | `game.so` via `dlopen` | compiled into a static lib, symbol-localized, called directly |
| **CGame module** (`GetCGameAPI`) | `cgame.so` via `dlopen` | same as game |
| **Entry / main loop** | `main()` + signals | `main()` reused; libnx init via `userAppInit()`; no signals |
| **Writable paths** | `$HOME`, XDG dirs | `sdmc:/switch/openmohaa` |
| **Read-only assets** | install dir | `romfs:/` (bundled in the `.nro`) |
| **Sockets** | bare BSD / WSAStartup | `socketInitializeDefault()` at startup |
| **Audio** | OpenAL (dlopen) | OpenAL (static, `switch-openal-soft`) |
| **Input** | SDL mouse+kbd+pad | SDL GameController (Joy-Con/Pro) + on-screen keyboard |
| **HTTP / auto-update** | cURL | compiled out |

### Static module linking without symbol collisions

On desktop, `game.so` and `cgame.so` each carry their **own** copies of
`q_shared.c`, `q_math.c`, the script engine and the `bg_*` movement code, plus
their own `Com_Error`/`Com_Printf` forwarders — these only stay separate because
each `.so` is its own address space. Statically linking all of that into one ELF
naively would multiply-define hundreds of symbols (and `g_main.cpp` defines
`Com_Error`/`Com_Printf` unconditionally, so the upstream `CGAME_HARD_LINKED`
trick alone is not enough for the game module).

The headers make this worse: `corepp/class.h` and `listener.h` change the
**layout** of `Class`/`Listener`/`Event` depending on `WITH_SCRIPT_ENGINE` /
`GAME_DLL` / `CGAME_DLL`, so the three copies are genuinely *incompatible* and
must stay isolated — they cannot be deduplicated.

Instead, the port keeps each module **fully self-contained**, exactly like the
`.so`, and then gives every one of its symbols a unique name:

1. `nx_game` / `nx_cgame` are built as static libs.
2. `ld -r --whole-archive` merges each into one relocatable object.
3. `objcopy --redefine-syms` renames *every externally-visible defined symbol*
   (global **and** weak — including the C++ vtable/typeinfo COMDAT symbols) to a
   per-module prefix (`nxg_` / `nxc_`), **except** the entry point
   (`GetGameAPI` / `GetCGameAPI`). Undefined references (libc, libstdc++,
   Recast) and local symbols are left untouched.
4. `source/sys_nx_modules.c` provides `Sys_GetGameAPI`/`Sys_GetCGameAPI` that
   call the linked-in entry points directly.

Renaming (rather than *localizing*) is essential: localizing the weak COMDAT
vtable symbols turns their COMDAT groups local, and the final linker then dedups
them against the engine's identically-named groups and discards sections.
Unique names give each module its own group signatures, so nothing is ever
deduped across the engine and the two modules — exactly what the three separate
`.so` files achieve on desktop. The whole step is `cmake/nx_isolate_module.sh`,
driven by `nx_localize_module` in `CMakeLists.txt`, and isolates ~13.7k game and
~2.2k cgame symbols. Zero changes to the engine source.

---

## 2. Prerequisites

Install **devkitPro** following the
[Getting Started guide](https://devkitpro.org/wiki/Getting_Started), then make
sure `DEVKITPRO` is exported (usually `/opt/devkitpro`).

Install the toolchain and portlib packages with `dkp-pacman` (a.k.a.
`(sudo) dkp-pacman` / `pacman` inside the devkitPro MSYS2 on Windows):

```sh
dkp-pacman -S switch-dev                 # devkitA64 + libnx + elf2nro/nacptool
dkp-pacman -S switch-sdl2                # SDL2 (Joy-Con/Pro as GameController, swkbd, touch)
dkp-pacman -S switch-mesa switch-libdrm_nouveau   # OpenGL / GLES via EGL + nouveau
dkp-pacman -S switch-openal-soft         # audio backend (REQUIRED - the only client audio path)
dkp-pacman -S switch-libvorbis switch-libogg switch-opus switch-opusfile  # audio codecs
dkp-pacman -S switch-libmad              # MP3 (optional; build with -DNX_USE_CODEC_MAD=OFF to skip)
dkp-pacman -S switch-zlib                # pk3 inflate
```

You also need, on the **build host** (these run during the build, not on the
Switch): a host CMake ≥ 3.18, and **flex** + **bison** (used to regenerate the
MoHAA script lexer/parser). On the devkitPro MSYS2 / most Linux distros:
`pacman -S flex bison` or `apt install flex bison`.

> **RecastNavigation and libjpeg are built from the vendored copies** in
> `openmohaa/code/thirdparty/`. (The renderer compiles with `JPEG_INTERNALS`
> and needs `jpegint.h`, which the portlib `switch-libjpeg-turbo` does not ship,
> so the bundled `jpeg-9f` is used instead — no jpeg portlib required.) The
> remaining codecs/zlib/OpenAL come from the portlib packages above.

> ### ✅ Verified
> This builds cleanly end-to-end with **devkitA64 GCC 16.1.0** and the packages
> above, producing a valid `openmohaa.nro` (~13 MB) with the icon, NACP and
> romfs embedded. Runtime behaviour on hardware (renderer context negotiation,
> GameSpy) has not been play-tested.

---

## 3. Building

From this directory:

```sh
export DEVKITPRO=/opt/devkitpro

# Configure + build (Makefile wrapper)
make                 # -> build/openmohaa.nro

# …or drive CMake directly:
cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/devkita64-libnx.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DNX_RENDERER=gl1
cmake --build build -j
```

Useful options:

| Option | Default | Meaning |
|---|---|---|
| `-DNX_RENDERER=gl1\|gl2` | `gl1` | which renderer to statically link (GL1 is the feature-complete MoHAA renderer; GL2 is the GLSL path) |
| `-DNX_USE_CODEC_MAD=OFF` | `ON` | drop MP3 support (smaller `.nro`, no `switch-mad`) |
| `-DNX_ENABLE_NXLINK=ON` | `OFF` | redirect `stdout`/`stderr` to an `nxlink` host for debugging |
| `-DOPENMOHAA_ROOT=…` | `../openmohaa` | path to the upstream checkout |

The result is `build/openmohaa.nro` (icon + NACP + romfs baked in).

---

## 4. Installing & running

1. Copy `build/openmohaa.nro` to your SD card, e.g.
   `sdmc:/switch/openmohaa/openmohaa.nro` (or anywhere hbmenu can see it).
2. Copy the game data from your **legitimately owned** MOHAA install onto the SD
   card next to it:

   ```
   sdmc:/switch/openmohaa/main/pak0.pk3 … pakN.pk3      (Allied Assault, required)
   sdmc:/switch/openmohaa/mainta/…                       (Spearhead, optional)
   sdmc:/switch/openmohaa/maintt/…                       (Breakthrough, optional)
   ```

   `make install SWITCH_SD=/path/to/SD` copies the `.nro` for you.
3. Launch from the homebrew menu.

Path resolution on Switch (see `source/sys_nx.c`):
- `fs_homepath` / config / saves / screenshots → `sdmc:/switch/openmohaa`
- `fs_basepath` → directory the `.nro` was launched from (normally the same)
- `Sys_SteamPath()` is repurposed to expose `romfs:/` so the bundled
  `romfs/main/autoexec.cfg` is found.

---

## 5. Controls (default `romfs/main/autoexec.cfg`)

| Switch input | Action |
|---|---|
| Left stick | move / strafe |
| Right stick | aim / look |
| ZR | fire |
| ZL | alt-fire / iron sights |
| R | next weapon |
| L | sprint |
| A | jump |
| B | crouch |
| X | use / activate |
| Y | reload |
| D-Pad ←/→ | lean |
| D-Pad ↑/↓ | weapon next / prev |
| + (Start) | menu |
| − (Select) | dev console (on-screen keyboard for typing) |

Rebind in-game or edit `sdmc:/switch/openmohaa/main/autoexec.cfg`. Text fields
and the console bring up the Switch software keyboard automatically (handled by
`switch-sdl2`'s `SDL_TEXTINPUT` integration).

---

## 6. Known limitations / what is stubbed

- **Client only** — no dedicated server, launcher, QVMs, or HTTP/auto-updater.
- **Online server browser** (GameSpy master server) may be unreliable; LAN /
  direct-connect by IP is the supported path. Blocking DNS is a risk on libnx.
- **Game assets are not included** — you must supply your own MOHAA paks.
- **Gamma** adjustment is likely unsupported (`r_gammamethod`/hardware gamma).
- **S3TC** texture compression may be unavailable in mesa → textures decompress
  to RGBA (more VRAM). Keep `r_picmip ≥ 1` on memory-heavy maps.
- **Renderer context**: the build defaults to GL1 with auto context negotiation
  (`r_preferOpenGLES -1`). If you get a black screen, try `r_preferOpenGLES 1`
  (force GLES) or rebuild with `-DNX_RENDERER=gl2`.
- Stubbed `Sys_*`: signals, `fork`/`exec`, dialogs, PID, env vars, file-limit,
  backtrace, clipboard falls back to SDL.

---

## 7. What this folder adds (file map)

```
openmohaa_nx/
├── CMakeLists.txt                     self-contained Switch build
├── Makefile                           convenience wrapper
├── cmake/
│   ├── toolchain/devkita64-libnx.cmake  delegates to devkitPro's Switch.cmake (+ ld/objcopy/nm)
│   └── nx_isolate_module.sh           ld -r + objcopy --redefine-syms module isolation
├── include/
│   ├── nx_platform.h                  forced-include: teaches q_platform.h about __SWITCH__
│   └── nx_local.h                     bridge API between libnx and the engine
├── compat/
│   ├── ifaddrs.h                      fallback <ifaddrs.h> (-idirafter; only if libnx lacks it)
│   └── nx_net_compat.h                net_ip.c shims: struct ipv6_mreq, inet_addr, if_nametoindex
├── source/
│   ├── nx_app.c                       libnx bring-up: romfs, sockets, swkbd, error applet, randomGet
│   ├── sys_nx.c                       Switch platform layer (replaces sys_unix.c)
│   ├── sys_nx_new.c                   backtrace/thread stubs (replaces sys_unix_new.c)
│   ├── sys_nx_modules.c               static GetGameAPI/GetCGameAPI shim (replaces sys_main_new.c)
│   └── nx_compat.c                    weak getifaddrs/inet_addr/if_nametoindex fallbacks
├── romfs/main/
│   ├── autoexec.cfg                   default controller bindings + cvars
│   └── README_ASSETS.txt
└── assets/icon.jpg                    256×256 homebrew menu icon
```

Reused unmodified from upstream for the Switch system layer: `con_log.c`,
`con_passive.c`, `sys_main.c`, `win_bounds.cpp`, `win_localization.cpp`.

---

## 8. Troubleshooting the build

- **`Required portlib not found`** — install the matching `dkp-pacman` package
  from §2 and reconfigure.
- **`flex`/`bison` errors** — install them on the host; they regenerate
  `code/parser/*` into `build/generated/parser/`.
- **Duplicate-symbol link errors from game/cgame** — confirm `objcopy` and `ld`
  are the devkitA64 ones (`$DEVKITPRO/devkitA64/bin/aarch64-none-elf-*`); the
  localization step relies on `--localize-hidden`.
- **`nx_create_nro` not found** — older devkitPro; the build falls back to
  invoking `nacptool` + `elf2nro` directly from `$DEVKITPRO/tools/bin`.

*OpenMoHAA is an independent project, not affiliated with or endorsed by
Electronic Arts. Bring your own legally-obtained game data.*
