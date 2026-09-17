// Prints a symbolized stack for every thread of a running process.
//
// There is no debugger installed on this machine, and a VR app that blocks
// waiting for frames looks exactly like a deadlock from the outside. This is
// the smallest thing that tells the two apart: suspend each thread, walk it
// with dbghelp against the PDB beside the exe, resume.
//
// Same machinery as the crash handler in sys/win32/win_pcvr.cpp, pointed at
// another process instead of this one.
//
// build:  tools\build\stackdump.bat
// usage:  stackdump.exe <pid | PreyVR.exe>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "dbghelp.lib")

static HANDLE proc = NULL;

static void DescribeAddress(DWORD64 addr, char *out, size_t outSize) {
    IMAGEHLP_MODULE64 mi;
    memset(&mi, 0, sizeof(mi));
    mi.SizeOfStruct = sizeof(mi);

    if (addr && SymGetModuleInfo64(proc, addr, &mi)) {
        _snprintf(out, outSize - 1, "%s+0x%llx", mi.ModuleName,
                  (unsigned long long)(addr - mi.BaseOfImage));
    } else {
        _snprintf(out, outSize - 1, addr ? "?" : "NULL");
    }

    out[outSize - 1] = 0;
}

static void PrintFrame(int n, DWORD64 addr) {
    char           where[512];
    unsigned char  symbuf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO   *sym = (SYMBOL_INFO *)symbuf;
    IMAGEHLP_LINE64 line;
    DWORD64        disp = 0;
    DWORD          lineDisp = 0;

    DescribeAddress(addr, where, sizeof(where));

    memset(symbuf, 0, sizeof(symbuf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 512 - 1;

    if (SymFromAddr(proc, addr, &disp, sym)) {
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);

        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
            const char *leaf = strrchr(line.FileName, '\\');
            printf("  %2d  %s+0x%llx   %s:%lu\n", n, sym->Name,
                   (unsigned long long)disp, leaf ? leaf + 1 : line.FileName,
                   (unsigned long)line.LineNumber);
            return;
        }

        printf("  %2d  %s+0x%llx   [%s]\n", n, sym->Name,
               (unsigned long long)disp, where);
        return;
    }

    printf("  %2d  0x%016llx  [%s]\n", n, (unsigned long long)addr, where);
}

static void WalkThread(DWORD tid) {
    HANDLE       th;
    CONTEXT      ctx;
    STACKFRAME64 frame;
    int          n;

    th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                    FALSE, tid);
    if (!th) {
        printf("\n--- thread %lu: cannot open\n", (unsigned long)tid);
        return;
    }

    if (SuspendThread(th) == (DWORD)-1) {
        printf("\n--- thread %lu: cannot suspend\n", (unsigned long)tid);
        CloseHandle(th);
        return;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;

    if (!GetThreadContext(th, &ctx)) {
        printf("\n--- thread %lu: cannot get context\n", (unsigned long)tid);
        ResumeThread(th);
        CloseHandle(th);
        return;
    }

    printf("\n--- thread %lu\n", (unsigned long)tid);

    memset(&frame, 0, sizeof(frame));
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (n = 0; n < 32; n++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th, &frame, &ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }

        if (frame.AddrPC.Offset == 0) {
            break;
        }

        PrintFrame(n, frame.AddrPC.Offset);
    }

    ResumeThread(th);
    CloseHandle(th);
}

static DWORD FindByName(const char *name) {
    PROCESSENTRY32 pe;
    HANDLE         snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD          found = 0;

    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                found = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return found;
}

int main(int argc, char **argv) {
    DWORD          pid;
    HANDLE         snap;
    THREADENTRY32  te;

    if (argc < 2) {
        printf("usage: stackdump.exe <pid | exename.exe>\n");
        return 2;
    }

    pid = (DWORD)atoi(argv[1]);
    if (pid == 0) {
        pid = FindByName(argv[1]);
    }

    if (pid == 0) {
        printf("no such process: %s\n", argv[1]);
        return 2;
    }

    proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) {
        printf("OpenProcess(%lu) failed: %lu\n", (unsigned long)pid,
               (unsigned long)GetLastError());
        return 2;
    }

    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    if (!SymInitialize(proc, NULL, TRUE)) {
        printf("SymInitialize failed: %lu\n", (unsigned long)GetLastError());
        return 2;
    }

    printf("=== stacks for pid %lu ===\n", (unsigned long)pid);

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        printf("thread snapshot failed\n");
        return 2;
    }

    memset(&te, 0, sizeof(te));
    te.dwSize = sizeof(te);

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                WalkThread(te.th32ThreadID);
            }
        } while (Thread32Next(snap, &te));
    }

    CloseHandle(snap);
    SymCleanup(proc);
    CloseHandle(proc);
    return 0;
}
