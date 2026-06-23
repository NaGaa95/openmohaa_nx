/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

sys_nx_new.c

Replacement for code/sys/new/sys_unix_new.c. The Unix version pulls in
<execinfo.h> (backtrace/backtrace_symbols), which devkitA64/newlib does not
provide, so the Switch build compiles this stubbed version instead.
===========================================================================
*/

#include "sys_local.h"

#include <stdio.h>

void Sys_PlatformInit_New(void)
{
}

void Sys_PrepareBackTrace(void)
{
}

void Sys_PrintBackTrace(void)
{
    /* No symbolized backtrace facility on Switch. */
    fprintf(stderr, "(backtrace unavailable on Switch)\n");
}

void Sys_DebugPrint(const char *message)
{
    fputs(message, stderr);
}

void Sys_PumpMessageLoop(void)
{
}

void SetNormalThreadPriority(void)
{
}

void SetBelowNormalThreadPriority(void)
{
}
