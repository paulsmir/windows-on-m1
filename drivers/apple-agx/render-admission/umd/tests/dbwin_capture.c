#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define DBWIN_BUFFER_BYTES 4096u

typedef struct _SELF_TEST_CONTEXT {
  const char *Message;
} SELF_TEST_CONTEXT;

static DWORD WINAPI SelfTestThread(void *Context) {
  const SELF_TEST_CONTEXT *test = (const SELF_TEST_CONTEXT *)Context;
  Sleep(100u);
  OutputDebugStringA(test->Message);
  return 0u;
}

static int WriteRecord(HANDLE File, const unsigned char *Buffer,
                       ULONGLONG Timestamp) {
  const DWORD processId = *(const DWORD *)Buffer;
  const char *message = (const char *)(Buffer + sizeof(DWORD));
  size_t messageBytes = 0u;
  char line[DBWIN_BUFFER_BYTES + 128u];
  int lineBytes;
  DWORD written;
  while (messageBytes < DBWIN_BUFFER_BYTES - sizeof(DWORD) &&
         message[messageBytes] != '\0')
    ++messageBytes;
  lineBytes = _snprintf_s(line, sizeof(line), _TRUNCATE,
                          "timestamp_ms=%llu pid=%lu message=%.*s\r\n",
                          Timestamp, processId, (int)messageBytes, message);
  if (lineBytes < 0 || !WriteFile(File, line, (DWORD)lineBytes, &written, NULL) ||
      written != (DWORD)lineBytes || !FlushFileBuffers(File))
    return 0;
  return 1;
}

int wmain(int ArgumentCount, wchar_t **Arguments) {
  const wchar_t *output;
  DWORD duration;
  BOOL selfTest;
  HANDLE bufferReady = NULL;
  HANDLE dataReady = NULL;
  HANDLE mapping = NULL;
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE testThread = NULL;
  unsigned char *buffer = NULL;
  ULONGLONG deadline;
  int result = 1;
  SELF_TEST_CONTEXT test = {"AppleAgx DBWIN self-test"};

  if (ArgumentCount < 3 || ArgumentCount > 4)
    return 2;
  output = Arguments[1];
  duration = wcstoul(Arguments[2], NULL, 10);
  if (duration == 0u || duration > 60000u)
    return 2;
  selfTest = ArgumentCount == 4 && wcscmp(Arguments[3], L"--self-test") == 0;
  if (ArgumentCount == 4 && !selfTest)
    return 2;

  bufferReady = CreateEventW(NULL, FALSE, FALSE, L"DBWIN_BUFFER_READY");
  dataReady = CreateEventW(NULL, FALSE, FALSE, L"DBWIN_DATA_READY");
  mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0u,
                               DBWIN_BUFFER_BYTES, L"DBWIN_BUFFER");
  if (bufferReady == NULL || dataReady == NULL || mapping == NULL ||
      GetLastError() == ERROR_ALREADY_EXISTS)
    goto cleanup;
  buffer = (unsigned char *)MapViewOfFile(
      mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0u, 0u, DBWIN_BUFFER_BYTES);
  file = CreateFileW(output, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  if (buffer == NULL || file == INVALID_HANDLE_VALUE)
    goto cleanup;

  if (selfTest) {
    testThread = CreateThread(NULL, 0u, SelfTestThread, &test, 0u, NULL);
    if (testThread == NULL)
      goto cleanup;
  }

  deadline = GetTickCount64() + duration;
  do {
    ULONGLONG now = GetTickCount64();
    DWORD remaining = now < deadline ? (DWORD)(deadline - now) : 0u;
    DWORD wait;
    if (!SetEvent(bufferReady))
      goto cleanup;
    wait = WaitForSingleObject(dataReady, remaining);
    if (wait == WAIT_OBJECT_0) {
      if (!WriteRecord(file, buffer, GetTickCount64()))
        goto cleanup;
    } else if (wait != WAIT_TIMEOUT) {
      goto cleanup;
    }
  } while (GetTickCount64() < deadline);

  if (testThread != NULL && WaitForSingleObject(testThread, 1000u) != WAIT_OBJECT_0)
    goto cleanup;
  result = 0;

cleanup:
  if (testThread != NULL)
    CloseHandle(testThread);
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  if (buffer != NULL)
    UnmapViewOfFile(buffer);
  if (mapping != NULL)
    CloseHandle(mapping);
  if (dataReady != NULL)
    CloseHandle(dataReady);
  if (bufferReady != NULL)
    CloseHandle(bufferReady);
  return result;
}
