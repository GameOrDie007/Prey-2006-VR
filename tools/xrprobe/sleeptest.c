// How long does Sys_Sleep(1) actually take?
//
// idSessionLocal::Frame's _WIN32 branch paces the whole game by spinning on
// com_ticNumber with Sys_Sleep(1) between polls, and Sys_Sleep is SDL_Delay.
// Tics arrive every USERCMD_MSEC = 1000/refresh ms (11 ms at 90 Hz), so the
// loop only hits the display rate if a 1 ms sleep really is about 1 ms.
// Windows' default timer resolution is 15.6 ms, which would pace the game at
// 1000/16 = 62.5 fps - exactly what this port measures.
//
// win_main.cpp's Sys_Init calls timeBeginPeriod(1). This checks whether that
// actually takes effect here.

#include <stdio.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <windows.h>

static double measure(const char *what) {
    LARGE_INTEGER freq, a, b;
    int i;
    double total;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&a);
    for (i = 0; i < 50; i++) {
        SDL_Delay(1);
    }
    QueryPerformanceCounter(&b);

    total = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)freq.QuadPart;
    printf("%-28s SDL_Delay(1) average = %6.3f ms   -> caps a poll loop at %6.1f fps\n",
           what, total / 50.0, 1000.0 / (total / 50.0));
    return total / 50.0;
}

int main(void) {
    ULONG minRes = 0, maxRes = 0, curRes = 0;
    HMODULE nt;

    SDL_SetMainReady();

    measure("before timeBeginPeriod:");

    timeBeginPeriod(1);
    measure("after timeBeginPeriod(1):");

    // What does the system think the resolution is?
    nt = GetModuleHandleA("ntdll.dll");
    if (nt) {
        LONG (WINAPI *pQuery)(PULONG, PULONG, PULONG) =
            (LONG (WINAPI *)(PULONG, PULONG, PULONG))GetProcAddress(nt, "NtQueryTimerResolution");
        if (pQuery && pQuery(&minRes, &maxRes, &curRes) == 0) {
            printf("\nNtQueryTimerResolution: current = %.4f ms  (best available %.4f ms)\n",
                   curRes / 10000.0, maxRes / 10000.0);
        }
    }

    timeEndPeriod(1);
    return 0;
}
