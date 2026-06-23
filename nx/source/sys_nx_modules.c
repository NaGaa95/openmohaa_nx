/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

sys_nx_modules.c

Replacement for code/sys/new/sys_main_new.c on the Switch build. Two jobs:

  1. Static module linking. The PC build dlopen()s game/cgame shared objects
     and resolves GetGameAPI/GetCGameAPI at runtime. On the Switch everything
     is one static ELF, so we forward Sys_GetGameAPI/Sys_GetCGameAPI directly
     to the linked-in entry points. The game and cgame translation units are
     compiled into separate relocatable blobs whose internal symbols are
     localized (ld -r + objcopy --localize-hidden), so only GetGameAPI and
     GetCGameAPI remain visible here - exactly mirroring the per-.so symbol
     isolation of the desktop build.

  2. The grab-bag of small platform helpers that sys_main_new.c provided
     (clipboard, render-thread stubs, legacy registry no-ops, Sys_InitEx).
     The HTTP/cURL and auto-update-checker bits are intentionally dropped
     (USE_HTTP is off for the console build).
===========================================================================
*/

#include "q_shared.h"
#include "qcommon.h"
#include "sys_local.h"
#include "win_localization.h"

#ifndef DEDICATED
#include <SDL.h>
#endif

/*
 * The real entry points are exported with extern "C" + default visibility
 * from g_main.cpp (GetGameAPI) and cg_main.c (GetCGameAPI). We deliberately
 * declare them with void* signatures here - identical to how sys_main_new.c
 * called them through a function pointer - which is ABI-compatible on AArch64
 * (pointer in x0, pointer out x0) and avoids pulling the large game/cgame
 * headers into this file.
 */
extern void *GetGameAPI(void *import);
extern void *GetCGameAPI(void);

/* Holds the most recent clipboard pull so it can be freed on the next call. */
static void *clipboard_text = NULL;

/*
==============================================================

RENDER-THREAD STUBS (single-threaded renderer on Switch)

==============================================================
*/

qboolean GLimp_SpawnRenderThread(void (*function)(void))
{
    (void)function;
    return qfalse;
}

void *GLimp_RendererSleep(void)
{
    return NULL;
}

void GLimp_FrontEndSleep(void)
{
}

void GLimp_WakeRenderer(void *data)
{
    (void)data;
}

/*
==============================================================

LEGACY / NO-OP PLATFORM HOOKS

==============================================================
*/

void Sys_ShowConsole(int visLevel, qboolean quitOnClose)
{
    (void)visLevel;
    (void)quitOnClose;
}

qboolean SaveRegistryInfo(qboolean user, const char *pszName, void *pvBuf, long lSize)
{
    (void)user; (void)pszName; (void)pvBuf; (void)lSize;
    return qfalse;
}

qboolean LoadRegistryInfo(qboolean user, const char *pszName, void *pvBuf, long *plSize)
{
    (void)user; (void)pszName; (void)pvBuf; (void)plSize;
    return qfalse;
}

qboolean IsFirstRun(void)   { return qfalse; }
qboolean IsNewConfig(void)  { return qfalse; }
qboolean IsSafeMode(void)   { return qfalse; }
void     ClearNewConfigFlag(void) {}
void     RecoverLostAutodialData(void) {}
void     Sys_CloseMutex(void) {}

/*
==============================================================

CLIPBOARD (via SDL)

==============================================================
*/

const char *Sys_GetWholeClipboard(void)
{
#ifndef DEDICATED
    char *data = NULL;
    char *cliptext;

    if ((cliptext = SDL_GetClipboardText()) != NULL) {
        if (cliptext[0] != 0) {
            size_t bufsize = Q_min(strlen(cliptext) + 1, 4096);

            if (clipboard_text != NULL) {
                Z_Free(clipboard_text);
                clipboard_text = NULL;
            }

            data = clipboard_text = Z_Malloc(bufsize);
            Q_strncpyz(data, cliptext, bufsize);
        }
        SDL_free(cliptext);
    }
    return data;
#else
    return NULL;
#endif
}

void Sys_SetClipboard(const char *contents)
{
#ifndef DEDICATED
    if (!contents || !contents[0]) {
        return;
    }
    SDL_SetClipboardText(contents);
#endif
}

/*
==============================================================

STATIC GAME / CGAME MODULE BINDING

==============================================================
*/

void Sys_UnloadGame(void)
{
    /* Nothing to unload - the game module is part of the executable. */
}

void *Sys_GetGameAPI(void *parms)
{
    Com_Printf("Sys_GetGameAPI: binding statically-linked game module...\n");
    return GetGameAPI(parms);
}

void Sys_UnloadCGame(void)
{
    /* Nothing to unload - the cgame module is part of the executable. */
}

void *Sys_GetCGameAPI(void *parms)
{
    /* GetCGameAPI() takes no argument; the import table is delivered later
       through clientGameExport_t::CG_Init. parms is intentionally ignored,
       matching the desktop loader's behaviour. */
    (void)parms;
    Com_Printf("Sys_GetCGameAPI: binding statically-linked cgame module...\n");
    return GetCGameAPI();
}

/* The engine calls these around fatal teardown; nothing to do statically. */
void VM_Forced_Unload_Start(void) {}
void VM_Forced_Unload_Done(void) {}

/*
==============================================================

ENGINE INIT/SHUTDOWN HOOKS

==============================================================
*/

void Sys_InitEx(void)
{
    /* Localization only - no cURL, no update checker on the console build. */
    Sys_InitLocalization();
}

void Sys_ShutdownEx(void)
{
    Sys_ShutLocalization();
}

void Sys_ProcessBackgroundTasks(void)
{
    /* No background HTTP/update tasks on Switch. */
}
