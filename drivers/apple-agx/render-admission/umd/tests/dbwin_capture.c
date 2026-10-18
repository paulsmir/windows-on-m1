#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define DBWIN_BUFFER_BYTES 4096u
#define CAPTURE_RECORD_CAPACITY 256u
#define SELF_MARKER "AppleAgx DBWIN self-test"
#define CHILD_MARKER "AppleAgx DBWIN child self-test"

typedef struct _CAPTURE_RECORD {
  DWORD ProcessId;
  ULONGLONG Timestamp;
  char Message[DBWIN_BUFFER_BYTES - sizeof(DWORD)];
} CAPTURE_RECORD;

typedef struct _SELF_TEST_CONTEXT {
  const char *Message;
} SELF_TEST_CONTEXT;

static DWORD WINAPI SelfTestThread(void *Context) {
  const SELF_TEST_CONTEXT *test = (const SELF_TEST_CONTEXT *)Context;
  Sleep(100u);
  OutputDebugStringA(test->Message);
  return 0u;
}

static int EmitWideMarker(const wchar_t *Message) {
  char marker[DBWIN_BUFFER_BYTES - sizeof(DWORD)];
  int bytes;
  if (Message == NULL)
    return 2;
  bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Message, -1, marker,
                              (int)sizeof(marker), NULL, NULL);
  if (bytes <= 0)
    return 2;
  OutputDebugStringA(marker);
  return 0;
}

static int CopyRecord(CAPTURE_RECORD *Record, const unsigned char *Buffer,
                      ULONGLONG Timestamp) {
  const char *message;
  size_t bytes = 0u;
  if (Record == NULL || Buffer == NULL)
    return 0;
  message = (const char *)(Buffer + sizeof(DWORD));
  while (bytes + 1u < sizeof(Record->Message) && message[bytes] != '\0')
    ++bytes;
  Record->ProcessId = *(const DWORD *)Buffer;
  Record->Timestamp = Timestamp;
  memcpy(Record->Message, message, bytes);
  Record->Message[bytes] = '\0';
  return 1;
}

static int WriteBytes(HANDLE File, const char *Buffer, DWORD Bytes) {
  DWORD written;
  return WriteFile(File, Buffer, Bytes, &written, NULL) && written == Bytes;
}

static int WriteRecord(HANDLE File, const CAPTURE_RECORD *Record) {
  char line[DBWIN_BUFFER_BYTES + 128u];
  int bytes;
  if (Record == NULL)
    return 0;
  bytes = _snprintf_s(line, sizeof(line), _TRUNCATE,
                      "timestamp_ms=%llu pid=%lu message=%s\r\n",
                      Record->Timestamp, Record->ProcessId, Record->Message);
  return bytes >= 0 && WriteBytes(File, line, (DWORD)bytes);
}

static int WriteSummary(HANDLE File, ULONG Records, ULONG Lost) {
  char line[128];
  int bytes = _snprintf_s(line, sizeof(line), _TRUNCATE,
                          "capture=summary ready=1 records=%lu lost=%lu\r\n",
                          Records, Lost);
  return bytes >= 0 && WriteBytes(File, line, (DWORD)bytes) &&
         FlushFileBuffers(File);
}

static BOOL StartMarkerChild(HANDLE *Process, DWORD *ProcessId) {
  wchar_t executable[MAX_PATH];
  wchar_t command[2u * MAX_PATH];
  STARTUPINFOW startup;
  PROCESS_INFORMATION process;
  if (Process == NULL || ProcessId == NULL ||
      GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable)) == 0u)
    return FALSE;
  if (_snwprintf_s(command, ARRAYSIZE(command), _TRUNCATE,
                   L"\"%s\" --emit-marker \"%S\"", executable,
                   CHILD_MARKER) < 0)
    return FALSE;
  ZeroMemory(&startup, sizeof(startup));
  ZeroMemory(&process, sizeof(process));
  startup.cb = sizeof(startup);
  if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0u, NULL, NULL,
                      &startup, &process))
    return FALSE;
  CloseHandle(process.hThread);
  *Process = process.hProcess;
  *ProcessId = process.dwProcessId;
  return TRUE;
}

static BOOL ExactRecordSeen(const CAPTURE_RECORD *Records, ULONG Count,
                            DWORD ProcessId, const char *Message) {
  ULONG index;
  for (index = 0u; index < Count; ++index) {
    if (Records[index].ProcessId == ProcessId &&
        strcmp(Records[index].Message, Message) == 0)
      return TRUE;
  }
  return FALSE;
}

