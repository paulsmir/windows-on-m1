/* RC5 diagnostic-only VA selection. No allocation or residency policy. */
#ifndef ADMISSION_UMD_VA_WATCH_H
#define ADMISSION_UMD_VA_WATCH_H
static inline unsigned long long AdmissionUmdWatchParse(const char *text) {
  unsigned long long value=0;
  if(!text || text[0]!='0' || (text[1]!='x' && text[1]!='X') || !text[2]) return 0;
  for(unsigned i=2;text[i];++i) {
    unsigned digit=text[i]>='0' && text[i]<='9' ? text[i]-'0' :
        text[i]>='a' && text[i]<='f' ? text[i]-'a'+10 :
        text[i]>='A' && text[i]<='F' ? text[i]-'A'+10 : 16;
    if(digit>15 || value>(~0ull-digit)/16) return 0;
    value=value*16+digit;
  }
  return value;
}
static inline int AdmissionUmdWatchContains(unsigned long long watch,
    unsigned long long va,unsigned long long bytes) {
  return watch && va && watch>=va && watch-va<bytes;
}
#ifdef ADMISSION_UMD_WATCH_WINDOWS
/* Process-start configuration; C++ local initialization is thread safe. */
static inline unsigned long long AdmissionUmdWatchVa(void) {
  static const unsigned long long watch=[]() {
    char text[32]={};
    const auto saved=GetLastError();
    unsigned n=GetEnvironmentVariableA("APPLE_AGX_UMD_WATCH_VA",text,sizeof(text));
    SetLastError(saved);
    return n && n<sizeof(text) ? AdmissionUmdWatchParse(text) : 0ull;
  }();
  return watch;
}
#endif
#endif
