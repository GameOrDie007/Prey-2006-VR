// Is GL_OVR_multiview_multisampled_render_to_texture actually supported here,
// and does wglGetProcAddress hand back a pointer for it regardless?
//
// ovrFramebuffer_Create takes its MSAA path on the strength of a NULL check:
//
//     if (multisamples > 1 && glFramebufferTextureMultisampleMultiviewOVR != NULL)
//
// On Android that is a fair test - eglGetProcAddress returns NULL for an
// extension the driver does not have. On Windows, wglGetProcAddress routinely
// returns a valid-looking pointer for any entry point the driver has a symbol
// for, whether or not the extension is exposed. Calling one of those is how an
// app ends up failing inside nvoglv64.dll, which is what this port does with
// vr_msaa 4 and does not with vr_msaa 0.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <GL/gl.h>

typedef const GLubyte *(APIENTRY *PFNGLGETSTRINGIPROC)(GLenum, GLuint);
#define GL_NUM_EXTENSIONS 0x821D

static int hasExtension(const char *want) {
    PFNGLGETSTRINGIPROC glGetStringi =
        (PFNGLGETSTRINGIPROC)wglGetProcAddress("glGetStringi");
    GLint n = 0, i;

    if (glGetStringi) {
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (i = 0; i < n; i++) {
            const GLubyte *e = glGetStringi(0x1F03 /*GL_EXTENSIONS*/, (GLuint)i);
            if (e && strcmp((const char *)e, want) == 0) {
                return 1;
            }
        }
        return 0;
    }

    {
        const char *ext = (const char *)glGetString(0x1F03);
        return (ext && strstr(ext, want)) ? 1 : 0;
    }
}

static void report(const char *name) {
    PROC p = wglGetProcAddress(name);
    const char *verdict;

    if (p == 0) {
        verdict = "NULL - correctly refused";
    } else if (p == (PROC)0x1 || p == (PROC)0x2 || p == (PROC)0x3 || p == (PROC)-1) {
        verdict = "wgl error sentinel";
    } else {
        verdict = "** NON-NULL **";
    }

    printf("  %-52s %p  %s\n", name, (void *)p, verdict);
}

int main(void) {
    WNDCLASSA wc = { 0 };
    HWND hwnd;
    HDC hdc;
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    int pf;
    HGLRC rc;

    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "pcvrmv";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(0, "pcvrmv", "t", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                           NULL, NULL, wc.hInstance, NULL);
    hdc = GetDC(hwnd);

    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);
    rc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, rc);

    printf("GL_RENDERER : %s\n", (const char *)glGetString(0x1F01));
    printf("GL_VERSION  : %s\n\n", (const char *)glGetString(0x1F02));

    printf("extension advertised?\n");
    printf("  %-52s %s\n", "GL_OVR_multiview",
           hasExtension("GL_OVR_multiview") ? "yes" : "NO");
    printf("  %-52s %s\n", "GL_OVR_multiview2",
           hasExtension("GL_OVR_multiview2") ? "yes" : "NO");
    printf("  %-52s %s\n\n", "GL_OVR_multiview_multisampled_render_to_texture",
           hasExtension("GL_OVR_multiview_multisampled_render_to_texture") ? "yes" : "NO");

    printf("wglGetProcAddress says:\n");
    report("glFramebufferTextureMultiviewOVR");
    report("glFramebufferTextureMultisampleMultiviewOVR");
    report("glRenderbufferStorageMultisampleEXT");
    report("glFramebufferTexture2DMultisampleEXT");

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    return 0;
}
