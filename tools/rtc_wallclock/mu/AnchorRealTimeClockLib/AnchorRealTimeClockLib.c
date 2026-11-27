/** @file
 *
 *  RealTimeClockLib for Apple silicon: wall clock from a UTC anchor handed over in the ADT, read-only.
 *
 *  SPDX-License-Identifier: MIT
 *
 *  Plan row B20 (investigation/MU_RTC_WALLCLOCK_DESIGN.md). At LibRtcInitialize (boot time, ADT still mapped) the
 *  /chosen anchors are copied into this runtime image:
 *    host anchor     m1n1-iomfb-utc-ms / -cntpct / -cntfrq  (proxyclient tools/chainload.py, host NTP time)
 *    hardware anchor m1n1-rtc-utc-ms   / -cntpct / -cntfrq  (m1n1 stage 1: SMC CLKM + PMU rtc_offset)
 *  The host anchor wins when both exist (Architect ruling Q1); the log then prints hardware minus host, which is the
 *  RTC error of this machine at every boot. LibGetTime = anchor + physical generic-timer elapsed time; no ADT, SMC or
 *  SPMI access at runtime. Without a valid anchor the behaviour is the previous virtual clock (2019-01 + uptime) and
 *  the boot log says so. LibSetTime stays EFI_UNSUPPORTED (stage 2 needs the owner's ruling).
 **/

#include <PiDxe.h>

#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/AppleDTLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/TimerLib.h>

#include "rtc_wallclock.h"

typedef struct {
  BOOLEAN   Valid;
  INT64     UtcMs;
  UINT64    Counter;
  UINT64    Frequency;
} RTC_ANCHOR;

STATIC EFI_TIME    BaseTime;
STATIC RTC_ANCHOR  mAnchor;                 // the one LibGetTime uses
STATIC CONST CHAR8 *mSource = "virtual";

// 2020-01-01 .. 2100-01-01 (plausibility; the RTC format itself spans 1834 .. 2106)
#define RTC_ANCHOR_MIN_MS  1577836800000LL
#define RTC_ANCHOR_MAX_MS  4102444800000LL

STATIC
CONST CHAR8 *
ReadAnchor (
  IN  dt_node_t  *Chosen,
  IN  CONST CHAR8 *MsName,
  IN  CONST CHAR8 *CntName,
  IN  CONST CHAR8 *FrqName,
  OUT RTC_ANCHOR *Anchor
  )
{
  size_t  Len;
  UINT64  *Ms, *Cnt, *Frq;

  Anchor->Valid = FALSE;
  if (Chosen == NULL) {
    return "no /chosen";
  }
  Ms  = dt_prop (Chosen, MsName, &Len);
  if ((Ms == NULL) || (Len != 8)) {
    return "absent";
  }
  Cnt = dt_prop (Chosen, CntName, &Len);
  if ((Cnt == NULL) || (Len != 8)) {
    return "counter missing";
  }
  Frq = dt_prop (Chosen, FrqName, &Len);
  if ((Frq == NULL) || (Len != 8)) {
    return "frequency missing";
  }
  if ((*Frq == 0) || (*Frq != ArmGenericTimerGetTimerFreq ())) {
    return "frequency differs from CNTFRQ";
  }
  if (((INT64)*Ms < RTC_ANCHOR_MIN_MS) || ((INT64)*Ms > RTC_ANCHOR_MAX_MS)) {
    return "time implausible";
  }
  if (*Cnt > GetPerformanceCounter ()) {
    return "counter in the future";
  }
  Anchor->UtcMs     = (INT64)*Ms;
  Anchor->Counter   = *Cnt;
  Anchor->Frequency = *Frq;
  Anchor->Valid     = TRUE;
  return "ok";
}

