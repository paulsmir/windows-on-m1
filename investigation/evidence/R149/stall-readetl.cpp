#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <stdio.h>
#include <map>
#include <string>
static unsigned long long count=0,exts=0,stacks=0,errors=0,userframes=0,kernelframes=0;
static std::map<std::string,unsigned long long> providers;
static std::map<unsigned,unsigned long long> ids;
static GUID dxg={0x802ec45a,0x1e99,0x4b83,{0x99,0x20,0x87,0xc9,0x82,0x77,0xba,0x9d}};
static void WINAPI record(PEVENT_RECORD e) {
 ++count;
 auto &g=e->EventHeader.ProviderId;
 char guid[80]; sprintf_s(guid,"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);
 providers[guid]++;
 bool isdxg=IsEqualGUID(g,dxg)!=0;
 if(isdxg) ids[e->EventHeader.EventDescriptor.Id]++;
 bool pc=false;
 for(unsigned i=0;i<e->ExtendedDataCount;++i) { auto &x=e->ExtendedData[i];
  if(x.ExtType==EVENT_HEADER_EXT_TYPE_STACK_TRACE64) { auto *v=(ULONGLONG*)(ULONG_PTR)x.DataPtr;
   for(unsigned j=1;j<x.DataSize/8;++j) { if(v[j] && v[j]<0x800000000000ULL) ++userframes; else if(v[j]>=0xffff000000000000ULL) ++kernelframes; if(v[j]>=0x7ff8eb980000ULL && v[j]<0x7ff8eb990000ULL) pc=true; }
  }
 }
 bool err=(isdxg && e->EventHeader.EventDescriptor.Id==467)||pc;
 // FILETIME for first error: 2026-09-27T09:40:02.4391858Z.
 const LONGLONG first=134350434414443515LL;
 bool inwindow=false; (void)first;
 if(err) ++errors;
 if(e->ExtendedDataCount) ++exts;
 if(err || inwindow) {
  printf("EVENT seq=%llu id=%u pid=%lu tid=%lu filetime=%lld ext=%u bytes=%u data=",count,e->EventHeader.EventDescriptor.Id,e->EventHeader.ProcessId,e->EventHeader.ThreadId,e->EventHeader.TimeStamp.QuadPart,e->ExtendedDataCount,e->UserDataLength);
  for(unsigned j=0;j<e->UserDataLength;j++) printf("%02x",((unsigned char*)e->UserData)[j]);
  puts("");
 }
 for(unsigned i=0;i<e->ExtendedDataCount;i++) {
  auto &x=e->ExtendedData[i];
  if(x.ExtType==EVENT_HEADER_EXT_TYPE_STACK_TRACE64 || x.ExtType==EVENT_HEADER_EXT_TYPE_STACK_TRACE32) ++stacks;
  if(err || inwindow) {
   printf("EXT type=%u size=%u data=",x.ExtType,x.DataSize);
   if(x.ExtType==EVENT_HEADER_EXT_TYPE_STACK_TRACE64) {
    auto *p=(ULONGLONG*)(ULONG_PTR)x.DataPtr;
    for(unsigned j=0;j<x.DataSize/8;j++) printf("%016llx,",p[j]);
   } else {
    auto *p=(unsigned char*)(ULONG_PTR)x.DataPtr;
    for(unsigned j=0;j<x.DataSize;j++) printf("%02x",p[j]);
   }
   puts("");
  }
 }
}
int wmain(int argc,wchar_t **argv) {
 if(argc!=2) return 2;
 EVENT_TRACE_LOGFILEW l={}; l.LogFileName=argv[1];
 l.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD; l.EventRecordCallback=record;
 TRACEHANDLE h=OpenTraceW(&l);
 if(h==INVALID_PROCESSTRACE_HANDLE) {printf("OpenTrace error=%lu\n",GetLastError());return 3;}
 printf("HEADER lost=%lu bufferslost=%lu start=%lld end=%lld pointer=%lu build=%lu\n",l.LogfileHeader.EventsLost,l.LogfileHeader.BuffersLost,l.LogfileHeader.StartTime.QuadPart,l.LogfileHeader.EndTime.QuadPart,l.LogfileHeader.PointerSize,l.LogfileHeader.VersionDetail.SubVersion);
 ULONG s=ProcessTrace(&h,1,NULL,NULL); CloseTrace(h);
 printf("SUMMARY status=%lu events=%llu extended=%llu stacks=%llu reason_events=%llu\n",s,count,exts,stacks,errors);
 printf("FRAMES user=%llu kernel=%llu\n",userframes,kernelframes);
 for(auto &p:providers) printf("PROVIDER %s %llu\n",p.first.c_str(),p.second);
 for(auto &p:ids) printf("ID %u %llu\n",p.first,p.second);
 return s?4:0;
}
