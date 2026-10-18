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
// Temporary aliases are sufficient for this RAM-boot recovery. Resolve the
// enumerated volume through the object namespace, not persistent Mount Manager.
static bool alias_exact(const wchar_t *letter,const wchar_t *native,bool *created) {
  *created=false;
  wchar_t existing[1024]={};
  if(QueryDosDeviceW(letter,existing,1024)) {
    if(_wcsicmp(existing,native)==0)return true;
    wprintf(L"REFUSE occupied alias %ls -> %ls\n",letter,existing);return false;
  }
  DWORD error=GetLastError();
  if(error!=ERROR_FILE_NOT_FOUND) {
    wprintf(L"QueryDosDevice %ls error=%lu\n",letter,error);return false;
  }
  if(!DefineDosDeviceW(DDD_RAW_TARGET_PATH|DDD_NO_BROADCAST_SYSTEM,letter,native)) {
    wprintf(L"DefineDosDevice %ls error=%lu\n",letter,GetLastError());return false;
  }
  *created=true;
  bool ok=QueryDosDeviceW(letter,existing,1024) && _wcsicmp(existing,native)==0;
  if(!ok) {
    DefineDosDeviceW(DDD_RAW_TARGET_PATH|DDD_NO_BROADCAST_SYSTEM|
                    DDD_REMOVE_DEFINITION|DDD_EXACT_MATCH_ON_REMOVE,letter,native);
    *created=false;
  }
  return ok;
}
static bool mount_exact(const wchar_t *letter,const wchar_t *volume) {
  wchar_t name[MAX_PATH]={},native[1024]={};
  size_t len=wcslen(volume);
  if(len<6 || wcsncmp(volume,L"\\\\?\\",4)!=0 || volume[len-1]!=L'\\')return false;
  wcscpy_s(name,volume+4);name[wcslen(name)-1]=0;
  if(!QueryDosDeviceW(name,native,1024)) {
    wprintf(L"QueryDosDevice %ls error=%lu\n",name,GetLastError());return false;
  }
  bool created=false;
  if(!alias_exact(letter,native,&created))return false;
  wprintf(L"VERIFIED_TEMP_ALIAS %ls -> %ls source=%ls created=%d\n",letter,native,volume,created);
  return true;
}
static int alias_self_test() {
  wchar_t system[4]={},native[1024]={},letter[3]={L'Z',L':',0},check[1024]={};
  if(!GetEnvironmentVariableW(L"SystemDrive",system,4) ||
     !QueryDosDeviceW(system,native,1024))return 20;
  for(;letter[0]>=L'D';--letter[0]) {
    if(!QueryDosDeviceW(letter,check,1024) && GetLastError()==ERROR_FILE_NOT_FOUND)break;
  }
  if(letter[0]<L'D')return 21;
  bool created=false;
  if(!alias_exact(letter,native,&created) || !created)return 22;
  bool reused=false;
  bool same=alias_exact(letter,native,&reused) && !reused;
  bool refused=!alias_exact(letter,L"\\Device\\EXP931WrongTarget",&reused);
  bool preserved=QueryDosDeviceW(letter,check,1024) && _wcsicmp(check,native)==0;
  wchar_t root[4]={letter[0],L':',L'\\',0},systemRoot[4]={system[0],L':',L'\\',0};
  DWORD serial1=0,serial2=0;
  bool readable=GetVolumeInformationW(root,NULL,0,&serial1,NULL,NULL,NULL,0) &&
      GetVolumeInformationW(systemRoot,NULL,0,&serial2,NULL,NULL,NULL,0) && serial1==serial2;
  bool removed=DefineDosDeviceW(DDD_RAW_TARGET_PATH|DDD_NO_BROADCAST_SYSTEM|
      DDD_REMOVE_DEFINITION|DDD_EXACT_MATCH_ON_REMOVE,letter,native)!=0;
  bool absent=!QueryDosDeviceW(letter,check,1024) && GetLastError()==ERROR_FILE_NOT_FOUND;
  if(!same || !refused || !preserved || !readable || !removed || !absent)return 23;
  puts("TEMP_ALIAS_CREATE_READ_REUSE_COLLISION_REFUSAL_EXACT_REMOVE_PASS");return 0;
}
static int compare_files(const wchar_t *first,const wchar_t *second) {
  HANDLE a=CreateFileW(first,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,NULL);
  if(a==INVALID_HANDLE_VALUE){printf("COMPARE_OPEN_FIRST_ERROR=%lu\n",GetLastError());return 30;}
  HANDLE b=CreateFileW(second,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,NULL);
  if(b==INVALID_HANDLE_VALUE){printf("COMPARE_OPEN_SECOND_ERROR=%lu\n",GetLastError());CloseHandle(a);return 31;}
  unsigned char x[8192],y[8192];DWORD nx=0,ny=0;ULONGLONG total=0;int result=0;
  for(;;) {
    if(!ReadFile(a,x,sizeof(x),&nx,NULL) || !ReadFile(b,y,sizeof(y),&ny,NULL)) {
      printf("COMPARE_READ_ERROR=%lu\n",GetLastError());result=32;break;
    }
    if(nx!=ny || memcmp(x,y,nx)!=0){printf("COMPARE_MISMATCH_OFFSET=%llu\n",total);result=33;break;}
    total+=nx;if(nx==0)break;
  }
  CloseHandle(b);CloseHandle(a);
  if(result==0)printf("BYTE_IDENTICAL_BACKUP_PASS bytes=%llu\n",total);
  return result;
}
int wmain(int argc,wchar_t **argv) {
  setvbuf(stdout,NULL,_IONBF,0);
  if(argc==4 && wcscmp(argv[1],L"--compare-files")==0)return compare_files(argv[2],argv[3]);
  if(argc==2 && wcscmp(argv[1],L"--alias-self-test")==0)return alias_self_test();
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
  if(!mount_exact(L"T:",target) || !mount_exact(L"R:",evidence))return 5;
  puts("EXACT_GPT_NTFS_IDENTITY_AND_MOUNTS_PASS");return 0;
}
