/* SPDX-License-Identifier: MIT
 * Native command framing follows the Asahi DRM UAPI:
 * Copyright (C) The Asahi Linux Contributors
 * Copyright (C) 2018-2023 Collabora Ltd.
 * Copyright (C) 2014-2018 Broadcom
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software. THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef APPLE_AGX_G4_SUBMIT_H
#define APPLE_AGX_G4_SUBMIT_H

#define APPLE_AGX_G4_PRIVATE_MAGIC 0x34584741u /* "AGX4" */
#define APPLE_AGX_G4_PRIVATE_VERSION 1u
#define APPLE_AGX_G4_NATIVE_MAX_BYTES 4096u
#define APPLE_AGX_G4_RENDER 0u
#define APPLE_AGX_G4_COMPUTE 1u
#define APPLE_AGX_G4_FRAGMENT_ATTACHMENTS 3u

typedef struct {
  unsigned int Magic;
  unsigned short Version;
  unsigned short HeaderBytes;
  unsigned int CommandBytes;
  unsigned int Reserved;
  unsigned long long CommandVa;
} APPLE_AGX_G4_PRIVATE_HEADER;

typedef struct {
  unsigned short Type;
  unsigned short Size;
  unsigned short VdmBarrier;
  unsigned short CdmBarrier;
} APPLE_AGX_G4_NATIVE_HEADER;

typedef struct {
  unsigned long long Pointer;
  unsigned long long Size;
  unsigned int Pad;
  unsigned int Flags;
} APPLE_AGX_G4_ATTACHMENT;

typedef enum {
  AppleAgxG4ParseOk = 0,
  AppleAgxG4ParseInvalid,
  AppleAgxG4ParseUnsupported,
  AppleAgxG4ParseUnmapped
} APPLE_AGX_G4_PARSE_RESULT;

typedef int (*APPLE_AGX_G4_ACCESS)(void *Context, unsigned long long GpuVa,
                                   unsigned int Bytes, int Write);

/* Parsing proves envelope framing and attachment access only. The render
 * payload's native VA fields still need a separate validator before any GPU
 * access or success return from SubmitCommandVirtual. */
typedef struct {
  const unsigned char *Native;
  const unsigned char *Render;
  const APPLE_AGX_G4_ATTACHMENT *Attachments;
  unsigned int CommandBytes;
  unsigned int RenderBytes;
  unsigned int AttachmentCount;
  unsigned long long CommandVa;
} APPLE_AGX_G4_SUBMIT_VIEW;

APPLE_AGX_G4_PARSE_RESULT AppleAgxG4ParseSubmit(
    const void *PrivateData, unsigned int PrivateCapacity,
    unsigned int UmdPrivateBytes, unsigned long long DmaVa,
    unsigned int DmaBytes, APPLE_AGX_G4_ACCESS Access, void *AccessContext,
    APPLE_AGX_G4_SUBMIT_VIEW *View);

#endif
