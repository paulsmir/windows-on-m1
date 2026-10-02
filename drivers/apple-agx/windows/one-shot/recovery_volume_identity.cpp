#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

static const GUID target_id = {0x54ba924f,0xc5e4,0x4898,{0x81,0x92,0x32,0x7d,0x5c,0xad,0x90,0x0e}};
static const GUID evidence_id = {0xf05b9952,0xee05,0x44e1,{0xbc,0xc4,0xb6,0x78,0xa8,0x50,0x1b,0x2a}};
static bool equal_id(const GUID &a,const GUID &b) { return memcmp(&a,&b,sizeof(a))==0; }
static bool target_matches(const PARTITION_INFORMATION_EX &p,const NTFS_VOLUME_DATA_BUFFER &n) {
  return p.PartitionStyle==PARTITION_STYLE_GPT && equal_id(p.Gpt.PartitionId,target_id) &&
      p.PartitionLength.QuadPart==118997647360LL &&
      (ULONGLONG)n.VolumeSerialNumber.QuadPart==0x6612cadc12caaffbULL &&
      n.BytesPerSector==4096 && n.NumberSectors.QuadPart==29052159LL;
}
static bool mount_exact(const wchar_t *letter,const wchar_t *volume) {
  wchar_t existing[MAX_PATH]={};
  if(GetVolumeNameForVolumeMountPointW(letter,existing,MAX_PATH))
    return _wcsicmp(existing,volume)==0;
  if(GetDriveTypeW(letter)!=DRIVE_NO_ROOT_DIR) {
    wprintf(L"REFUSE occupied mount point %ls\n",letter);return false;
  }
  if(!SetVolumeMountPointW(letter,volume)) {
    wprintf(L"SetVolumeMountPoint %ls %ls error=%lu\n",letter,volume,GetLastError());return false;
  }
  return true;
}
int wmain(int argc,wchar_t **argv) {
  setvbuf(stdout,NULL,_IONBF,0);
  if(argc==2 && wcscmp(argv[1],L"--self-test")==0) {
    PARTITION_INFORMATION_EX p={};NTFS_VOLUME_DATA_BUFFER n={};
    p.PartitionStyle=PARTITION_STYLE_GPT;p.Gpt.PartitionId=target_id;
    p.PartitionLength.QuadPart=118997647360LL;n.VolumeSerialNumber.QuadPart=0x6612cadc12caaffbLL;
    n.BytesPerSector=4096;n.NumberSectors.QuadPart=29052159LL;
    if(!target_matches(p,n))return 10;
    p.Gpt.PartitionId=evidence_id;if(target_matches(p,n))return 11;p.Gpt.PartitionId=target_id;
    n.VolumeSerialNumber.QuadPart^=1;if(target_matches(p,n))return 12;n.VolumeSerialNumber.QuadPart^=1;
    p.PartitionLength.QuadPart+=4096;if(target_matches(p,n))return 13;
    puts("EXACT_PARTITION_SERIAL_SIZE_SELF_TEST_PASS");return 0;
  }
  wchar_t system[MAX_PATH]={};GetEnvironmentVariableW(L"SystemDrive",system,MAX_PATH);
  if(_wcsicmp(system,L"X:")!=0){puts("REFUSE not WinPE X:");return 2;}
  wchar_t volume[MAX_PATH]={},target[MAX_PATH]={},evidence[MAX_PATH]={};
  unsigned targets=0,evidences=0;
  HANDLE search=FindFirstVolumeW(volume,MAX_PATH);
  if(search==INVALID_HANDLE_VALUE){printf("FindFirstVolume error=%lu\n",GetLastError());return 3;}
  do {
    wchar_t device[MAX_PATH]={};wcscpy_s(device,volume);
    size_t len=wcslen(device);if(!len || device[len-1]!=L'\\')continue;device[len-1]=0;
    HANDLE h=CreateFileW(device,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){wprintf(L"OPEN %ls error=%lu\n",volume,GetLastError());continue;}
    PARTITION_INFORMATION_EX p={};DWORD bytes=0;
    BOOL ok=DeviceIoControl(h,IOCTL_DISK_GET_PARTITION_INFO_EX,NULL,0,&p,sizeof(p),&bytes,NULL);
    if(ok && p.PartitionStyle==PARTITION_STYLE_GPT) {
      wprintf(L"VOLUME %ls GPT=%08lx-%04x-%04x size=%llu partition=%lu\n",volume,p.Gpt.PartitionId.Data1,p.Gpt.PartitionId.Data2,p.Gpt.PartitionId.Data3,(ULONGLONG)p.PartitionLength.QuadPart,p.PartitionNumber);
      if(equal_id(p.Gpt.PartitionId,target_id)) {
        NTFS_VOLUME_DATA_BUFFER n={};
        if(DeviceIoControl(h,FSCTL_GET_NTFS_VOLUME_DATA,NULL,0,&n,sizeof(n),&bytes,NULL) && target_matches(p,n)) {
          ++targets;wcscpy_s(target,volume);wprintf(L"TARGET_MATCH serial=%016llx\n",(ULONGLONG)n.VolumeSerialNumber.QuadPart);
        } else {wprintf(L"REFUSE target NTFS identity mismatch error=%lu\n",GetLastError());}
      }
      if(equal_id(p.Gpt.PartitionId,evidence_id)) {
        wchar_t fs[MAX_PATH]={};
        if(GetVolumeInformationW(volume,NULL,0,NULL,NULL,NULL,fs,MAX_PATH) && _wcsicmp(fs,L"FAT32")==0) {
          ++evidences;wcscpy_s(evidence,volume);
        }
      }
    } else {wprintf(L"PARTITION_QUERY %ls error=%lu\n",volume,GetLastError());}
    CloseHandle(h);
  } while(FindNextVolumeW(search,volume,MAX_PATH));
  DWORD enumerationError=GetLastError();FindVolumeClose(search);
  if(enumerationError!=ERROR_NO_MORE_FILES || targets!=1 || evidences!=1) {
    printf("REFUSE enumeration=%lu targets=%u evidence=%u\n",enumerationError,targets,evidences);return 4;
  }
  if(!mount_exact(L"T:\\",target) || !mount_exact(L"R:\\",evidence))return 5;
  puts("EXACT_GPT_NTFS_IDENTITY_AND_MOUNTS_PASS");return 0;
}
