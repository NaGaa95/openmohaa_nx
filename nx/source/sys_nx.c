/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

sys_nx.c

Switch implementation of the engine's platform abstraction. This file takes
the place of code/sys/sys_unix.c for the Switch build (the Switch build does
NOT compile sys_unix.c). It implements every Sys_* entry point the engine
expects, mapped onto libnx/newlib semantics:

  * Writable user data        -> sdmc:/switch/openmohaa     (NX_HOMEPATH)
  * Read-only bundled assets  -> romfs:/                     (via Sys_SteamPath)
  * Time                      -> gettimeofday (libnx GTOD)
  * Randomness                -> libnx CSRNG (NX_RandomBytes)
  * fork()/exec()/dialogs     -> stubbed (no process model on Switch)

Directory scanning, fopen/stat/mkdir all work unchanged because libnx exposes
sdmc: and romfs: through newlib's devoptab layer.
===========================================================================
*/

#define _FILE_OFFSET_BITS 64

#include "q_shared.h"
#include "qcommon.h"
#include "sys_local.h"
#include "nx_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <fenv.h>

qboolean stdinIsATTY = qfalse;

/*
==================
NX_IsReadOnlyPath

romfs: is read-only; never try to create directories or open for write there.
==================
*/
static qboolean NX_IsReadOnlyPath(const char *path)
{
    return path && !Q_strncmp(path, "romfs:", 6) ? qtrue : qfalse;
}

/*
==============================================================

PATHS

==============================================================
*/

/* All three "home" roots collapse onto the single writable SD-card tree. */
char *Sys_DefaultHomeConfigPath(void) { return NX_HOMEPATH; }
char *Sys_DefaultHomeDataPath(void)   { return NX_HOMEPATH; }
char *Sys_DefaultHomeStatePath(void)  { return NX_HOMEPATH; }

/*
================
Sys_SteamPath

Re-purposed on Switch to expose the read-only romfs partition as an extra
search root, so any assets baked into the .nro (romfs:/main, a fallback
config, the controller autoexec, GL2 shaders, ...) are found by the VFS.
================
*/
char *Sys_SteamPath(void) { return "romfs:"; }
char *Sys_GogPath(void) { return ""; }
char *Sys_MicrosoftStorePath(void) { return ""; }

/*
==================
Sys_Cwd

Deterministic, writable base used as a last-resort install path when argv[0]
can't be resolved to a directory.
==================
*/
char *Sys_Cwd(void)
{
    static char cwd[MAX_OSPATH];
    Q_strncpyz(cwd, NX_HOMEPATH, sizeof(cwd));
    return cwd;
}

/*
==================
Sys_BinaryPathRelative
==================
*/
char *Sys_BinaryPathRelative(const char *relative)
{
    static char resolved[MAX_OSPATH];
    Com_sprintf(resolved, sizeof(resolved), "%s/%s", Sys_BinaryPath(), relative);
    return resolved;
}

/*
==================
Sys_Basename

Minimal, allocation-free basename that also copes with the "sdmc:" / "romfs:"
device prefixes (only '/' is treated as a separator).
==================
*/
const char *Sys_Basename(char *path)
{
    static char base[MAX_OSPATH];
    const char *p;
    int len;

    if (!path || !*path) {
        Q_strncpyz(base, ".", sizeof(base));
        return base;
    }

    len = strlen(path);
    // strip trailing slashes
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    p = path + len;
    while (p > path && p[-1] != '/') {
        p--;
    }

    Q_strncpyz(base, p, sizeof(base));
    // remove any trailing slash copied over
    {
        int blen = strlen(base);
        while (blen > 1 && base[blen - 1] == '/') {
            base[--blen] = '\0';
        }
    }
    return base;
}

/*
==================
Sys_Dirname
==================
*/
const char *Sys_Dirname(char *path)
{
    static char dir[MAX_OSPATH];
    int len;

    if (!path || !*path) {
        Q_strncpyz(dir, ".", sizeof(dir));
        return dir;
    }

    Q_strncpyz(dir, path, sizeof(dir));
    len = strlen(dir);

    // strip trailing slashes
    while (len > 1 && dir[len - 1] == '/') {
        dir[--len] = '\0';
    }

    // walk back to the last separator
    while (len > 0 && dir[len - 1] != '/') {
        dir[--len] = '\0';
    }

    // drop the separator itself (unless it's a device root like "sdmc:/")
    while (len > 1 && dir[len - 1] == '/' && dir[len - 2] != ':') {
        dir[--len] = '\0';
    }

    if (!dir[0]) {
        Q_strncpyz(dir, ".", sizeof(dir));
    }
    return dir;
}

/*
==============================================================

TIME / RANDOM / USER

==============================================================
*/

