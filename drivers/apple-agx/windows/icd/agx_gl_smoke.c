/* CS 1.6 ICD plan, phase 5: x86 OpenGL smoke test in GoldSrc style.
 *
 * Opens a 640x480 window, creates a legacy WGL context (opengl32.dll from the
 * executable's directory when present: Mesa's libgl-gdi + our
 * libgallium_wgl.dll), prints GL_VENDOR/RENDERER/VERSION, then for N frames
 * clears to red, draws a green immediate-mode triangle (glBegin/glEnd, the
 * GoldSrc renderer's path) and a blue textured quad, reads back pixels and
 * calls SwapBuffers. Exit 0 only when every checked pixel matches.
 * Usage: agx_gl_smoke.exe [frames] [expect-renderer-substring]
 * Run from a directory without opengl32.dll to exercise Microsoft's
 * opengl32.dll and the adapter's registered ICD (EXP1156). */
#include <windows.h>
#include <dbghelp.h>
#include <GL/gl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EXP1146 lost stdout and had no dump when the process faulted: write a
 * minidump beside the executable and report the fault on stderr. */
/* EXT_framebuffer_object / EXT_framebuffer_blit (GL 1.1 headers lack them). */
#define GL_FRAMEBUFFER_EXT 0x8D40
#define GL_READ_FRAMEBUFFER_EXT 0x8CA8
#define GL_DRAW_FRAMEBUFFER_EXT 0x8CA9
#define GL_COLOR_ATTACHMENT0_EXT 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE_EXT 0x8CD5
typedef void (APIENTRY *PFNGENFB)(GLsizei, GLuint *);
typedef void (APIENTRY *PFNBINDFB)(GLenum, GLuint);
typedef void (APIENTRY *PFNFBTEX2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFNCHECKFB)(GLenum);
typedef void (APIENTRY *PFNBLITFB)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

