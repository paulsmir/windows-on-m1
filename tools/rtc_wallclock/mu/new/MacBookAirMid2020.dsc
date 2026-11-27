#MacBook Air Mid 2020 UEFI DSC file
#some parts borrowed from WOA-Project/SurfaceDuoPkg
#Disclaimer: probably not the best UEFI dev out there
#
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#
#  This program and the accompanying materials
#  are licensed and made available under the terms and conditions of the BSD License
#  which accompanies this distribution.  The full text of the license may be found at
#  http://opensource.org/licenses/bsd-license.php
#
#  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
#  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
#
#

#SPDX-License-Identifier: BSD 2-Clause

[Defines]
  PLATFORM_NAME                  = MacBookAirMid2020
  PLATFORM_GUID                  = 482c40bb-f36c-4c25-bc04-2c05e75542ef
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/MacBookAirMid2020-$(ARCH)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MacBookAirMid2020Pkg/MacBookAirMid2020.fdf
  SECURE_BOOT_ENABLE             = FALSE
  AIC_BUILD                      = TRUE #AIC build enabled by default, change to false if you want to use a vGIC
  USES_MAC_CPU                   = TRUE # a futureproofing switch, changes SoC identifier in SMBIOS
  NETWORK_TLS_ENABLE             = TRUE
  # Enumeration-only AGX G2 profile.  Stable builds must keep this FALSE.
  J313_AGX_G2_PROFILE            = FALSE
  # Offline native ANS candidate only; never enable before EL2 trap qualification.
  J313_ANS_NATIVE_PROFILE        = FALSE
  # Installed native boot of the internal Windows (requires J313_ANS_NATIVE_PROFILE): boots only
  # the pinned Windows ESP below instead of the preloaded WinPE RAMDisk. Never a default.
  J313_ANS_INSTALLED_BOOT        = FALSE
  # ANS-DIRECT (requires J313_ANS_NATIVE_PROFILE): APPL0003 describes the physical ANS2 controller
  # and ASC mailbox; m1n1 must be launched with --ans-direct. Never a default.
  J313_ANS_DIRECT                = FALSE
  # Candidate-only synthetic-interrupt Full Graphics ABI admission profile.
  J313_AGX_ABI_ADMISSION_PROFILE            = FALSE
  J313_KBD_BACKLIGHT                        = TRUE
  # Wireless PCI segment 1 (MCFG entry 1 + PCI1/RP00 SSDT, G3b _PRT INTA/INTB); default off.
  J313_WIFI_PCIE                            = FALSE
  J313_AGX_RESERVE_V2_PROFILE                = FALSE
  # IRQ-completion (admission profile only): also list the ASC mailbox
  # recv-not-empty line (guest 887, level) in the AGX device _CRS (Mu 95263b97).
  J313_AGX_MAILBOX_IRQ                       = FALSE
  # Wall clock (plan B20): RealTimeClockRuntimeDxe reads the ADT /chosen UTC anchor (host m1n1-iomfb-utc-*, or the
  # m1n1 hardware anchor m1n1-rtc-utc-*) instead of the uptime-only virtual clock. Read-only; default off.
  J313_RTC_ANCHOR                            = FALSE

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DSILICON_PLATFORM=8103
!if $(J313_AGX_RESERVE_V2_PROFILE) == TRUE
  *_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES -D HAS_MEMCPY_INTRINSICS -DAGX_LOCAL_RESERVE_V2
!if $(J313_AGX_MAILBOX_IRQ) == TRUE
  *_*_*_ASLPP_FLAGS = -DAGX_LOCAL_RESERVE_V2 -DAGX_MAILBOX_IRQ
!else
  *_*_*_ASLPP_FLAGS = -DAGX_LOCAL_RESERVE_V2
!endif
!else
  *_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES -D HAS_MEMCPY_INTRINSICS
!if $(J313_AGX_MAILBOX_IRQ) == TRUE
  *_*_*_ASLPP_FLAGS = -DAGX_MAILBOX_IRQ
!endif
!endif

[PcdsFixedAtBuild.common]
!if $(J313_ANS_NATIVE_PROFILE) == TRUE
  # Must equal config/j313-ans-installed-boot.json.broker.guest_base/size (root contract test).
  gAppleSiliconPkgTokenSpaceGuid.PcdAnsBrokerWindowBase|0x300010000
  gAppleSiliconPkgTokenSpaceGuid.PcdAnsBrokerWindowSize|0x4000
!if $(J313_ANS_INSTALLED_BOOT) == TRUE
  # J313 disk 0 GPT entry "WINESP" (recovery Get-Partition 2026-11-07, firmware path in EXP2225):
  # HD(5,GPT,F05B9952-EE05-44E1-BCC4-B678A8501B2A,0x1D5AF00,0x20000). Must equal config/j313-ans-installed-boot.json.
  gAppleSiliconPkgTokenSpaceGuid.PcdAnsInstalledEspStartLba|0x1D5AF00
  gAppleSiliconPkgTokenSpaceGuid.PcdAnsInstalledEspBlocks|0x20000
  gAppleSiliconPkgTokenSpaceGuid.PcdAnsInstalledEspGuid|{ 0x52, 0x99, 0x5B, 0xF0, 0x05, 0xEE, 0xE1, 0x44, 0xBC, 0xC4, 0xB6, 0x78, 0xA8, 0x50, 0x1B, 0x2A }
