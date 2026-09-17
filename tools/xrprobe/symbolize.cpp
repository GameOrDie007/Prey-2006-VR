// Resolve "PreyVR.exe+0xRVA" offsets from a crash.txt against the matching PDB.
//
// The in-process crash handler writes module+offset for every frame, which
// survives even when symbolization inside the dying process does not - and on
// another machine there may be no usable symbol path at all. As long as the
// binary is byte-identical to the one built here, the offsets can be resolved
// afterwards, on this machine, against the PDB sitting next to the build.
//
// build:  tools\build\symbolize.bat
// usage:  symbolize.exe <image.exe|dll> <rva> [rva ...]
//         RVAs may be given as 0x2002da or 2002da.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "dbghelp.lib")

// Arbitrary; the RVAs from the crash report are added to this.
#define FAKE_BASE 0x10000000ULL

int main(int argc, char **argv) {
    HANDLE proc = GetCurrentProcess();
    DWORD64 base;
    char pdbDir[MAX_PATH];
    char *slash;
    int i;

    if (argc < 3) {
        printf("usage: symbolize.exe <image> <rva> [rva ...]\n");
        return 2;
    }

    // Look for the PDB beside the image.
    strncpy(pdbDir, argv[1], sizeof(pdbDir) - 1);
    pdbDir[sizeof(pdbDir) - 1] = 0;
    slash = strrchr(pdbDir, '\\');
    if (!slash) {
        slash = strrchr(pdbDir, '/');
    }
    if (slash) {
        *slash = 0;
    } else {
        strcpy(pdbDir, ".");
    }

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEBUG * 0);

    if (!SymInitialize(proc, pdbDir, FALSE)) {
        printf("SymInitialize failed: %lu\n", (unsigned long)GetLastError());
        return 2;
    }

    base = SymLoadModuleEx(proc, NULL, argv[1], NULL, FAKE_BASE, 0, NULL, 0);
    if (!base) {
        printf("SymLoadModuleEx(%s) failed: %lu\n", argv[1],
               (unsigned long)GetLastError());
        return 2;
    }

    {
        IMAGEHLP_MODULE64 mi;
        memset(&mi, 0, sizeof(mi));
        mi.SizeOfStruct = sizeof(mi);
        if (SymGetModuleInfo64(proc, base, &mi)) {
            printf("image   : %s\n", argv[1]);
            printf("symbols : %s\n\n",
                   mi.SymType == SymPdb ? mi.LoadedPdbName : "** NO PDB LOADED **");
        }
    }

    for (i = 2; i < argc; i++) {
        unsigned long long rva = strtoull(argv[i], NULL, 16);
        DWORD64 addr = base + rva;
        unsigned char symbuf[sizeof(SYMBOL_INFO) + 1024];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
        IMAGEHLP_LINE64 line;
        DWORD64 disp = 0;
        DWORD lineDisp = 0;

        memset(symbuf, 0, sizeof(symbuf));
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 1024 - 1;

        printf("+0x%llx\n", rva);

        if (SymFromAddr(proc, addr, &disp, sym)) {
            printf("    %s + 0x%llx\n", sym->Name, (unsigned long long)disp);

            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
                printf("    %s:%lu\n", line.FileName, (unsigned long)line.LineNumber);
            } else {
                printf("    (no line info)\n");
            }
        } else {
            printf("    unresolved: %lu\n", (unsigned long)GetLastError());
        }

        printf("\n");
    }

    SymCleanup(proc);
    return 0;
}