EFI_STATUS
EFIAPI
LibGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities
  )
{
  UINT32               Freq = ArmGenericTimerGetTimerFreq ();
  struct rtc_wc_time   Civil;
  UINT64               ElapsedSeconds, ElapsedDays;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Freq == 0) {
    return EFI_DEVICE_ERROR;
  }
  if (Capabilities != NULL) {
    Capabilities->Accuracy   = 0;
    Capabilities->Resolution = Freq;
    Capabilities->SetsToZero = FALSE;
  }

  if (mAnchor.Valid &&
      (rtc_wc_to_civil (rtc_wc_now_ms (mAnchor.UtcMs, mAnchor.Counter, GetPerformanceCounter (), mAnchor.Frequency),
                        &Civil) == 0))
  {
    Time->Year       = Civil.year;
    Time->Month      = Civil.month;
    Time->Day        = Civil.day;
    Time->Hour       = Civil.hour;
    Time->Minute     = Civil.minute;
    Time->Second     = Civil.second;
    Time->Nanosecond = Civil.nanosecond;
    Time->TimeZone   = 0;                     // UTC (minutes from UTC; not EFI_UNSPECIFIED_TIMEZONE)
    Time->Daylight   = 0;
    return EFI_SUCCESS;
  }

  // Fallback: the previous virtual clock (base month + uptime), unchanged behaviour.
  ElapsedSeconds   = GetPerformanceCounter () / Freq;
  ElapsedDays      = ElapsedSeconds / (24 * 60 * 60);
  Time->Year       = BaseTime.Year;
  Time->Month      = BaseTime.Month;
  Time->Day        = (UINT8)(1 + (ElapsedDays % 28));
  ElapsedSeconds  %= 24 * 60 * 60;
  Time->Hour       = (UINT8)(ElapsedSeconds / 3600);
  ElapsedSeconds  %= 3600;
  Time->Minute     = (UINT8)(ElapsedSeconds / 60);
  Time->Second     = (UINT8)(ElapsedSeconds % 60);
  Time->Nanosecond = 0;
  Time->TimeZone   = 0;
  Time->Daylight   = 0;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
LibSetTime (
  IN EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;                     // read-only by design (stage 2 needs the owner)
}

EFI_STATUS
EFIAPI
LibGetWakeupTime (
  OUT BOOLEAN   *Enabled,
  OUT BOOLEAN   *Pending,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
LibSetWakeupTime (
  IN BOOLEAN    Enabled,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
LibRtcInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  dt_node_t    *Chosen;
  RTC_ANCHOR   Host, Hw;
  CONST CHAR8  *HostWhy, *HwWhy;

  BaseTime.Year  = 2019;
  BaseTime.Month = 1;

  Chosen  = dt_get ("chosen");
  HostWhy = ReadAnchor (Chosen, "m1n1-iomfb-utc-ms", "m1n1-iomfb-utc-cntpct", "m1n1-iomfb-utc-cntfrq", &Host);
  HwWhy   = ReadAnchor (Chosen, "m1n1-rtc-utc-ms", "m1n1-rtc-utc-cntpct", "m1n1-rtc-utc-cntfrq", &Hw);

  if (Host.Valid) {
    mAnchor = Host;
    mSource = "host";
  } else if (Hw.Valid) {
    mAnchor = Hw;
    mSource = "hardware";
  }

  if (mAnchor.Valid) {
    DEBUG ((DEBUG_INFO, "RTC: wall-clock source=%a utc_ms=%ld cntpct=0x%lx cntfrq=%lu (host:%a hardware:%a)\n",
            mSource, mAnchor.UtcMs, mAnchor.Counter, mAnchor.Frequency, HostWhy, HwWhy));
  } else {
    DEBUG ((DEBUG_INFO, "RTC: no wall-clock anchor (host:%a hardware:%a), virtual clock\n", HostWhy, HwWhy));
  }
  if (Host.Valid && Hw.Valid) {
    // both translated to the same counter instant: hardware minus host = the RTC error
    INT64  Now = (INT64)GetPerformanceCounter ();
    INT64  D   = rtc_wc_now_ms (Hw.UtcMs, Hw.Counter, (UINT64)Now, Hw.Frequency) -
                 rtc_wc_now_ms (Host.UtcMs, Host.Counter, (UINT64)Now, Host.Frequency);
    DEBUG ((DEBUG_INFO, "RTC: hardware minus host = %ld ms\n", D));
  }
  return EFI_SUCCESS;
}

VOID
EFIAPI
LibRtcVirtualNotifyEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  return;                                     // only scalar state, nothing to convert
}
