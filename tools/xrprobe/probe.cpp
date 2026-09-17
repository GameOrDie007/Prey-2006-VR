// PreyVR -> PCVR feasibility probe.
//
// Answers the three questions Milestone 0 left open, without a headset for
// part 1 and with one streaming for part 2:
//
//   1. Does this desktop GL driver expose GL_OVR_multiview2 in a real context?
//   2. Will it compile PreyVR's shaders verbatim (#version 300 es + multiview),
//      or does every one of the 17 need a desktop dialect?
//   3. Will VDXR hand out a 2-layer array swapchain for OpenGL, and can
//      glFramebufferTextureMultiviewOVR bind it into a complete FBO?
//
// Deliberately uses PreyVR's own bundled OpenXR headers (1.1.38) rather than a
// newer SDK, because those are what the port will compile against.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>          // openxr_platform.h's D3D sections need IUnknown
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>

#define _CRT_SECURE_NO_WARNINGS
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include "openxr.h"
#include "openxr_platform.h"

// ---------------------------------------------------------------- GL decls
// opengl32.lib only exports GL 1.1, so everything past that is a proc address.

#define GL_NUM_EXTENSIONS                 0x821D
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_TEXTURE_2D_ARRAY               0x8C1A
#define GL_FRAMEBUFFER                    0x8D40
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#define GL_SRGB8_ALPHA8                   0x8C43
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_MAX_VIEWS_OVR                  0x9631
#define GL_TEXTURE_DEPTH_                 0x8071

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr_;

typedef const GLubyte* (APIENTRY *PFNGLGETSTRINGIPROC)(GLenum, GLuint);
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum);
typedef void   (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint);
typedef void   (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRY *PFNGLDELETESHADERPROC)(GLuint);
typedef void   (APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei, GLuint*);
typedef void   (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum, GLuint);
typedef GLenum (APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void   (APIENTRY *PFNGLTEXIMAGE3DPROC)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void   (APIENTRY *PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);

static PFNGLGETSTRINGIPROC                        pglGetStringi;
static PFNGLCREATESHADERPROC                      pglCreateShader;
static PFNGLSHADERSOURCEPROC                      pglShaderSource;
static PFNGLCOMPILESHADERPROC                     pglCompileShader;
static PFNGLGETSHADERIVPROC                       pglGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC                  pglGetShaderInfoLog;
static PFNGLDELETESHADERPROC                      pglDeleteShader;
static PFNGLGENFRAMEBUFFERSPROC                   pglGenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC                   pglBindFramebuffer;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC            pglCheckFramebufferStatus;
static PFNGLTEXIMAGE3DPROC                        pglTexImage3D;
static PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC    pglFramebufferTextureMultiviewOVR;

static void* GLProc(const char* n) {
    void* p = (void*)wglGetProcAddress(n);
    if (!p) {
        HMODULE m = GetModuleHandleA("opengl32.dll");
        if (m) p = (void*)GetProcAddress(m, n);
    }
    return p;
}

// ------------------------------------------------- PreyVR's actual shader
// Copied byte for byte out of
// neo/renderer/glsl/interactionShaderVP.cpp. If this compiles, the other 16
// are the same dialect and the same multiview construct.

static const char* const interactionShaderVP = R"(
#version 300 es

// Multiview
#define NUM_VIEWS 2
#extension GL_OVR_multiview2 : enable
layout(num_views=NUM_VIEWS) in;

precision highp float;

// In
in highp vec4 attr_Vertex;
in lowp vec4 attr_Color;
in vec4 attr_TexCoord;
in vec3 attr_Tangent;
in vec3 attr_Bitangent;
in vec3 attr_Normal;

// Uniforms
layout(shared) uniform ViewMatrices
{
    uniform highp mat4 u_viewMatrices[NUM_VIEWS];
};
layout(shared) uniform ProjectionMatrix
{
    uniform highp mat4 u_projectionMatrix;
};
uniform highp mat4 u_modelMatrix;
uniform mat4 u_lightProjection;
uniform lowp float u_colorModulate;
uniform lowp float u_colorAdd;
uniform vec4 u_lightOrigin;
uniform vec4 u_viewOrigin;
uniform vec4 u_bumpMatrixS;
uniform vec4 u_bumpMatrixT;
uniform vec4 u_diffuseMatrixS;
uniform vec4 u_diffuseMatrixT;
uniform vec4 u_specularMatrixS;
uniform vec4 u_specularMatrixT;

// Out
// gl_Position
out vec2 var_TexDiffuse;
out vec2 var_TexNormal;
out vec2 var_TexSpecular;
out vec4 var_TexLight;
out vec3 var_Normal;
out lowp vec4 var_Color;
out vec3 var_L;
out vec3 var_V;
out vec3 var_H;

void main()
{
  mat3 M = mat3(attr_Tangent, attr_Bitangent, attr_Normal);

  var_TexNormal.x = dot(u_bumpMatrixS, attr_TexCoord);
  var_TexNormal.y = dot(u_bumpMatrixT, attr_TexCoord);

  var_TexDiffuse.x = dot(u_diffuseMatrixS, attr_TexCoord);
  var_TexDiffuse.y = dot(u_diffuseMatrixT, attr_TexCoord);

  var_TexSpecular.x = dot(u_specularMatrixS, attr_TexCoord);
  var_TexSpecular.y = dot(u_specularMatrixT, attr_TexCoord);

  var_TexLight.x = dot(u_lightProjection[0], attr_Vertex);
  var_TexLight.y = dot(u_lightProjection[1], attr_Vertex);
  var_TexLight.z = dot(u_lightProjection[2], attr_Vertex);
  var_TexLight.w = dot(u_lightProjection[3], attr_Vertex);

  vec3 L = u_lightOrigin.xyz - attr_Vertex.xyz;
  vec3 V = u_viewOrigin.xyz - attr_Vertex.xyz;
  vec3 H = normalize(L) + normalize(V);

  var_L = L * M;
  var_V = V * M;
  var_H = H * M;

  var_Normal = attr_Normal * M;

  if (u_colorModulate == 0.0) {
    var_Color = vec4(u_colorAdd);
  } else {
    var_Color = (attr_Color * u_colorModulate) + vec4(u_colorAdd);
  }

  gl_Position = u_projectionMatrix * (u_viewMatrices[gl_ViewID_OVR] * (u_modelMatrix * attr_Vertex));
}
)";