!endif
!endif
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModel|"MacBook Air (Mid 2020)"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemModelNumber|"MacBookAir10,1"
  gAppleSiliconPkgTokenSpaceGuid.PcdSmbiosSystemSku|"MacBook Air (MacBookAir10,1)"
  # Every sibling M1 platform sets this; the Air did not, so it fell back to the .dec
  # default of 1. AppleUsbTypeCBringupDxe skips index 0 (the DFU port), so with a count
  # of 1 its loop body never runs at all and no USB controller is ever registered.
  # The Air has two Type-C ports, hence two DWC3 controllers.
  gAppleSiliconPkgTokenSpaceGuid.PcdAppleNumDwc3Controllers|2
  # Gate for BootRamdiskHelperDxe; without it the driver exits before looking for the
  # embedded image. Defaults to FALSE in AppleSiliconPkg.dec.
  gAppleSiliconPkgTokenSpaceGuid.PcdInitializeRamdisk|TRUE
  

[Components.common]

!if $(J313_ANS_NATIVE_PROFILE) == TRUE
!if $(J313_ANS_DIRECT) == TRUE
!if $(J313_AGX_ABI_ADMISSION_PROFILE) == TRUE
!if $(J313_KBD_BACKLIGHT) == TRUE
!if $(J313_WIFI_PCIE) == TRUE
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesAgxAbiAdmissionAnsDirectKbdWifi.inf
!else
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesAgxAbiAdmissionAnsDirectKbd.inf
!endif
!else
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesAgxAbiAdmissionAnsDirect.inf
!endif
!else
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesAnsDirect.inf
!endif
!else
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesAnsNative.inf
!endif
!elseif $(J313_AGX_ABI_ADMISSION_PROFILE) == TRUE
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesAgxAbiAdmission.inf
!elseif $(J313_AGX_G2_PROFILE) == TRUE
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesG2.inf
!else
  MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTables.inf
!endif # J313_AGX_ABI_ADMISSION_PROFILE / J313_AGX_G2_PROFILE

  # Built as a standalone application, deliberately NOT packaged into the FV: it is copied
  # onto the embedded RAMDisk instead. Running it exercises LoadImage/StartImage from a FAT
  # volume on BlockIo - the same path bootmgfw.efi takes - and the shell profile here has
  # no built-in acpiview, so it doubles as the ACPI table dumper.
  # Same idea: standalone, copied onto the RAMDisk, never packaged into the FV.
  AppleSiliconPkg/Application/BootLaunchApp/BootLaunchApp.inf
!if $(J313_ANS_NATIVE_PROFILE) == TRUE
!if $(J313_ANS_INSTALLED_BOOT) == TRUE
  AppleSiliconPkg/Drivers/WindowsAutoBootDxe/WindowsAutoBootAnsInstalled.inf
!else
  AppleSiliconPkg/Drivers/WindowsAutoBootDxe/WindowsAutoBootAnsNative.inf
!endif
!else
  AppleSiliconPkg/Drivers/WindowsAutoBootDxe/WindowsAutoBootDxe.inf
!endif
  AppleSiliconPkg/Application/BootLaunchApp/BootLaunchAppQemu.inf {
    <LibraryClasses>
      DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  }

  ShellPkg/Application/AcpiViewApp/AcpiViewApp.inf {
    <LibraryClasses>
      AcpiViewCommandLib|ShellPkg/Library/UefiShellAcpiViewCommandLib/UefiShellAcpiViewCommandLib.inf
  }

#
# The family package was not included at all, unlike every sibling platform
# (MacMini2020.dsc and MacBookProLate2020.dsc both pull theirs in first). Consequences:
# DeviceFamilyAcpiTables.inf was never built, so AcpiPlatformDxe asserted looking for the
# device family tables, and the family's display/console PCDs were never applied either.
#
!include MacBookAirFamilyPkg/MacBookAirFamily.dsc.inc
!include T810XFamilyPkg/T810XFamilyPkg.dsc.inc
!include MacBookAirMid2020Pkg/J313GuestLayout.dsc.inc
!include AppleSiliconPkg/AppleSiliconPkg.dsc.inc
!include AppleSiliconPkg/FrontpageDsc.inc
#
# Trim the debug output. The MemoryAttributesTable dump and the GCD memory space maps run
# to hundreds of lines each and are emitted right at ReadyToBoot, drowning out exactly the
# part we need to see: whether BDS calls StartImage for Boot0003 (Internal UEFI Shell) and
# what it returns. Keep ERROR/WARN/INIT/LOAD/INFO/BM, drop VERBOSE, GCD, POOL and PAGE.
#
# DebugLib here is MdePkg/BaseDebugLibSerialPort, which honours this PCD.
# Placed after the !include lines above so it is the last word on the value.
#
[PcdsFixedAtBuild.common]
  # 0x40 = DEBUG_INFO. AppleUsbTypeCBringupDxe logs its whole bringup at INFO, so
  # without this bit the driver is simply inaudible rather than silent.
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000447
  #
  # EfiBootManagerBoot() -> BmSetMemoryTypeInformationVariable() does a warm reset when the
  # memory type information changes, so that the new pool sizes take effect. On real
  # hardware that happens once: the variable persists and the next boot proceeds.
  #
  # Here there is no non-volatile variable storage -- the firmware is reloaded into RAM on
  # every run -- so the variable is always "missing", and the machine reset forever, always
  # before StartImage (hence no "Booting ..." line ever appeared in the log). Confirmed by
  # instrumenting RuntimeServiceResetSystem: "RESET CALLED: type=1 status=Success
  # caller=0x9DDB426DC" resolved to EfiBootManagerBoot +0xa2c.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
