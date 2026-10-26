/* Weak definitions allow multi-TU host replays to use this forced include.
 * Existing semantic replays do not opt into diagnostic collection. */
#ifndef D3D12_WAIT_DISABLED_FIXTURE_H
#define D3D12_WAIT_DISABLED_FIXTURE_H
#include "../drivers/apple-agx/mesa/winsys/agx_win32_wait.h"
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((weak)) unsigned (*AgxWin32WaitBeginHook)(unsigned);
__attribute__((weak)) void (*AgxWin32WaitEndHook)(unsigned);
__attribute__((weak)) unsigned AdmissionUmdWaitBegin(unsigned site) { (void)site; return 0; }
__attribute__((weak)) void AdmissionUmdWaitEnd(unsigned token) { (void)token; }
__attribute__((weak)) void AdmissionUmdWaitSetPhase(unsigned phase) { (void)phase; }
#ifdef __cplusplus
}
#endif
#endif