// A minimal desktop-dialect multiview shader, to separate "the ES dialect was
// rejected" from "multiview was rejected". Only one of those means work.
static const char* const desktopMultiviewVP = R"(#version 330 core
#extension GL_OVR_multiview2 : require
layout(num_views=2) in;
in vec4 attr_Vertex;
uniform mat4 u_viewMatrices[2];
uniform mat4 u_projectionMatrix;
void main() {
  gl_Position = u_projectionMatrix * (u_viewMatrices[gl_ViewID_OVR] * attr_Vertex);
}
)";

// Same shader, ES dialect, no multiview - separates dialect from extension.
static const char* const esPlainVP = R"(#version 300 es
precision highp float;
in vec4 attr_Vertex;
uniform mat4 u_mvp;
void main() { gl_Position = u_mvp * attr_Vertex; }
)";

static bool CompileVS(const char* src, const char* label) {
    GLuint s = pglCreateShader(GL_VERTEX_SHADER);
    pglShaderSource(s, 1, &src, NULL);
    pglCompileShader(s);
    GLint ok = 0;
    pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    GLint loglen = 0;
    pglGetShaderiv(s, GL_INFO_LOG_LENGTH, &loglen);
    printf("  %-42s : %s\n", label, ok ? "COMPILED" : "**FAILED**");
    if (loglen > 1) {
        char* log = (char*)malloc(loglen + 1);
        pglGetShaderInfoLog(s, loglen, NULL, log);
        printf("      driver log: ");
        for (char* p = log; *p; ++p) {
            putchar(*p);
            if (*p == '\n' && *(p + 1)) printf("                  ");
        }
        if (loglen && log[loglen - 2] != '\n') putchar('\n');
        free(log);
    }
    pglDeleteShader(s);
    return ok != 0;
}