unsigned long sys_timeBase = 0;
int curtime;

int Sys_Milliseconds(void)
{
    struct timeval tp;

    gettimeofday(&tp, NULL);

    if (!sys_timeBase) {
        sys_timeBase = tp.tv_sec;
        return tp.tv_usec / 1000;
    }

    curtime = (tp.tv_sec - sys_timeBase) * 1000 + tp.tv_usec / 1000;
    return curtime;
}

qboolean Sys_RandomBytes(byte *string, int len)
{
    if (len <= 0) {
        return qfalse;
    }
    return NX_RandomBytes((unsigned char *)string, (size_t)len) ? qtrue : qfalse;
}

char *Sys_GetCurrentUser(void)
{
    /* The Switch has no POSIX user accounts. */
    return "player";
}

qboolean Sys_LowPhysicalMemory(void)
{
    return qfalse;
}

/*
==============================================================

FILE / DIRECTORY

==============================================================
*/

FILE *Sys_FOpen(const char *ospath, const char *mode)
{
    struct stat buf;

    if (!stat(ospath, &buf) && S_ISDIR(buf.st_mode)) {
        return NULL;
    }
    return fopen(ospath, mode);
}

qboolean Sys_Mkdir(const char *path)
{
    int result;

    if (NX_IsReadOnlyPath(path)) {
        /* romfs is read-only; pretend success so FS_CreatePath() doesn't error */
        return qtrue;
    }

    result = mkdir(path, 0777);
    if (result != 0) {
        return errno == EEXIST ? qtrue : qfalse;
    }
    return qtrue;
}

FILE *Sys_Mkfifo(const char *ospath)
{
    /* No named pipes on Switch (the com_pipefile feature is unused). */
    (void)ospath;
    return NULL;
}

#define MAX_FOUND_FILES 0x1000

