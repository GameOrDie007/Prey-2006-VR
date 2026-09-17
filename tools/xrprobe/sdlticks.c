// Does SDL_GetTicks() advance without SDL_Init()?
//
// Sys_Milliseconds() is SDL_GetTicks(), and the whole engine tic clock hangs
// off it: Sys_AsyncThread compares (Sys_Milliseconds() >> 4) against a latched
// value to decide how many times to call common->Async(), which is the only
// thing that increments com_ticNumber. idSessionLocal::Frame then spins until
// com_ticNumber advances. If this returns a constant, the game freezes the
// moment Common::Init finishes - which is exactly the symptom.
//
// On Android SDL is initialised by SDL's Java activity before native main runs.
// This build enters through its own main() and the only SDL_Init in the tree is
// in SDL_win32_main.c, which is not in the build.

#include <stdio.h>

// SDL.h renames main to SDL_main unless told not to; this probe has its own.
#define SDL_MAIN_HANDLED
#include <SDL.h>

int main(void) {
    Uint32 a, b, c;

    a = SDL_GetTicks();
    SDL_Delay(200);
    b = SDL_GetTicks();
    SDL_Delay(200);
    c = SDL_GetTicks();

    printf("without SDL_Init : %u %u %u  -> %s\n", a, b, c,
           (c > a) ? "ADVANCES" : "**CONSTANT**");

    printf("SDL_WasInit(TIMER) before init: 0x%x\n", SDL_WasInit(SDL_INIT_TIMER));

    if (SDL_Init(SDL_INIT_TIMER) < 0) {
        printf("SDL_Init(TIMER) failed: %s\n", SDL_GetError());
        return 1;
    }

    a = SDL_GetTicks();
    SDL_Delay(200);
    b = SDL_GetTicks();
    SDL_Delay(200);
    c = SDL_GetTicks();

    printf("with SDL_Init    : %u %u %u  -> %s\n", a, b, c,
           (c > a) ? "ADVANCES" : "**CONSTANT**");

    return 0;
}