int wmain(int ArgumentCount, wchar_t **Arguments) {
  const wchar_t *output;
  DWORD duration;
  BOOL selfTest;
  BOOL controlled;
  HANDLE bufferReady = NULL;
  HANDLE dataReady = NULL;
  HANDLE mapping = NULL;
  HANDLE readyEvent = NULL;
  HANDLE stopEvent = NULL;
  HANDLE file = INVALID_HANDLE_VALUE;
  HANDLE testThread = NULL;
  HANDLE childProcess = NULL;
  unsigned char *buffer = NULL;
  CAPTURE_RECORD *records = NULL;
  ULONG recordCount = 0u;
  ULONG lost = 0u;
  DWORD childProcessId = 0u;
  DWORD selfProcessId = GetCurrentProcessId();
  ULONGLONG deadline;
  int result = 1;
  BOOL captureReady = FALSE;
  SELF_TEST_CONTEXT test = {SELF_MARKER};

  if (ArgumentCount == 3 && wcscmp(Arguments[1], L"--emit-marker") == 0)
    return EmitWideMarker(Arguments[2]);
  if (ArgumentCount < 3 || ArgumentCount > 6)
    return 2;
  output = Arguments[1];
  duration = wcstoul(Arguments[2], NULL, 10);
  if (duration == 0u || duration > 60000u)
    return 2;
  selfTest = ArgumentCount == 4 && wcscmp(Arguments[3], L"--self-test") == 0;
  controlled = ArgumentCount == 6 && wcscmp(Arguments[3], L"--control") == 0;
  if (!selfTest && !controlled && ArgumentCount != 3)
    return 2;

  bufferReady = CreateEventW(NULL, FALSE, FALSE, L"DBWIN_BUFFER_READY");
  if (bufferReady == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    goto cleanup;
  dataReady = CreateEventW(NULL, FALSE, FALSE, L"DBWIN_DATA_READY");
  if (dataReady == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    goto cleanup;
  mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0u,
                               DBWIN_BUFFER_BYTES, L"DBWIN_BUFFER");
  if (mapping == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    goto cleanup;
  if (controlled) {
    readyEvent = CreateEventW(NULL, TRUE, FALSE, Arguments[4]);
    if (readyEvent == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
      goto cleanup;
    stopEvent = CreateEventW(NULL, TRUE, FALSE, Arguments[5]);
    if (stopEvent == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
      goto cleanup;
  }
  buffer = (unsigned char *)MapViewOfFile(
      mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0u, 0u, DBWIN_BUFFER_BYTES);
  records = (CAPTURE_RECORD *)HeapAlloc(
      GetProcessHeap(), HEAP_ZERO_MEMORY,
      CAPTURE_RECORD_CAPACITY * sizeof(CAPTURE_RECORD));
  file = CreateFileW(output, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  if (buffer == NULL || records == NULL || file == INVALID_HANDLE_VALUE)
    goto cleanup;
  captureReady = TRUE;

  if (selfTest) {
    testThread = CreateThread(NULL, 0u, SelfTestThread, &test, 0u, NULL);
    if (testThread == NULL ||
        !StartMarkerChild(&childProcess, &childProcessId))
      goto cleanup;
  }
  if (readyEvent != NULL && !SetEvent(readyEvent))
    goto cleanup;

  deadline = GetTickCount64() + duration;
  do {
    HANDLE waits[2];
    DWORD waitCount = 1u;
    ULONGLONG now = GetTickCount64();
    DWORD remaining = now < deadline ? (DWORD)(deadline - now) : 0u;
    DWORD wait;
    if (!SetEvent(bufferReady))
      goto cleanup;
    waits[0] = dataReady;
    if (stopEvent != NULL)
      waits[waitCount++] = stopEvent;
    wait = WaitForMultipleObjects(waitCount, waits, FALSE, remaining);
    if (wait == WAIT_OBJECT_0) {
      if (recordCount < CAPTURE_RECORD_CAPACITY) {
        if (!CopyRecord(&records[recordCount], buffer, GetTickCount64()))
          goto cleanup;
        ++recordCount;
      } else {
        ++lost;
      }
    } else if (stopEvent != NULL && wait == WAIT_OBJECT_0 + 1u) {
      break;
    } else if (wait != WAIT_TIMEOUT) {
      goto cleanup;
    }
  } while (GetTickCount64() < deadline);

  if (testThread != NULL &&
      WaitForSingleObject(testThread, 1000u) != WAIT_OBJECT_0)
    goto cleanup;
  if (childProcess != NULL &&
      WaitForSingleObject(childProcess, 1000u) != WAIT_OBJECT_0)
    goto cleanup;
  if (selfTest &&
      (!ExactRecordSeen(records, recordCount, selfProcessId, SELF_MARKER) ||
       !ExactRecordSeen(records, recordCount, childProcessId, CHILD_MARKER))) {
    result = 3;
  } else {
    result = 0;
  }

cleanup:
  if (file != INVALID_HANDLE_VALUE && captureReady) {
    ULONG index;
    for (index = 0u; index < recordCount; ++index) {
      if (!WriteRecord(file, &records[index]))
        result = 1;
    }
    if (!WriteSummary(file, recordCount, lost))
      result = 1;
  }
  if (testThread != NULL)
    CloseHandle(testThread);
  if (childProcess != NULL) {
    if (WaitForSingleObject(childProcess, 0u) == WAIT_TIMEOUT)
      TerminateProcess(childProcess, 1u);
    CloseHandle(childProcess);
  }
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  if (records != NULL)
    HeapFree(GetProcessHeap(), 0u, records);
  if (buffer != NULL)
    UnmapViewOfFile(buffer);
  if (stopEvent != NULL)
    CloseHandle(stopEvent);
  if (readyEvent != NULL)
    CloseHandle(readyEvent);
  if (mapping != NULL)
    CloseHandle(mapping);
  if (dataReady != NULL)
    CloseHandle(dataReady);
  if (bufferReady != NULL)
    CloseHandle(bufferReady);
  return result;
}
