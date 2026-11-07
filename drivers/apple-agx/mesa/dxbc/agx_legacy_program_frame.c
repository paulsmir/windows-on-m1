/* Original explicit-span legacy PROGRAM framing. SPDX-License-Identifier: MIT */
#include "agx_legacy_program_frame.h"
#include <string.h>
static uint32_t read32(const unsigned char *p){
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
enum agx_legacy_frame_status agx_legacy_frame_program(
 const void *input,size_t accessible_bytes,const struct agx_legacy_frame_profile *profile,
 struct agx_legacy_program_frame *output){
 if(!output)return AGX_LEGACY_FRAME_INVALID;
 memset(output,0,sizeof(*output));
 if(!input||!profile||accessible_bytes<8||!profile->stage_mask||
    (profile->stage_mask&~UINT32_C(0x3f))||!profile->model_mask||
    (profile->model_mask&~UINT32_C(0xf)))return AGX_LEGACY_FRAME_INVALID;
 const unsigned char *p=input;const uint32_t version=read32(p),words=read32(p+4);
 /* A DXBC archive is an explicitly different framing boundary. There is no
  * equivalent positive magic requirement for the legacy token stream. */
 if(version==UINT32_C(0x43425844))return AGX_LEGACY_FRAME_INVALID;
 /* words <= accessible_bytes/4 implies words*4 <= accessible_bytes <= SIZE_MAX
  * on both 32/64-bit size_t. Keep this check BEFORE the only multiplication. */
 if(words<2||words>accessible_bytes/4)return AGX_LEGACY_FRAME_INVALID;
 const uint32_t stage=version>>16;const unsigned major=(version>>4)&15u,minor=version&15u;
 unsigned model=0;
 if(major==4&&minor==0)model=AGX_LEGACY_SM40;
 else if(major==4&&minor==1)model=AGX_LEGACY_SM41;
 else if(major==5&&minor==0)model=AGX_LEGACY_SM50;
 else if(major==5&&minor==1)model=AGX_LEGACY_SM51;
 if(stage>=6||!(profile->stage_mask&(UINT32_C(1)<<stage))||!model||
    !(profile->model_mask&model))return AGX_LEGACY_FRAME_UNSUPPORTED;
 /* Byte-sized output fields are assigned only after the full framing check.
  * Bits15:8 are not interpreted: primary decoder macros/current parser ignore
  * them; no undocumented reserved-bit rejection is added here. */
 struct agx_legacy_program_frame framed;memset(&framed,0,sizeof(framed));
 framed.program_bytes=(size_t)words*4;framed.words=words;framed.version=version;
 framed.stage=stage;framed.major=(uint8_t)major;framed.minor=(uint8_t)minor;
 *output=framed;return AGX_LEGACY_FRAME_OK;
}
