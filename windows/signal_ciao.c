/*
 *  signal_ciao.c
 *
 *  Tiny helper to send an interrupt to a running Ciao Prolog engine
 *  on Windows.  Opens the named event "CiaoInterrupt_{PID}" that the
 *  engine creates at startup, signals it, and exits.
 *
 *  Usage:  signal_ciao.exe <PID>
 *  Exit:   0 = success, 1 = bad args, 2 = event not found
 *
 *  Compile:  gcc -O2 -o signal_ciao.exe signal_ciao.c
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    char evname[128];
    HANDLE evt;

    if (argc != 2) {
        fprintf(stderr, "usage: signal_ciao <PID>\n");
        return 1;
    }

    snprintf(evname, sizeof(evname), "CiaoInterrupt_%s", argv[1]);

    evt = OpenEventA(EVENT_MODIFY_STATE, FALSE, evname);
    if (!evt) {
        return 2;  /* event not found — engine too old or not running */
    }

    SetEvent(evt);
    CloseHandle(evt);
    return 0;
}
