##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  #
  # Timer
  #
  # hero2lte kernel log: "Architected cp15 timer(s) running at 26.00MHz (virt)"
  USE_PHYSICAL_TIMER                         = 0

  #
  # KEP
  #
  HAS_CNTFRQ_EL0_EMPTY_ERRATA                = 1

!include SamsungPkg/SamsungPkg.dsc.inc

[BuildOptions]
  # Mongoose M1 + Cortex-A53 are ARMv8.0 (no v8.2 / RCpc)
  *_*_AARCH64_CC_FLAGS = -march=armv8-a+crypto+crc

[PcdsFixedAtBuild]
  #
  # ACPI
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemTableId|0x2030393838453553
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x8890

  #
  # CPU
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  #
  # GIC
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x11001000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x11002000

  #
  # Timer
  #
  gSiliciumPkgTokenSpaceGuid.PcdTimerFreqOverwrite|26000000

  # SPEEDY: not configured for 8890 yet (PCD left at default)

  #
  # GPIO
  # List Contents (hero2lte DT pinctrl@ nodes): 0x10580000, 0x105A0000, 0x10E60000, 0x114B0000, 0x136D0000, 0x14C90000, 0x14CA0000, 0x14CC0000, 0x14CD0000, 0x14CE0000, 0x15690000
  gSamsungPkgTokenSpaceGuid.PcdGpioCtrlAddr|{ 0x00, 0x00, 0x58, 0x10, 0x00, 0x00, 0x5A, 0x10, 0x00, 0x00, 0xE6, 0x10, 0x00, 0x00, 0x4B, 0x11, 0x00, 0x00, 0x6D, 0x13, 0x00, 0x00, 0xC9, 0x14, 0x00, 0x00, 0xCA, 0x14, 0x00, 0x00, 0xCC, 0x14, 0x00, 0x00, 0xCD, 0x14, 0x00, 0x00, 0xCE, 0x14, 0x00, 0x00, 0x69, 0x15 }

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"Exynos (TM) 8890 @ 2.30 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"S5E8890"

[LibraryClasses]
  #
  # SEC Libraries
  #
  PlatformSecLib|S5E8890Pkg/Library/PlatformSecLib/PlatformSecLib.inf

  #
  # Memory Libraries
  #
  RamManagerLib|SiliciumNullPkg/Library/RamManagerLibNull/RamManagerLibNull.inf

  #
  # IO Libraries
  #
  GpioBankLib|S5E8890Pkg/Library/GpioBankLib/GpioBankLib.inf

  #
  # Storage Libraries
  #
  PlatformUfsLib|S5E8890Pkg/Library/PlatformUfsLib/PlatformUfsLib.inf
