#ifndef APPLE_AGX_RENDER_NATIVE_STAGING_H
#define APPLE_AGX_RENDER_NATIVE_STAGING_H

/* WDK types are supplied by the including driver header. Opaque bindings are
 * held by KMD allocation objects; neither kernel pointers nor MDLs cross ABI. */
struct _ADMISSION_CONTEXT;
#ifdef __cplusplus
extern "C" {
#endif
NTSTATUS AdmissionNativeStagingInitialize(void);
/* Final driver unload only: normal Stop must first prove AdapterIdle. An
 * unexpected live owner loses its CPU view/callback but retains GPU backing. */
VOID AdmissionNativeStagingUninitialize(void);
NTSTATUS AdmissionNativeStagingMap(struct _ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess, ULONG Bytes, PULONGLONG Token, PULONGLONG UserVa);
/* Cancellation is only for a ring that has never had a WDDM binding. */
NTSTATUS AdmissionNativeStagingCancel(struct _ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess, ULONGLONG Token);
BOOLEAN AdmissionNativeStagingBind(struct _ADMISSION_CONTEXT *Adapter,
    ULONGLONG Token, ULONGLONG Offset, ULONGLONG Bytes, PVOID *Binding);
VOID AdmissionNativeStagingUnbind(PVOID Binding);
BOOLEAN AdmissionNativeStagingPage(PVOID Binding,
    ULONGLONG AllocationOffset, ULONGLONG Ipa);
/* Inverse of Page: the offset of Ipa inside this binding's own slice, or
 * FALSE for every page that is not one of its slice pages. */
BOOLEAN AdmissionNativeStagingLocate(PVOID Binding, ULONGLONG Ipa,
    PULONGLONG AllocationOffset);
/* Receipt-only, like Page: the authenticated page at AllocationOffset and
 * the ring offset whose page is Ipa (ADMISSION_NATIVE_PTE_NONE if absent). */
VOID AdmissionNativeStagingDescribe(PVOID Binding, ULONGLONG AllocationOffset,
    ULONGLONG Ipa, PULONGLONG ExpectedIpa, PULONGLONG BindingOffset,
    PULONGLONG BindingBytes, PULONGLONG RingOffset);
BOOLEAN AdmissionNativeStagingOwns(PVOID Binding,
    struct _ADMISSION_CONTEXT *Adapter, PVOID GpuProcess);
VOID AdmissionNativeStagingUnmapProcess(struct _ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess);
BOOLEAN AdmissionNativeStagingProcessIdle(struct _ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess);
BOOLEAN AdmissionNativeStagingAdapterIdle(struct _ADMISSION_CONTEXT *Adapter);
/* Caller has acknowledged graph retirement after ProcessIdle returned TRUE.
 * New map/bind admission must already be stopped for this KMD process. */
VOID AdmissionNativeStagingDestroyProcess(struct _ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess);

BOOLEAN AdmissionMemoryPoolChargeNative(struct _ADMISSION_CONTEXT *Adapter,
    ULONGLONG Bytes);
VOID AdmissionMemoryPoolUnchargeNative(struct _ADMISSION_CONTEXT *Adapter,
    ULONGLONG Bytes);
#ifdef __cplusplus
}
#endif
#endif
