// Verifies that every entry point gl_loader.cpp provides actually resolves on
// this driver, with a real GL context current. Cheaper than finding out from a
// null-pointer crash inside the renderer.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include "gl_loader.h"

int main(void) {
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "pcvrglload";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "pcvrglload", "t", WS_OVERLAPPEDWINDOW,
                                0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);
    HGLRC rc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, rc);

    printf("GL_VERSION: %s\n\n", (const char *)glGetString(GL_VERSION));

    bool ok = PCVR_GL_Init();
    printf("\nPCVR_GL_Init: %s\n", ok ? "all 20 entry points resolved"
                                      : "**INCOMPLETE, see above**");

    // Prove one of them is genuinely callable, not merely non-null. Drain the
    // error queue first so the check reports this call's own result.
    while (glGetError() != GL_NO_ERROR) {}
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    GLenum err = glGetError();
    printf("glGenFramebuffers through the loader -> id %u, GL error 0x%04X : %s\n",
           fbo, err, (fbo != 0 && err == GL_NO_ERROR) ? "works" : "**failed**");

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    return ok ? 0 : 1;
}