static LONG WINAPI crash(EXCEPTION_POINTERS *info) {
  char path[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
  while (n && path[n - 1] != '\\') --n;
  strcpy_s(path + n, MAX_PATH - n, "agx_gl_smoke.dmp");
  fprintf(stderr, "CRASH code %08lx address %p\n", info->ExceptionRecord->ExceptionCode,
          info->ExceptionRecord->ExceptionAddress);
  HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (f != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION e = {GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
        (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
        &e, NULL, NULL);
    CloseHandle(f);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

/* EXP1147 hung after frame 0 and was killed with no evidence: a watchdog
 * thread dumps every thread's stack when no frame finishes for 15 s. */
static volatile LONG frame_progress;
static DWORD WINAPI watchdog(LPVOID unused) {
  LONG last = -1; DWORD idle = 0;
  (void)unused;
  for (;;) {
    Sleep(1000);
    LONG now = frame_progress;
    if (now != last) { last = now; idle = 0; continue; }
    if (++idle < 15) continue;
    char path[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    while (n && path[n - 1] != '\\') --n;
    strcpy_s(path + n, MAX_PATH - n, "agx_gl_smoke_hang.dmp");
    printf("HANG no frame progress for 15 s after step %ld\n", now);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
          (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
          NULL, NULL, NULL);
      CloseHandle(f);
    }
    TerminateProcess(GetCurrentProcess(), 6);
  }
}

/* EXP1148: a Mesa assert opened the CRT's modal message box and the test
 * looked hung. Print asserts to stderr and dump on the resulting abort. */
static void on_abort(int sig) {
  char path[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
  (void)sig;
  while (n && path[n - 1] != '\\') --n;
  strcpy_s(path + n, MAX_PATH - n, "agx_gl_smoke_abort.dmp");
  fprintf(stderr, "ABORT\n");
  HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (f != INVALID_HANDLE_VALUE) {
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
        (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
        NULL, NULL, NULL);
    CloseHandle(f);
  }
  TerminateProcess(GetCurrentProcess(), 7);
}

/* EXP1156: what dxgkrnl names as this process's OpenGL ICD for the window's
 * adapter (KMTQAITYPE_UMOPENGLINFO; a WOW64 caller gets the ...Wow values).
 * Layouts of d3dkmthk.h, declared here so the test builds without the WDK. */
typedef struct { HDC hDc; UINT hAdapter; LUID AdapterLuid; UINT VidPnSourceId; } SMOKE_OPENADAPTERFROMHDC;
typedef struct { UINT hAdapter; int Type; void *pPrivateDriverData; UINT PrivateDriverDataSize; } SMOKE_QUERYADAPTERINFO;
typedef struct { WCHAR UmdOpenGlIcdFileName[MAX_PATH]; ULONG Version; ULONG Flags; } SMOKE_OPENGLINFO;
typedef struct { UINT hAdapter; } SMOKE_CLOSEADAPTER;
typedef struct { WCHAR DeviceName[32]; UINT hAdapter; LUID AdapterLuid; UINT VidPnSourceId; } SMOKE_OPENADAPTERFROMGDI;
static void print_icd_info(HWND wnd, HDC dc) {
  HMODULE gdi = GetModuleHandleA("gdi32.dll");
  LONG (APIENTRY *open)(SMOKE_OPENADAPTERFROMHDC *) = NULL;
  LONG (APIENTRY *query)(SMOKE_QUERYADAPTERINFO *) = NULL;
  LONG (APIENTRY *close)(SMOKE_CLOSEADAPTER *) = NULL;
  LONG (APIENTRY *opengdi)(SMOKE_OPENADAPTERFROMGDI *) = NULL;
  SMOKE_OPENADAPTERFROMHDC o; SMOKE_QUERYADAPTERINFO q; SMOKE_OPENGLINFO info; SMOKE_CLOSEADAPTER c;
  LONG status;
  if (gdi) {
    *(FARPROC *)&open = GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc");
    *(FARPROC *)&query = GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
    *(FARPROC *)&close = GetProcAddress(gdi, "D3DKMTCloseAdapter");
    *(FARPROC *)&opengdi = GetProcAddress(gdi, "D3DKMTOpenAdapterFromGdiDisplayName");
  }
  if (!open || !query || !close) { printf("ICD_INFO no-thunks\n"); return; }
  memset(&o, 0, sizeof(o)); o.hDc = dc;
  status = open(&o);
  if (status < 0 && opengdi) {
    /* A DC outside an interactive session has no adapter (builder control);
     * the window's monitor names the GDI display device instead. */
    MONITORINFOEXW mi; SMOKE_OPENADAPTERFROMGDI g;
    printf("ICD_INFO open-adapter-from-hdc %08lx\n", (unsigned long)status);
    memset(&mi, 0, sizeof(mi)); mi.cbSize = sizeof(mi);
    memset(&g, 0, sizeof(g));
    if (GetMonitorInfoW(MonitorFromWindow(wnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi)) {
      wcsncpy_s(g.DeviceName, 32, mi.szDevice, _TRUNCATE);
      status = opengdi(&g);
      o.hAdapter = g.hAdapter; o.AdapterLuid = g.AdapterLuid;
    }
  }
  if (status < 0) { printf("ICD_INFO open-adapter %08lx\n", (unsigned long)status); return; }
  memset(&info, 0, sizeof(info)); memset(&q, 0, sizeof(q));
  q.hAdapter = o.hAdapter; q.Type = 2; /* KMTQAITYPE_UMOPENGLINFO */
  q.pPrivateDriverData = &info; q.PrivateDriverDataSize = sizeof(info);
  status = query(&q);
  printf("ICD_INFO status %08lx luid %08lx:%08lx name \"%ls\" version %lu flags %08lx\n",
         (unsigned long)status, (unsigned long)o.AdapterLuid.HighPart,
         (unsigned long)o.AdapterLuid.LowPart, status < 0 ? L"" : info.UmdOpenGlIcdFileName,
         status < 0 ? 0ul : info.Version, status < 0 ? 0ul : info.Flags);
  c.hAdapter = o.hAdapter; close(&c);
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
  if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
  return DefWindowProcA(w, m, a, b);
}

static int expect(const char *what, int x, int y, unsigned rgb) {
  unsigned char p[4] = {0};
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
  unsigned got = ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
  /* Allow small rounding differences per channel. */
  int ok = abs((int)p[0] - (int)(rgb >> 16 & 255)) <= 2 &&
           abs((int)p[1] - (int)(rgb >> 8 & 255)) <= 2 &&
           abs((int)p[2] - (int)(rgb & 255)) <= 2;
  if (!ok) printf("PIXEL_MISMATCH %s (%d,%d) got %06x expected %06x\n", what, x, y, got, rgb);
  return ok;
}

int main(int argc, char **argv) {
  int frames = argc > 1 ? atoi(argv[1]) : 120;
  setvbuf(stdout, NULL, _IONBF, 0);
  SetUnhandledExceptionFilter(crash);
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  signal(SIGABRT, on_abort);
  CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
  const char *want = argc > 2 && strcmp(argv[2], "-") ? argv[2] : NULL;
  /* "nodepth": a colour-only window (the G4 KMD builder accepts no ZLS
   * attachment yet), cleared with GL_COLOR_BUFFER_BIT only. */
  int nodepth = argc > 3 && !strcmp(argv[3], "nodepth");
  /* "fbo": draw the scene into a colour-only framebuffer object and blit it
   * to the window, so no batch carries the window's depth buffer. */
  int fbo = argc > 3 && !strcmp(argv[3], "fbo");
  /* "depthtest": a near yellow quad, glFlush (the next batch must reload
   * depth through ZLS), then a far magenta quad over it; yellow must win. */
  int depthtest = argc > 3 && !strcmp(argv[3], "depthtest");
  PFNGENFB genfb = NULL; PFNBINDFB bindfb = NULL; PFNFBTEX2D fbtex = NULL;
  PFNCHECKFB checkfb = NULL; PFNBLITFB blitfb = NULL; GLuint fb = 0, fbcolor = 0;
  WNDCLASSA wc; HWND wnd; HDC dc; HGLRC rc; PIXELFORMATDESCRIPTOR pfd;
  int format, failures = 0;
  LARGE_INTEGER f, t0, t1;
  GLuint tex; unsigned char texels[4 * 4 * 4];
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleA(NULL);
  wc.lpszClassName = "AgxGlSmoke"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  RegisterClassA(&wc);
  wnd = CreateWindowA("AgxGlSmoke", "AGX GL smoke", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                      100, 100, 656, 519, NULL, NULL, wc.hInstance, NULL);
  if (!wnd) { printf("NO_WINDOW %lu\n", GetLastError()); return 2; }
  dc = GetDC(wnd);
  print_icd_info(wnd, dc);
  memset(&pfd, 0, sizeof(pfd));
  pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
  format = 0;
  if (nodepth) {
    int count = DescribePixelFormat(dc, 1, sizeof(pfd), &pfd);
    for (int i = 1; i <= count && !format; ++i) {
      PIXELFORMATDESCRIPTOR c;
      DescribePixelFormat(dc, i, sizeof(c), &c);
      if ((c.dwFlags & (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER)) ==
              (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER) &&
          !(c.dwFlags & PFD_GENERIC_FORMAT) && c.iPixelType == PFD_TYPE_RGBA &&
          c.cColorBits >= 24 && !c.cDepthBits && !c.cStencilBits && !c.cAccumBits) {
        format = i; pfd = c;
      }
    }
    if (!format)
      for (int i = 1; i <= count; ++i) {
        PIXELFORMATDESCRIPTOR c;
        DescribePixelFormat(dc, i, sizeof(c), &c);
        if (c.cDepthBits && c.cStencilBits) continue;
        printf("PFD %d flags %08lx type %d color %u alpha %u depth %u stencil %u accum %u\n",
               i, c.dwFlags, c.iPixelType, c.cColorBits, c.cAlphaBits, c.cDepthBits,
               c.cStencilBits, c.cAccumBits);
      }
  } else {
    format = ChoosePixelFormat(dc, &pfd);
  }
  if (!format || !SetPixelFormat(dc, format, &pfd)) { printf("NO_PIXEL_FORMAT %d %lu\n", format, GetLastError()); return 3; }
  { PIXELFORMATDESCRIPTOR got; DescribePixelFormat(dc, format, sizeof(got), &got);
    printf("PIXEL_FORMAT %d flags %08lx color %u depth %u\n", format, got.dwFlags,
           got.cColorBits, got.cDepthBits); }
  InterlockedIncrement(&frame_progress);
  rc = wglCreateContext(dc);
  if (!rc || !wglMakeCurrent(dc, rc)) { printf("NO_CONTEXT %lu\n", GetLastError()); return 4; }
  printf("GL_VENDOR %s\nGL_RENDERER %s\nGL_VERSION %s\n", (const char *)glGetString(GL_VENDOR),
         (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
  if (want && !strstr((const char *)glGetString(GL_RENDERER), want)) {
    printf("UNEXPECTED_RENDERER (want %s)\n", want); return 5;
  }
  for (int i = 0; i < 16; ++i) { texels[4*i] = 0; texels[4*i+1] = 0; texels[4*i+2] = 255; texels[4*i+3] = 255; }
  glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
  if (fbo) {
    *(PROC *)&genfb = wglGetProcAddress("glGenFramebuffersEXT");
    *(PROC *)&bindfb = wglGetProcAddress("glBindFramebufferEXT");
    *(PROC *)&fbtex = wglGetProcAddress("glFramebufferTexture2DEXT");
    *(PROC *)&checkfb = wglGetProcAddress("glCheckFramebufferStatusEXT");
    *(PROC *)&blitfb = wglGetProcAddress("glBlitFramebufferEXT");
    if (!genfb || !bindfb || !fbtex || !checkfb || !blitfb) { printf("NO_FBO_ENTRY_POINTS\n"); return 8; }
    glGenTextures(1, &fbcolor); glBindTexture(GL_TEXTURE_2D, fbcolor);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    genfb(1, &fb); bindfb(GL_FRAMEBUFFER_EXT, fb);
    fbtex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, fbcolor, 0);
    printf("FBO status %04x\n", checkfb(GL_FRAMEBUFFER_EXT));
    glBindTexture(GL_TEXTURE_2D, tex);
  }
  glViewport(0, 0, 640, 480);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 640, 0, 480, -1, 1);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
  for (int n = 0; n < frames; ++n) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    if (fbo) bindfb(GL_FRAMEBUFFER_EXT, fb);
    glClearColor(1.f, 0.f, 0.f, 1.f);
    glClear(nodepth || fbo ? GL_COLOR_BUFFER_BIT : GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.f, 1.f, 0.f);
    glBegin(GL_TRIANGLES);
    glVertex2f(40.f, 40.f); glVertex2f(280.f, 40.f); glVertex2f(40.f, 280.f);
    glEnd();
    if (depthtest) {
      glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
      glColor3f(1.f, 1.f, 0.f); /* near: z = -0.5 in ortho(-1,1) */
      glBegin(GL_QUADS);
      glVertex3f(300.f, 300.f, 0.5f); glVertex3f(380.f, 300.f, 0.5f);
      glVertex3f(380.f, 380.f, 0.5f); glVertex3f(300.f, 380.f, 0.5f);
      glEnd();
      glFlush();
      glColor3f(1.f, 0.f, 1.f); /* far, drawn later */
      glBegin(GL_QUADS);
      glVertex3f(280.f, 280.f, -0.5f); glVertex3f(400.f, 280.f, -0.5f);
      glVertex3f(400.f, 400.f, -0.5f); glVertex3f(280.f, 400.f, -0.5f);
      glEnd();
      glDisable(GL_DEPTH_TEST);
    }
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.f, 1.f, 1.f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(400.f, 200.f);
    glTexCoord2f(1, 0); glVertex2f(600.f, 200.f);
    glTexCoord2f(1, 1); glVertex2f(600.f, 400.f);
    glTexCoord2f(0, 1); glVertex2f(400.f, 400.f);
    glEnd();
    if (fbo) {
      bindfb(GL_READ_FRAMEBUFFER_EXT, fb); bindfb(GL_DRAW_FRAMEBUFFER_EXT, 0);
      blitfb(0, 0, 640, 480, 0, 0, 640, 480, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      bindfb(GL_FRAMEBUFFER_EXT, 0);
    }
    if (n == 0 || n == frames - 1) {
      glFinish();
      failures += !expect("clear", 620, 20, 0xff0000);
      failures += !expect("triangle", 80, 80, 0x00ff00);
      failures += !expect("texture", 500, 300, 0x0000ff);
      if (depthtest) {
        failures += !expect("depth near wins", 340, 340, 0xffff00);
        failures += !expect("depth far visible", 290, 290, 0xff00ff);
      }
      printf("FRAME %d checked, glGetError 0x%x\n", n, glGetError());
    }
    InterlockedIncrement(&frame_progress);
    SwapBuffers(dc);
    InterlockedIncrement(&frame_progress);
  }
  QueryPerformanceCounter(&t1);
  printf("FRAMES %d in %.3f s = %.1f fps\n", frames,
         (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart,
         frames * (double)f.QuadPart / (double)(t1.QuadPart - t0.QuadPart));
  wglMakeCurrent(NULL, NULL); wglDeleteContext(rc); ReleaseDC(wnd, dc); DestroyWindow(wnd);
  printf(failures ? "AGX_GL_SMOKE: FAIL %d\n" : "AGX_GL_SMOKE: PASS\n", failures);
  return failures ? 1 : 0;
}