void Sys_ListFilteredFiles(
    const char *basedir, char *subdirs, char *filter, qboolean wantsubs, char **list, int *numfiles)
{
    char           search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
    char           filename[MAX_OSPATH];
    DIR           *fdir;
    struct dirent *d;
    struct stat    st;

    if (*numfiles >= MAX_FOUND_FILES - 1) {
        return;
    }

    if (basedir[0] == '\0') {
        return;
    }

    if (strlen(subdirs)) {
        Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
    } else {
        Com_sprintf(search, sizeof(search), "%s", basedir);
    }

    if ((fdir = opendir(search)) == NULL) {
        return;
    }

    while ((d = readdir(fdir)) != NULL) {
        if (!(Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..")) && Q_stricmp(d->d_name, "cvs")) {
            continue;
        }

        Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
        if (stat(filename, &st) == -1) {
            continue;
        }

        if ((st.st_mode & S_IFDIR) != 0 && wantsubs) {
            if (strlen(subdirs)) {
                Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
            } else {
                Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
            }
            Sys_ListFilteredFiles(basedir, newsubdirs, filter, wantsubs, list, numfiles);
        }

        if (*numfiles >= MAX_FOUND_FILES - 1) {
            break;
        }

        if (strlen(subdirs)) {
            Com_sprintf(filename, sizeof(filename), "%s/%s", subdirs, d->d_name);
        } else {
            Q_strncpyz(filename, d->d_name, sizeof(filename));
        }

        if (!Com_FilterPath(filter, filename, qfalse)) {
            continue;
        }

        list[*numfiles] = CopyString(filename);
        (*numfiles)++;
    }

    closedir(fdir);
}

char **Sys_ListFiles(const char *directory, const char *extension, const char *filter, int *numfiles, qboolean wantsubs)
{
    struct dirent *d;
    DIR           *fdir;
    char           search[MAX_OSPATH];
    int            nfiles;
    char         **listCopy;
    char          *list[MAX_FOUND_FILES];
    int            i;
    struct stat    st;
    char           buffer[64];

    if (directory[0] == '\0') {
        *numfiles = 0;
        return NULL;
    }

    if (!extension) {
        extension = "";
    }

    if (!filter && (extension[0] != '/' || extension[1])) {
        Q_snprintf(buffer, sizeof(buffer), "*%s", extension);
        filter = buffer;
    }

    if (filter) {
        nfiles = 0;
        Sys_ListFilteredFiles(directory, "", filter, wantsubs, list, &nfiles);

        list[nfiles] = NULL;
        *numfiles    = nfiles;
        if (!nfiles) {
            return NULL;
        }

        listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
        for (i = 0; i < nfiles; i++) {
            listCopy[i] = list[i];
        }
        listCopy[i] = NULL;
        return listCopy;
    }

    nfiles = 0;

    if ((fdir = opendir(directory)) == NULL) {
        *numfiles = 0;
        return NULL;
    }

    while ((d = readdir(fdir)) != NULL) {
        Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
        if (stat(search, &st) == -1) {
            continue;
        }

        if (!(Q_stricmp(d->d_name, ".") && Q_stricmp(d->d_name, "..") && Q_stricmp(d->d_name, "cvs"))) {
            continue;
        }

        if ((st.st_mode & S_IFDIR) == 0) {
            continue;
        }

        if (nfiles == MAX_FOUND_FILES - 1) {
            break;
        }

        list[nfiles] = CopyString(d->d_name);
        nfiles++;
    }

    list[nfiles] = NULL;
    closedir(fdir);

    *numfiles = nfiles;
    if (!nfiles) {
        return NULL;
    }

    listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
    for (i = 0; i < nfiles; i++) {
        listCopy[i] = list[i];
    }
    listCopy[i] = NULL;
    return listCopy;
}

void Sys_FreeFileList(char **list)
{
    int i;

    if (!list) {
        return;
    }

    for (i = 0; list[i]; i++) {
        Z_Free(list[i]);
    }

    Z_Free(list);
}

/*
==================
Sys_Sleep
==================
*/
void Sys_Sleep(int msec)
{
    struct timespec req;

    if (msec == 0) {
        return;
    }

    /* There's no console stdin to wait on; never block indefinitely. */
    if (msec < 0) {
        msec = 10;
    }

    req.tv_sec  = msec / 1000;
    req.tv_nsec = (msec % 1000) * 1000000;
    nanosleep(&req, NULL);
}

/*
==============================================================

DIALOGS / ERROR REPORTING

==============================================================
*/

void Sys_ErrorDialog(const char *error)
{
    char        buffer[1024];
    unsigned    size;
    int         f = -1;
    const char *homedatapath = Cvar_VariableString("fs_homedatapath");
    const char *gamedir      = Cvar_VariableString("fs_game");
    const char *fileName     = "crashlog.txt";
    char       *ospath       = FS_BuildOSPath(homedatapath, gamedir, fileName);

    Sys_Print(va("%s\n", error));

    /* Show the message on screen through the libnx error applet. */
    NX_FatalError("OpenMoHAA", error);

    if (FS_CreatePath(homedatapath)) {
        Com_Printf("ERROR: couldn't create path '%s' for crash log.\n", ospath);
        return;
    }

    f = open(ospath, O_CREAT | O_TRUNC | O_WRONLY, 0640);
    if (f == -1) {
        Com_Printf("ERROR: couldn't open %s\n", fileName);
        return;
    }

    while ((size = CON_LogRead(buffer, sizeof(buffer))) > 0) {
        if (write(f, buffer, size) != (int)size) {
            Com_Printf("ERROR: couldn't fully write to %s\n", fileName);
            break;
        }
    }

    close(f);
}

dialogResult_t Sys_Dialog(dialogType_t type, const char *message, const char *title)
{
    /* No interactive dialog system; behave as if the user accepted. */
    Com_DPrintf("[dialog] %s: %s\n", title ? title : "", message ? message : "");

    switch (type) {
    case DT_YES_NO:    return DR_YES;
    case DT_OK_CANCEL: return DR_OK;
    default:           return DR_OK;
    }
}

/*
==============================================================

GL / PLATFORM INIT

==============================================================
*/

void Sys_GLimpSafeInit(void) {}
void Sys_GLimpInit(void) {}

void Sys_SetFloatEnv(void)
{
    fesetround(FE_TONEAREST);
}

void Sys_PlatformInit(void)
{
    Sys_SetFloatEnv();
    stdinIsATTY = qfalse;
    /* No POSIX signal handlers on Switch - faults are fatal at the kernel. */
}

void Sys_PlatformExit(void)
{
    /* Socket/romfs teardown happens in userAppExit(). */
}

void Sys_SetEnv(const char *name, const char *value)
{
    if (value && *value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
}

/*
==============================================================

PROCESS / MISC

==============================================================
*/

int Sys_PID(void)
{
    return 1;
}

qboolean Sys_PIDIsRunning(int pid)
{
    (void)pid;
    return qfalse;
}

qboolean Sys_DllExtension(const char *name)
{
    /* Static-only build: never actually loads DLLs, but keep the predicate
       honest for any code path that checks it. */
    return COM_CompareExtension(name, DLL_EXT) ? qtrue : qfalse;
}

qboolean Sys_OpenFolderInPlatformFileManager(const char *path)
{
    (void)path;
    return qfalse;
}

qboolean Sys_SetMaxFileLimit(void)
{
    /* libnx has a fixed FD table; nothing to raise. */
    return qfalse;
}
