/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

nx_local.h

Bridge declarations between the libnx-specific translation units
(nx_app.c, which includes <switch.h>) and the rest of the engine.

These helpers are kept in their own unit so that <switch.h> (which pulls
in the full libnx type universe: u8/u16/u32/u64, Result, etc.) never has to
be included alongside the engine's q_shared.h, avoiding type clashes.
===========================================================================
*/

#ifndef NX_LOCAL_H
#define NX_LOCAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Root of the writable user directory on the SD card.
   All config, savegames, screenshots and logs live under here. */
#define NX_HOMEPATH "sdmc:/switch/openmohaa"

/* romfs mount path for read-only bundled assets (fonts, shaders, fallback
   menu data shipped inside the .nro). The actual game paks are expected on
   the SD card under NX_HOMEPATH. */
#define NX_ROMFS "romfs:/"

/* Fill 'buf' with 'len' cryptographically-strong random bytes.
   Returns non-zero on success. Backed by libnx randomGet(). */
int NX_RandomBytes(unsigned char *buf, size_t len);

/* Show a blocking fatal-error message using the libnx error applet.
   Safe to call even if the applet cannot be shown. */
void NX_FatalError(const char *title, const char *message);

/* Bring up the software keyboard so the user can enter a line of text.
   'out' receives at most 'outLen' bytes (NUL-terminated). 'initial' and
   'guide' may be NULL. Returns non-zero if the user confirmed input. */
int NX_SoftwareKeyboard(const char *guide, const char *initial, char *out, size_t outLen);

/* Returns non-zero while the application should keep running. Goes to zero
   when the user requests exit through the HOME menu / applet message. */
int NX_AppShouldRun(void);

/* Toggle the CPU boost clock (libnx appletSetCpuBoostMode). on != 0 raises the
   CPU to its max clock (and throttles the GPU to minimum), which speeds up the
   CPU-bound work during boot and level loading. Must be OFF during gameplay so
   the GPU runs at full clocks. No-ops if the boost state is unchanged. */
void NX_SetCpuBoost(int on);

/* Render resolution for the current console mode: 720p handheld, 1080p docked. */
void NX_GetDisplayResolution(int *width, int *height);

/* Returns 1 once when the dock state changes (poll each frame to vid_restart). */
int NX_OperationModeChanged(void);

#ifdef __cplusplus
}
#endif

#endif /* NX_LOCAL_H */
