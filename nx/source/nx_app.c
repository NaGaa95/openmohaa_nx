/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

nx_app.c

libnx application bring-up. This is the ONLY translation unit that includes
<switch.h>; everything the rest of the engine needs from libnx is exposed
through the small C API declared in nx_local.h.

libnx calls userAppInit() before main() and userAppExit() after main()
returns (they are weak symbols in the default crt0 path), so we use them to
mount romfs, start the BSD socket service, and optionally redirect stdio to
an nxlink host for debugging.
===========================================================================
*/

#include <switch.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "nx_local.h"

/*
 * Give the game a generous heap. The default hbloader heap is usually the
 * whole free application memory, which is what we want for a full game
 * engine. We still pin a minimum so we never start with a tiny heap when
 * launched as a forwarder/applet.
 */
#define NX_HEAP_MIN (192u * 1024u * 1024u)

/* Larger main-thread stack: the engine's recursive parsers (TIKI / script)
 * and the renderer can use a fair amount. */
size_t __stacksize = 2u * 1024u * 1024u;

static int s_socketsReady = 0;
static int s_romfsReady   = 0;

/*
==================
NX_MakeUserDirs

Make sure the writable directory tree exists on the SD card before the
engine's filesystem starts poking at it.
==================
*/
static void NX_MakeUserDirs(void)
{
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/openmohaa", 0777);
    mkdir("sdmc:/switch/openmohaa/main", 0777);
}

/*
==================
userAppInit

Called by libnx before main().
==================
*/
void userAppInit(void)
{
    Result rc;

    /* Filesystem for romfs:/ (bundled read-only assets). Non-fatal if the
       .nro was built without a romfs partition. */
    rc = romfsInit();
    s_romfsReady = R_SUCCEEDED(rc);

    /* BSD sockets - required before any socket()/getaddrinfo() call made by
       the engine's netcode (qcommon/net_ip.c). */
    rc = socketInitializeDefault();
    s_socketsReady = R_SUCCEEDED(rc);

#ifdef NXLINK_ENABLE
    /* Redirect stdout/stderr to the nxlink host (devkitPro) for debugging. */
    if (s_socketsReady) {
        nxlinkStdio();
    }
#endif

    /* Locale / system services used for region & language queries. */
    setInitialize();
    setsysInitialize();

    /* HID: SDL2 already calls hidInitialize on Switch; nothing to do here. */

    NX_MakeUserDirs();

    /* Boost the CPU through the whole boot/asset-load phase. IN_Frame() takes
       over management once the engine is up (drops to normal at the menu). */
    NX_SetCpuBoost(1);
}

/*
==================
userAppExit

Called by libnx after main() returns.
==================
*/
void userAppExit(void)
{
    setsysExit();
    setExit();

    if (s_socketsReady) {
        socketExit();
        s_socketsReady = 0;
    }
    if (s_romfsReady) {
        romfsExit();
        s_romfsReady = 0;
    }
}

/*
==================
NX_RandomBytes
==================
*/
int NX_RandomBytes(unsigned char *buf, size_t len)
{
    if (!buf || !len) {
        return 0;
    }

    /* csrngGetRandomBytes via the libnx helper - always succeeds. */
    randomGet(buf, len);
    return 1;
}

/*
==================
NX_FatalError

Show a blocking message through the error applet, then return.
==================
*/
void NX_FatalError(const char *title, const char *message)
{
    ErrorApplicationConfig c;

    if (!title) {
        title = "OpenMoHAA";
    }
    if (!message) {
        message = "A fatal error occurred.";
    }

    /* Best effort - if the applet can't be shown we simply continue. */
    if (R_SUCCEEDED(errorApplicationCreate(&c, title, message))) {
        errorApplicationShow(&c);
    }
}

/*
==================
NX_SoftwareKeyboard

Show the on-screen keyboard and read one line of text.
==================
*/
int NX_SoftwareKeyboard(const char *guide, const char *initial, char *out, size_t outLen)
{
    SwkbdConfig kbd;
    Result      rc;

    if (!out || outLen == 0) {
        return 0;
    }
    out[0] = '\0';

    rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        return 0;
    }

    swkbdConfigMakePresetDefault(&kbd);
    if (guide) {
        swkbdConfigSetGuideText(&kbd, guide);
    }
    if (initial) {
        swkbdConfigSetInitialText(&kbd, initial);
    }
    swkbdConfigSetStringLenMax(&kbd, (u32)(outLen - 1));

    rc = swkbdShow(&kbd, out, outLen);
    swkbdClose(&kbd);

    if (R_FAILED(rc)) {
        out[0] = '\0';
        return 0;
    }

    return 1;
}

/*
==================
NX_AppShouldRun
==================
*/
int NX_AppShouldRun(void)
{
    return appletMainLoop() ? 1 : 0;
}

/*
==================
NX_SetCpuBoost

Raise the CPU to its boost clock during CPU-bound phases (boot, level loading).
FastLoad also throttles the GPU to minimum, so this must only be used while
loading - never during gameplay. Tracked so we only hit the service on change.
==================
*/
static int s_cpuBoost = -1;

void NX_SetCpuBoost(int on)
{
    on = on ? 1 : 0;
    if (on == s_cpuBoost) {
        return;
    }
    s_cpuBoost = on;
    appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
}

/*
==================
NX_GetDisplayResolution

Render resolution from the console operation mode: 720p handheld, 1080p docked.
appletGetOperationMode() is authoritative (SDL's desktop mode can be stale), so
the renderer uses this directly instead of r_mode/r_customwidth.
==================
*/
void NX_GetDisplayResolution(int *width, int *height)
{
    int docked = (appletGetOperationMode() == AppletOperationMode_Console);
    if (width) {
        *width = docked ? 1920 : 1280;
    }
    if (height) {
        *height = docked ? 1080 : 720;
    }
}

/*
==================
NX_OperationModeChanged

Returns 1 once when the dock state changes vs the previous call, so the frame
loop can fire a vid_restart to switch between 720p and 1080p on dock/undock.
==================
*/
int NX_OperationModeChanged(void)
{
    static int s_lastMode = -1;
    int        mode       = (int)appletGetOperationMode();

    if (s_lastMode == -1) {
        s_lastMode = mode; /* baseline on first call */
        return 0;
    }
    if (mode != s_lastMode) {
        s_lastMode = mode;
        return 1;
    }
    return 0;
}
