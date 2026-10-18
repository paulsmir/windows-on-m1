#ifndef APPLE_AGX_RENDER_WOW64_H
#define APPLE_AGX_RENDER_WOW64_H

/* DXGKQAITYPE_64BITONLYCAPS (EXP1225). dxgkrnl reads the answer once at
 * adapter start and leaves a 64-bit-only adapter out of every WOW64
 * process's adapter enumeration. The adapter supports 32-bit processes
 * exactly when the package registered a WOW64 user-mode driver: the INF's
 * HKR UserModeDriverNameWow value in the driver key, the file name the
 * 32-bit D3D runtime loads from %systemroot%\SysWOW64. */

#define ADMISSION_REG_SZ 1u
#define ADMISSION_REG_MULTI_SZ 7u

/* 1 when Data (Bytes bytes of registry value data of type Type) holds a
 * REG_SZ or REG_MULTI_SZ whose first string is non-empty and terminated
 * inside the data; 0 for anything else, so a missing or malformed
 * registration keeps the adapter 64-bit-only. */
int AdmissionWow64UmdNamed(unsigned int Type, const void *Data, unsigned int Bytes);

/* DXGK_64_BIT_ONLY_CAPS.SupportsOnly64Bit for that reading. */
unsigned int AdmissionWow64Only64Bit(int Wow64UmdNamed);

#endif
