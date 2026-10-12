/* EXP1225 diagnostic: what dxgkrnl tells a 32-bit (WOW64) process about each
 * adapter's user-mode driver. EXP1225's 32-bit DXGI enumerated only the
 * Microsoft Basic Render Driver although the WOW64 UMD was installed as
 * UserModeDriverNameWow; this prints, per adapter from D3DKMTEnumAdapters2,
 * its LUID, then for KMTUMDVERSION_DX9/DX10/DX11/DX12 the status and file
 * name of KMTQAITYPE_UMDRIVERNAME, and the adapter type flags
 * (KMTQAITYPE_ADAPTERTYPE). With a LUID argument (low part, hex) it also opens
 * that adapter with D3DKMTOpenAdapterFromLuid and asks the same questions:
 * whether dxgkrnl hides an adapter from enumeration or refuses it outright.
 * Usage: agx_kmt_umdname.exe [luid-low-hex] */
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <stdlib.h>

typedef NTSTATUS (APIENTRY *ENUM2)(D3DKMT_ENUMADAPTERS2 *);
typedef NTSTATUS (APIENTRY *QUERY)(const D3DKMT_QUERYADAPTERINFO *);
typedef NTSTATUS (APIENTRY *CLOSE)(const D3DKMT_CLOSEADAPTER *);
typedef NTSTATUS (APIENTRY *OPENLUID)(D3DKMT_OPENADAPTERFROMLUID *);
typedef NTSTATUS (APIENTRY *ENUM3)(D3DKMT_ENUMADAPTERS3 *);

static void ask(QUERY query, D3DKMT_HANDLE adapter) {
  D3DKMT_QUERYADAPTERINFO q;
  for (int v = KMTUMDVERSION_DX9; v <= KMTUMDVERSION_DX12; ++v) {
    D3DKMT_UMDFILENAMEINFO name;
    memset(&name, 0, sizeof(name));
    name.Version = (KMTUMDVERSION)v;
    memset(&q, 0, sizeof(q));
    q.hAdapter = adapter; q.Type = KMTQAITYPE_UMDRIVERNAME;
    q.pPrivateDriverData = &name; q.PrivateDriverDataSize = sizeof(name);
    NTSTATUS s = query(&q);
    printf("  UMDRIVERNAME v%d 0x%08lx %ls\n", v, (unsigned long)s, NT_SUCCESS(s) ? name.UmdFileName : L"");
  }
}

int main(int argc, char **argv) {
  HMODULE gdi = LoadLibraryA("gdi32.dll");
  ENUM2 enum2 = (ENUM2)(void *)GetProcAddress(gdi, "D3DKMTEnumAdapters2");
  QUERY query = (QUERY)(void *)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
  CLOSE close = (CLOSE)(void *)GetProcAddress(gdi, "D3DKMTCloseAdapter");
  D3DKMT_ENUMADAPTERS2 e;
  BOOL wow = FALSE;
  IsWow64Process(GetCurrentProcess(), &wow);
  printf("WOW64 %d pointer %u\n", wow, (unsigned)sizeof(void *));
  if (!enum2 || !query) { printf("NO_THUNKS\n"); return 2; }
  memset(&e, 0, sizeof(e));
  NTSTATUS st = enum2(&e);
  printf("ENUM2 count 0x%08lx %u\n", (unsigned long)st, e.NumAdapters);
  e.pAdapters = (D3DKMT_ADAPTERINFO *)calloc(e.NumAdapters ? e.NumAdapters : 1, sizeof(D3DKMT_ADAPTERINFO));
  st = enum2(&e);
  printf("ENUM2 0x%08lx %u\n", (unsigned long)st, e.NumAdapters);
  for (UINT i = 0; NT_SUCCESS(st) && i < e.NumAdapters; ++i) {
    D3DKMT_ADAPTERINFO *a = &e.pAdapters[i];
    printf("ADAPTER %u luid %08lx:%08lx sources %u\n", i, (unsigned long)a->AdapterLuid.HighPart,
           (unsigned long)a->AdapterLuid.LowPart, a->NumOfSources);
    D3DKMT_ADAPTERTYPE type;
    D3DKMT_QUERYADAPTERINFO q;
    memset(&type, 0, sizeof(type)); memset(&q, 0, sizeof(q));
    q.hAdapter = a->hAdapter; q.Type = KMTQAITYPE_ADAPTERTYPE;
    q.pPrivateDriverData = &type; q.PrivateDriverDataSize = sizeof(type);
    printf("  ADAPTERTYPE 0x%08lx value 0x%08x\n", (unsigned long)query(&q), type.Value);
    for (int v = KMTUMDVERSION_DX9; v <= KMTUMDVERSION_DX12; ++v) {
      D3DKMT_UMDFILENAMEINFO name;
      memset(&name, 0, sizeof(name));
      name.Version = (KMTUMDVERSION)v;
      memset(&q, 0, sizeof(q));
      q.hAdapter = a->hAdapter; q.Type = KMTQAITYPE_UMDRIVERNAME;
      q.pPrivateDriverData = &name; q.PrivateDriverDataSize = sizeof(name);
      NTSTATUS s = query(&q);
      printf("  UMDRIVERNAME v%d 0x%08lx %ls\n", v, (unsigned long)s, NT_SUCCESS(s) ? name.UmdFileName : L"");
    }
    if (close) { D3DKMT_CLOSEADAPTER c; c.hAdapter = a->hAdapter; close(&c); }
  }
  /* EnumAdapters3 with each filter: a display-only or compute-only class
   * would explain an adapter missing from the default enumeration. */
  ENUM3 enum3 = (ENUM3)(void *)GetProcAddress(gdi, "D3DKMTEnumAdapters3");
  for (int f = 0; enum3 && f < 4; ++f) {
    D3DKMT_ENUMADAPTERS3 e3;
    D3DKMT_ADAPTERINFO list[16];
    memset(&e3, 0, sizeof(e3)); memset(list, 0, sizeof(list));
    e3.Filter.Value = (ULONGLONG)f;   /* bit0 compute-only, bit1 display-only */
    e3.NumAdapters = 16; e3.pAdapters = list;
    NTSTATUS s3 = enum3(&e3);
    printf("ENUM3 filter %d 0x%08lx %u:", f, (unsigned long)s3, e3.NumAdapters);
    for (UINT i = 0; NT_SUCCESS(s3) && i < e3.NumAdapters; ++i) {
      printf(" %08lx", (unsigned long)list[i].AdapterLuid.LowPart);
      if (close) { D3DKMT_CLOSEADAPTER c; c.hAdapter = list[i].hAdapter; close(&c); }
    }
    printf("\n");
  }
  if (argc > 1) {
    OPENLUID open = (OPENLUID)(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
    D3DKMT_OPENADAPTERFROMLUID o;
    memset(&o, 0, sizeof(o));
    o.AdapterLuid.LowPart = (DWORD)strtoul(argv[1], NULL, 16);
    NTSTATUS s = open ? open(&o) : (NTSTATUS)0xc0000002L;
    printf("OPENLUID %08lx 0x%08lx handle %u\n", (unsigned long)o.AdapterLuid.LowPart, (unsigned long)s, o.hAdapter);
    if (NT_SUCCESS(s)) {
      ask(query, o.hAdapter);
      if (close) { D3DKMT_CLOSEADAPTER c; c.hAdapter = o.hAdapter; close(&c); }
    }
  }
  printf("AGX_KMT_UMDNAME: DONE\n");
  return 0;
}