// ------------------------------------------------------------- OpenXR decls
static HMODULE g_loader;
#define XRLOAD(n) PFN_##n n = (PFN_##n)GetProcAddress(g_loader, #n); \
    if (!n) { printf("  !! loader is missing export %s\n", #n); return 1; }

static void PrintVer(const char* label, XrVersion v) {
    printf("%s%u.%u.%u", label, (unsigned)XR_VERSION_MAJOR(v),
           (unsigned)XR_VERSION_MINOR(v), (unsigned)XR_VERSION_PATCH(v));
}

int main(void) {
    printf("================================================================\n");
    printf(" PreyVR -> PCVR probe   (headers: PreyVR's own, ");
    PrintVer("", XR_CURRENT_API_VERSION);
    printf(")\n");
    printf("================================================================\n\n");

    // ---------------------------------------------------------- GL context
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "preyvrprobe";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "preyvrprobe", "probe", WS_OVERLAPPEDWINDOW,
                                0, 0, 320, 240, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { printf("FATAL: CreateWindow failed (%lu)\n", GetLastError()); return 1; }
    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        printf("FATAL: SetPixelFormat failed (%lu)\n", GetLastError()); return 1;
    }
    HGLRC hglrc = wglCreateContext(hdc);
    if (!hglrc || !wglMakeCurrent(hdc, hglrc)) {
        printf("FATAL: wglCreateContext/MakeCurrent failed (%lu)\n", GetLastError()); return 1;
    }

    printf("--- PART 1: desktop GL (no headset needed) ---\n\n");
    printf("  GL_VENDOR   : %s\n", (const char*)glGetString(GL_VENDOR));
    printf("  GL_RENDERER : %s\n", (const char*)glGetString(GL_RENDERER));
    printf("  GL_VERSION  : %s\n", (const char*)glGetString(GL_VERSION));

    pglGetStringi   = (PFNGLGETSTRINGIPROC)GLProc("glGetStringi");
    pglCreateShader = (PFNGLCREATESHADERPROC)GLProc("glCreateShader");
    pglShaderSource = (PFNGLSHADERSOURCEPROC)GLProc("glShaderSource");
    pglCompileShader = (PFNGLCOMPILESHADERPROC)GLProc("glCompileShader");
    pglGetShaderiv  = (PFNGLGETSHADERIVPROC)GLProc("glGetShaderiv");
    pglGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)GLProc("glGetShaderInfoLog");
    pglDeleteShader = (PFNGLDELETESHADERPROC)GLProc("glDeleteShader");
    pglGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)GLProc("glGenFramebuffers");
    pglBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)GLProc("glBindFramebuffer");
    pglCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)GLProc("glCheckFramebufferStatus");
    pglTexImage3D   = (PFNGLTEXIMAGE3DPROC)GLProc("glTexImage3D");
    pglFramebufferTextureMultiviewOVR =
        (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)GLProc("glFramebufferTextureMultiviewOVR");

    const char* slv = pglGetStringi ? (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) : NULL;
    printf("  GLSL        : %s\n\n", slv ? slv : "(unavailable)");

    // extensions
    bool ext_mv = false, ext_mv2 = false, ext_es3 = false, ext_mvmsaa = false;
    GLint next = 0;
    if (pglGetStringi) {
        glGetIntegerv(GL_NUM_EXTENSIONS, &next);
        for (GLint i = 0; i < next; i++) {
            const char* e = (const char*)pglGetStringi(GL_EXTENSIONS, i);
            if (!e) continue;
            if (!strcmp(e, "GL_OVR_multiview"))  ext_mv = true;
            if (!strcmp(e, "GL_OVR_multiview2")) ext_mv2 = true;
            if (!strcmp(e, "GL_ARB_ES3_compatibility")) ext_es3 = true;
            if (!strcmp(e, "GL_OVR_multiview_multisampled_render_to_texture")) ext_mvmsaa = true;
        }
    }
    printf("  %u extensions advertised\n", (unsigned)next);
    printf("    [%s] GL_OVR_multiview\n",          ext_mv  ? "YES" : " - ");
    printf("    [%s] GL_OVR_multiview2   <-- the one the shaders need\n", ext_mv2 ? "YES" : " - ");
    printf("    [%s] GL_ARB_ES3_compatibility  <-- lets #version 300 es compile\n", ext_es3 ? "YES" : " - ");
    printf("    [%s] GL_OVR_multiview_multisampled_render_to_texture\n", ext_mvmsaa ? "YES" : " - ");
    printf("    [%s] glFramebufferTextureMultiviewOVR entry point\n",
           pglFramebufferTextureMultiviewOVR ? "YES" : " - ");
    if (ext_mv2) {
        GLint maxviews = 0;
        glGetIntegerv(GL_MAX_VIEWS_OVR, &maxviews);
        printf("    GL_MAX_VIEWS_OVR = %d  (need >= 2)\n", maxviews);
    }
    printf("\n");

    // shader compiles
    printf("  Shader compilation:\n");
    bool ok_es_plain = CompileVS(esPlainVP,          "#version 300 es, no multiview");
    bool ok_desktop  = CompileVS(desktopMultiviewVP, "#version 330 core + multiview2");
    bool ok_theirs   = CompileVS(interactionShaderVP,"THEIRS: interactionShaderVP verbatim");
    printf("\n");

    // multiview FBO
    printf("  Multiview FBO on a locally-created 2-layer array texture:\n");
    bool ok_fbo = false;
    if (pglFramebufferTextureMultiviewOVR && pglTexImage3D && pglGenFramebuffers) {
        GLuint tex = 0, fbo = 0;
        glGenTextures(1, &tex);
        printf("    local 2-layer array texture id = %u\n", tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
        pglTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 256, 256, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        pglGenFramebuffers(1, &fbo);
        pglBindFramebuffer(GL_FRAMEBUFFER, fbo);
        while (glGetError() != GL_NO_ERROR) {}          // drain first, per the Raze lesson
        pglFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, 0, 2);
        GLenum err = glGetError();
        GLenum st = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
        ok_fbo = (err == GL_NO_ERROR && st == GL_FRAMEBUFFER_COMPLETE);
        printf("    glFramebufferTextureMultiviewOVR -> GL error 0x%04X, FBO status 0x%04X : %s\n",
               err, st, ok_fbo ? "COMPLETE" : "**not complete**");
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Negative control. A flat 2D texture must NOT satisfy numViews=2.
        // If this is accepted, a COMPLETE above proves nothing and the whole
        // multiview verdict has to be thrown away.
        GLuint flat = 0, fbo2 = 0;
        glGenTextures(1, &flat);
        glBindTexture(GL_TEXTURE_2D, flat);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        pglGenFramebuffers(1, &fbo2);
        pglBindFramebuffer(GL_FRAMEBUFFER, fbo2);
        while (glGetError() != GL_NO_ERROR) {}
        pglFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, flat, 0, 0, 2);
        GLenum nerr = glGetError();
        GLenum nst  = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
        bool neg_rejected = !(nerr == GL_NO_ERROR && nst == GL_FRAMEBUFFER_COMPLETE);
        printf("    NEGATIVE CONTROL, flat 2D texture id %u with numViews=2 -> GL error 0x%04X, status 0x%04X : %s\n",
               flat, nerr, nst,
               neg_rejected ? "rejected, as it must be"
                            : "**ACCEPTED - the positive result above is meaningless**");
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        printf("    skipped, entry points unavailable\n");
    }
    printf("\n");

    // ------------------------------------------------------------- OpenXR
    printf("--- PART 2: OpenXR / VDXR (needs the headset streaming) ---\n\n");

    g_loader = LoadLibraryA("openxr_loader.dll");
    if (!g_loader) {
        printf("  FATAL: openxr_loader.dll not found beside the exe (err %lu)\n", GetLastError());
        return 1;
    }
    XRLOAD(xrEnumerateInstanceExtensionProperties)
    XRLOAD(xrCreateInstance)
    XRLOAD(xrDestroyInstance)
    XRLOAD(xrGetInstanceProperties)
    XRLOAD(xrGetSystem)
    XRLOAD(xrGetInstanceProcAddr)
    XRLOAD(xrCreateSession)
    XRLOAD(xrDestroySession)
    XRLOAD(xrEnumerateSwapchainFormats)
    XRLOAD(xrCreateSwapchain)
    XRLOAD(xrDestroySwapchain)
    XRLOAD(xrEnumerateSwapchainImages)
    XRLOAD(xrEnumerateViewConfigurationViews)

    uint32_t count = 0;
    bool have_gl = false;
    xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL);
    XrExtensionProperties* exts = (XrExtensionProperties*)calloc(count, sizeof(*exts));
    for (uint32_t i = 0; i < count; i++) exts[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    xrEnumerateInstanceExtensionProperties(NULL, count, &count, exts);
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(exts[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) have_gl = true;
    for (uint32_t i = 0; i < count; i++) printf("      %s\n", exts[i].extensionName);
    printf("  runtime advertises %u extensions; XR_KHR_opengl_enable: %s\n",
           count, have_gl ? "YES" : "NO");
    if (!have_gl) { printf("  -> cannot bind GL to this runtime. Stopping.\n"); return 2; }

    const char* enabled[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo ici = { XR_TYPE_INSTANCE_CREATE_INFO };
    strcpy(ici.applicationInfo.applicationName, "PreyVR probe");
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = enabled;
    XrInstance instance = XR_NULL_HANDLE;
    XrResult r = xrCreateInstance(&ici, &instance);
    if (XR_FAILED(r)) { printf("  xrCreateInstance FAILED (%d)\n", (int)r); return 3; }

    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &ip)))
        printf("  runtime: %s\n", ip.runtimeName);

    XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sysid = XR_NULL_SYSTEM_ID;
    r = xrGetSystem(instance, &sgi, &sysid);
    if (XR_FAILED(r)) {
        printf("\n  xrGetSystem FAILED (%d) - no headset streaming.\n", (int)r);
        printf("  Part 1 above is still valid. Re-run with Virtual Desktop\n");
        printf("  connected to answer the swapchain question.\n");
        return 4;
    }

    uint32_t nviews = 0;
    xrEnumerateViewConfigurationViews(instance, sysid,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nviews, NULL);
    XrViewConfigurationView* vcv = (XrViewConfigurationView*)calloc(nviews, sizeof(*vcv));
    for (uint32_t i = 0; i < nviews; i++) vcv[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    xrEnumerateViewConfigurationViews(instance, sysid,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nviews, &nviews, vcv);
    printf("  stereo views: %u, recommended %u x %u per eye\n",
           nviews, vcv[0].recommendedImageRectWidth, vcv[0].recommendedImageRectHeight);

    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = NULL;
    xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
                          (PFN_xrVoidFunction*)&xrGetOpenGLGraphicsRequirementsKHR);
    if (xrGetOpenGLGraphicsRequirementsKHR) {
        XrGraphicsRequirementsOpenGLKHR gr = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
        if (XR_SUCCEEDED(xrGetOpenGLGraphicsRequirementsKHR(instance, sysid, &gr))) {
            printf("  GL requirement: min ");
            PrintVer("", gr.minApiVersionSupported);
            PrintVer(", max ", gr.maxApiVersionSupported);
            printf("\n");
        }
    } else {
        printf("  !! xrGetOpenGLGraphicsRequirementsKHR not resolvable\n");
    }

    XrGraphicsBindingOpenGLWin32KHR gb = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    gb.hDC = hdc;
    gb.hGLRC = hglrc;
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &gb;
    sci.systemId = sysid;
    XrSession session = XR_NULL_HANDLE;
    r = xrCreateSession(instance, &sci, &session);
    printf("\n  xrCreateSession with XrGraphicsBindingOpenGLWin32KHR: %s",
           XR_SUCCEEDED(r) ? "OK\n" : "");
    if (XR_FAILED(r)) { printf("FAILED (%d)\n", (int)r); return 5; }

    uint32_t nfmt = 0;
    xrEnumerateSwapchainFormats(session, 0, &nfmt, NULL);
    int64_t* fmts = (int64_t*)calloc(nfmt, sizeof(int64_t));
    xrEnumerateSwapchainFormats(session, nfmt, &nfmt, fmts);
    printf("  %u swapchain formats; first few:", nfmt);
    for (uint32_t i = 0; i < nfmt && i < 6; i++) printf(" 0x%04X", (unsigned)fmts[i]);
    printf("\n\n");

    // ---- the decisive test ----
    int64_t fmt = nfmt ? fmts[0] : GL_SRGB8_ALPHA8;
    for (uint32_t i = 0; i < nfmt; i++) if (fmts[i] == GL_SRGB8_ALPHA8) { fmt = GL_SRGB8_ALPHA8; break; }

    for (int arraySize = 1; arraySize <= 2; arraySize++) {
        XrSwapchainCreateInfo scci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        scci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        scci.format = fmt;
        scci.sampleCount = 1;
        scci.width = vcv[0].recommendedImageRectWidth;
        scci.height = vcv[0].recommendedImageRectHeight;
        scci.faceCount = 1;
        scci.arraySize = arraySize;
        scci.mipCount = 1;
        XrSwapchain sc = XR_NULL_HANDLE;
        r = xrCreateSwapchain(session, &scci, &sc);
        printf("  xrCreateSwapchain arraySize=%d (%ux%u, format 0x%04X) : %s\n",
               arraySize, scci.width, scci.height, (unsigned)fmt,
               XR_SUCCEEDED(r) ? "OK" : "**FAILED**");
        if (XR_FAILED(r)) { printf("      XrResult %d\n", (int)r); continue; }

        uint32_t nimg = 0;
        xrEnumerateSwapchainImages(sc, 0, &nimg, NULL);
        XrSwapchainImageOpenGLKHR* imgs =
            (XrSwapchainImageOpenGLKHR*)calloc(nimg, sizeof(*imgs));
        for (uint32_t i = 0; i < nimg; i++) imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        xrEnumerateSwapchainImages(sc, nimg, &nimg, (XrSwapchainImageBaseHeader*)imgs);
        printf("      %u images, first GL texture id %u\n", nimg, imgs[0].image);

        if (arraySize == 2 && pglFramebufferTextureMultiviewOVR && nimg) {
            GLuint fbo = 0;
            pglGenFramebuffers(1, &fbo);
            pglBindFramebuffer(GL_FRAMEBUFFER, fbo);
            while (glGetError() != GL_NO_ERROR) {}      // drain first
            // Confirm the runtime handed us an array texture with 2 layers,
            // rather than trusting a COMPLETE that might come from elsewhere.
            GLint depth = -1, w = -1;
            glBindTexture(GL_TEXTURE_2D_ARRAY, imgs[0].image);
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_DEPTH_, &depth);
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &w);
            printf("      runtime texture %u queried as GL_TEXTURE_2D_ARRAY: width %d, depth(layers) %d\n",
                   imgs[0].image, w, depth);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            pglFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              imgs[0].image, 0, 0, 2);
            GLenum err = glGetError();
            GLenum st = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
            printf("      multiview FBO on the runtime's own texture -> GL error 0x%04X, status 0x%04X : %s\n",
                   err, st,
                   (err == GL_NO_ERROR && st == GL_FRAMEBUFFER_COMPLETE)
                       ? "COMPLETE  <== this is the answer" : "**not complete**");
            pglBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        free(imgs);
        xrDestroySwapchain(sc);
    }

    printf("\n--- verdict inputs ---\n");
    printf("  multiview2 extension  : %s\n", ext_mv2 ? "present" : "ABSENT");
    printf("  their shader verbatim : %s\n", ok_theirs ? "compiles" : "REJECTED");
    printf("  local multiview FBO   : %s\n", ok_fbo ? "complete" : "NOT COMPLETE");
    printf("  (ES dialect alone %s, desktop multiview alone %s)\n",
           ok_es_plain ? "ok" : "REJECTED", ok_desktop ? "ok" : "REJECTED");

    xrDestroySession(session);
    xrDestroyInstance(instance);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hglrc);
    return 0;
}
