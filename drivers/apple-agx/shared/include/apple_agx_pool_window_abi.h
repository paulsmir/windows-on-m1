#ifndef APPLE_AGX_POOL_WINDOW_ABI_H
#define APPLE_AGX_POOL_WINDOW_ABI_H

/* M2-f2: the hypervisor presents driver-allocated guest RAM chunks as one
 * contiguous guest-IPA window, the CPU base of the KMD's segment 3. The
 * registers live in the AGX power-broker page (HV_AGX_G2_POWER_BROKER_BASE).
 * 32-bit registers take 32-bit accesses, 64-bit registers 64-bit accesses. */
#define AGX_POOL_WINDOW_OFFSET 0xe00u
#define AGX_POOL_WINDOW_BYTES 0x40u

#define AGX_POOL_WINDOW_REG_MAGIC 0x00u        /* u32 'AGXW' */
#define AGX_POOL_WINDOW_REG_VERSION 0x04u      /* u32 */
#define AGX_POOL_WINDOW_REG_STATUS 0x08u       /* u32, bit 0: window available */
#define AGX_POOL_WINDOW_REG_IPA 0x10u          /* u64, window base IPA */
#define AGX_POOL_WINDOW_REG_CAPACITY 0x18u     /* u64, window bytes */
#define AGX_POOL_WINDOW_REG_MAPPED 0x20u       /* u64, bytes mapped from the base */
#define AGX_POOL_WINDOW_REG_SOURCE_IPA 0x28u   /* u64, write: first source page */
#define AGX_POOL_WINDOW_REG_SOURCE_BYTES 0x30u /* u64, write: bytes to append */
#define AGX_POOL_WINDOW_REG_COMMAND 0x38u      /* u32, write */
#define AGX_POOL_WINDOW_REG_RESULT 0x3cu       /* u32, last command's result */

#define AGX_POOL_WINDOW_MAGIC 0x57584741u /* "AGXW" */
#define AGX_POOL_WINDOW_VERSION 1u
#define AGX_POOL_WINDOW_STATUS_AVAILABLE 1u
#define AGX_POOL_WINDOW_PAGE 0x4000ull

/* MAP_APPEND maps window [MAPPED, MAPPED + SOURCE_BYTES) onto the guest pages
 * SOURCE_IPA.. (all or nothing). UNMAP_ALL invalidates every window page and
 * the stage-2 TLBs; it is refused while the GPU broker holds a backing for a
 * window page. */
#define AGX_POOL_WINDOW_CMD_MAP_APPEND 1u
#define AGX_POOL_WINDOW_CMD_UNMAP_ALL 2u

enum AGX_POOL_WINDOW_RESULT {
  AgxPoolWindowOk = 0,
  AgxPoolWindowInvalid = 1,   /* alignment, zero length, unknown command */
  AgxPoolWindowState = 2,     /* no window */
  AgxPoolWindowCapacity = 3,  /* past the window end */
  AgxPoolWindowPage = 4,      /* a source page is not plain guest RAM */
  AgxPoolWindowDuplicate = 5, /* a source page is already in the window */
  AgxPoolWindowBusy = 6,      /* the GPU broker holds a backing for the page */
};

#endif
