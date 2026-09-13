#ifndef AGX_KMT_NATIVE_QUALIFICATION_H
#define AGX_KMT_NATIVE_QUALIFICATION_H
#include <windows.h>
#include <d3dkmthk.h>
/* 0: verified and closed; 1: failed and safely closed; 2: retain process and
 * borrowed handles because completion, evidence collection or cleanup is pending. */
int AgxKmtNativeQualificationRun(D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device,
    D3DKMT_HANDLE PagingQueue, volatile const UINT64 *PagingFence, UINT ExpectedCandidateBuild);
#if defined(AGX_KMT_NATIVE_QUALIFICATION_TEST)
/* Pure matching regression in the existing UmdContractTest; no registry/KMT calls. */
unsigned AgxKmtNativeQualificationFreshnessContractTest(void);
#endif
#endif
