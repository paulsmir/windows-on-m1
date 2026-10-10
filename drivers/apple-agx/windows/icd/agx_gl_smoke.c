/* CS 1.6 ICD plan, phase 5: x86 OpenGL smoke test in GoldSrc style.
 *
 * Opens a 640x480 window, creates a legacy WGL context (opengl32.dll from the
 * executable's directory when present: Mesa's libgl-gdi + our
 * libgallium_wgl.dll), prints GL_VENDOR/RENDERER/VERSION, then for N frames
 * clears to red, draws a green immediate-mode triangle (glBegin/glEnd, the
 * GoldSrc renderer's path) and a blue textured quad, reads back pixels and
 * calls SwapBuffers. Exit 0 only when every checked pixel matches.
 * Usage: agx_gl_smoke.exe [frames] [expect-renderer-substring] */
#include <windows.h>
#include <dbghelp.h>
#include <GL/gl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EXP1146 lost stdout and had no dump when the process faulted: write a
 * minidump beside the executable and report the fault on stderr. */
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
  const char *want = argc > 2 ? argv[2] : NULL;
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
  memset(&pfd, 0, sizeof(pfd));
  pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
  format = ChoosePixelFormat(dc, &pfd);
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
  glViewport(0, 0, 640, 480);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 640, 0, 480, -1, 1);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
  for (int n = 0; n < frames; ++n) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    glClearColor(1.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.f, 1.f, 0.f);
    glBegin(GL_TRIANGLES);
    glVertex2f(40.f, 40.f); glVertex2f(280.f, 40.f); glVertex2f(40.f, 280.f);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.f, 1.f, 1.f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(400.f, 200.f);
    glTexCoord2f(1, 0); glVertex2f(600.f, 200.f);
    glTexCoord2f(1, 1); glVertex2f(600.f, 400.f);
    glTexCoord2f(0, 1); glVertex2f(400.f, 400.f);
    glEnd();
    if (n == 0 || n == frames - 1) {
      glFinish();
      failures += !expect("clear", 620, 20, 0xff0000);
      failures += !expect("triangle", 80, 80, 0x00ff00);
      failures += !expect("texture", 500, 300, 0x0000ff);
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
