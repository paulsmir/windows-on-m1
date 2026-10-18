#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Read-only console topology: no mode, power, resource, or present calls. */
static LONG DumpActivePaths(void) {
  const UINT32 flags = QDC_ONLY_ACTIVE_PATHS;
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    UINT32 pc = 0, mc = 0;
    LONG result = GetDisplayConfigBufferSizes(flags, &pc, &mc);
    wprintf(L"PATH_SIZES flags=0x%x status=%ld paths=%u modes=%u\n", flags, result, pc, mc);
    if (result != ERROR_SUCCESS) return result;
    if (pc > 256 || mc > 1024) return ERROR_INVALID_DATA;
    DISPLAYCONFIG_PATH_INFO *paths = calloc(pc ? pc : 1, sizeof(*paths));
    DISPLAYCONFIG_MODE_INFO *modes = calloc(mc ? mc : 1, sizeof(*modes));
    if (!paths || !modes) { free(paths); free(modes); return ERROR_OUTOFMEMORY; }
    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pc, paths, &mc, modes, NULL);
    wprintf(L"PATH_RESULT flags=0x%x status=%ld paths=%u modes=%u\n", flags, result, pc, mc);
    if (result == ERROR_SUCCESS) {
      for (UINT32 i = 0; i < pc; ++i) {
        DISPLAYCONFIG_PATH_INFO *p = &paths[i];
        wprintf(L"PATH index=%u flags=0x%x source=%08lx:%08lx/%u source_status=0x%x source_mode=%u target=%08lx:%08lx/%u available=%d target_status=0x%x target_mode=%u technology=%u rotation=%u scaling=%u refresh=%u/%u\n",
          i,p->flags,(ULONG)p->sourceInfo.adapterId.HighPart,p->sourceInfo.adapterId.LowPart,p->sourceInfo.id,p->sourceInfo.statusFlags,p->sourceInfo.modeInfoIdx,
          (ULONG)p->targetInfo.adapterId.HighPart,p->targetInfo.adapterId.LowPart,p->targetInfo.id,p->targetInfo.targetAvailable,p->targetInfo.statusFlags,p->targetInfo.modeInfoIdx,
          (UINT)p->targetInfo.outputTechnology,(UINT)p->targetInfo.rotation,(UINT)p->targetInfo.scaling,p->targetInfo.refreshRate.Numerator,p->targetInfo.refreshRate.Denominator);
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {0};
        source.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;source.header.size=sizeof(source);source.header.adapterId=p->sourceInfo.adapterId;source.header.id=p->sourceInfo.id;
        LONG sr=DisplayConfigGetDeviceInfo(&source.header);
        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {0};
        target.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;target.header.size=sizeof(target);target.header.adapterId=p->targetInfo.adapterId;target.header.id=p->targetInfo.id;
        LONG tr=DisplayConfigGetDeviceInfo(&target.header);
        wprintf(L"PATH_NAMES index=%u source_status=%ld source=%ls target_status=%ld monitor=%ls device=%ls\n",i,sr,source.viewGdiDeviceName,tr,target.monitorFriendlyDeviceName,target.monitorDevicePath);
      }
      for (UINT32 i=0;i<mc;++i) {
        DISPLAYCONFIG_MODE_INFO *m=&modes[i];
        if(m->infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
          wprintf(L"SOURCE_MODE index=%u id=%u luid=%08lx:%08lx width=%u height=%u format=%u x=%ld y=%ld\n",i,m->id,(ULONG)m->adapterId.HighPart,m->adapterId.LowPart,m->sourceMode.width,m->sourceMode.height,(UINT)m->sourceMode.pixelFormat,m->sourceMode.position.x,m->sourceMode.position.y);
      }
    }
    free(paths);free(modes);
    if(result!=ERROR_INSUFFICIENT_BUFFER)return result;
  }
  return ERROR_INSUFFICIENT_BUFFER;
}
int wmain(void) {
  DWORD session=0;
  if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return 1;
  wprintf(L"CONSOLE_STATE pid=%lu session=%lu monitors=%d screen=%dx%d\n",GetCurrentProcessId(),session,GetSystemMetrics(SM_CMONITORS),GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
  if(session!=1)return 2;
  LONG result=DumpActivePaths();
  if(result!=ERROR_SUCCESS)return 3;
  DISPLAY_DEVICEW d={0};d.cb=sizeof(d);
  for(DWORD i=0;i<32 && EnumDisplayDevicesW(NULL,i,&d,0);++i){
    wprintf(L"GDI_DEVICE index=%lu name=%ls flags=0x%lx text=%ls id=%ls\n",i,d.DeviceName,d.StateFlags,d.DeviceString,d.DeviceID);
    ZeroMemory(&d,sizeof(d));d.cb=sizeof(d);
  }
  return 0;
}
