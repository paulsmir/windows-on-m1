#include <windows.h>

int wmain(int argc, wchar_t **argv) {
  HMODULE module;
  const char *exports[] = {"wglCreateContext", "wglMakeCurrent",
                           "wglGetProcAddress", "glClear"};
  unsigned index;
  if (argc != 2)
    return 2;
  module = LoadLibraryW(argv[1]);
  if (module == NULL)
    return 3;
  for (index = 0u; index < sizeof(exports) / sizeof(exports[0]); ++index)
    if (GetProcAddress(module, exports[index]) == NULL)
      return 4;
  return 0;
}
