/* MIT: bounded CPU wait attribution. IDs are a versioned trace ABI. */
#ifndef AGX_WIN32_WAIT_H
#define AGX_WIN32_WAIT_H

enum AgxWaitSite {
  AgxWaitCpuFence=1, AgxWaitPagingFence, AgxWaitRenderFence,
  AgxWaitMakeResident, AgxWaitAllocate, AgxWaitLock, AgxWaitEscape,
  AgxWaitCopyEscape, AgxWaitSubmit, AgxWaitInflightSlot, AgxWaitBatchPoll,
  AgxWaitBatchSlot, AgxWaitSyncWriter, AgxWaitSyncReaders, AgxWaitFlush,
  AgxWaitStagingBlit, AgxWaitTransferMap, AgxWaitQueryCreate,
  AgxWaitQueryCommand, AgxWaitQueryIncrement, AgxWaitContextRetire,
  AgxWaitOwnerLock, AgxWaitEvent, AgxWaitQueryWriters, AgxWaitBoCreate,
  AgxWaitCountMap, AgxWaitSceneEscape, AgxWaitPoolMapEscape,
  AgxWaitNativeReadyEscape, AgxWaitCompletionEscape, AgxWaitTimestampEscape,
  AgxWaitFrameArmEscape, AgxWaitAllocateCanonical, AgxWaitLockHash,
  AgxWaitLockTransfer, AgxWaitEvict, AgxWaitMapGpuVa, AgxWaitReserveGpuVa,
  AgxWaitFreeGpuVa,
  AgxWaitSiteCount
};
enum AgxWaitPhase {
  AgxWaitPhaseOther, AgxWaitPhaseQueryBegin, AgxWaitPhaseQueryEnd,
  AgxWaitPhaseQueryResolve, AgxWaitPhaseDraw, AgxWaitPhaseDispatch,
  AgxWaitPhaseSubmit, AgxWaitPhaseCount
};
#ifdef __cplusplus
extern "C" {
#endif
/* Defined in the native BO adapter, also linked by GL. No hook means no clock.
 * The D3D12 owner installs the thread-local UMD collector once at creation. */
extern unsigned (*AgxWin32WaitBeginHook)(unsigned site);
extern void (*AgxWin32WaitEndHook)(unsigned token);
unsigned AdmissionUmdWaitBegin(unsigned site);
void AdmissionUmdWaitEnd(unsigned token);
int AdmissionUmdWaitExecuteBegin(void);
unsigned AdmissionUmdWaitSerial(void);
void AdmissionUmdWaitExecuteEnd(long status);
void AdmissionUmdWaitSetPhase(unsigned phase);
#ifdef __cplusplus
}
#endif
static inline unsigned AgxWin32WaitBegin(unsigned site) {
  return AgxWin32WaitBeginHook ? AgxWin32WaitBeginHook(site) : 0;
}
static inline void AgxWin32WaitEnd(unsigned token) {
  if (token && AgxWin32WaitEndHook) AgxWin32WaitEndHook(token);
}
#ifdef __cplusplus
struct AgxUmdWaitScope {
  unsigned Token;
  explicit AgxUmdWaitScope(unsigned site) : Token(AdmissionUmdWaitBegin(site)) {}
  ~AgxUmdWaitScope() { AdmissionUmdWaitEnd(Token); }
  AgxUmdWaitScope(const AgxUmdWaitScope &) = delete;
  AgxUmdWaitScope &operator=(const AgxUmdWaitScope &) = delete;
};
#endif
#endif
