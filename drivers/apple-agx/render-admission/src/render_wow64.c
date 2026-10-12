#include "render_wow64.h"

int AdmissionWow64UmdNamed(unsigned int Type, const void *Data, unsigned int Bytes) {
  const unsigned char *bytes = (const unsigned char *)Data;
  unsigned int characters, index;
  if ((Type != ADMISSION_REG_SZ && Type != ADMISSION_REG_MULTI_SZ) || bytes == 0 ||
      Bytes < 4u || (Bytes & 1u) != 0u)
    return 0;
  /* Registry strings are UTF-16LE; the data need not be aligned. */
  characters = Bytes / 2u;
  if ((bytes[0] | bytes[1]) == 0u)
    return 0;
  for (index = 1u; index < characters; ++index) {
    if ((bytes[2u * index] | bytes[2u * index + 1u]) == 0u)
      return 1;
  }
  return 0;
}

unsigned int AdmissionWow64Only64Bit(int Wow64UmdNamed) {
  return Wow64UmdNamed ? 0u : 1u;
}
