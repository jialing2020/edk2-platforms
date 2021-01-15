/** @file
  Library of memory map for Phytium platform.

  Copyright (C) 2020, Phytium Technology Co Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ArmSmcLib.h>
#include <PhytiumSystemServiceInterface.h>

// Number of Virtual Memory Map Descriptors
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 32

// DDR attributes
#define DDR_ATTRIBUTES_CACHED              ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define DDR_ATTRIBUTES_UNCACHED            ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED

/**
  Return the Virtual Memory Map of your platform

  This Virtual Memory Map is used by MemoryInitPei Module to initialize the MMU on your platform.

  @param[out]  VirtualMemoryMap  Array of ARM_MEMORY_REGION_DESCRIPTOR describing a Physical-to-
                                 Virtual Memory mapping. This array must be ended by a zero-filled
                                 entry
**/
VOID
ArmPlatformGetVirtualMemoryMap (
  IN ARM_MEMORY_REGION_DESCRIPTOR** VirtualMemoryMap
  )
{
  ARM_MEMORY_REGION_ATTRIBUTES  CacheAttributes;
  ARM_MEMORY_REGION_DESCRIPTOR  *VirtualMemoryTable;
  EFI_RESOURCE_ATTRIBUTE_TYPE   ResourceAttributes;
  MEMORY_BLOCK                  *MemBlock = NULL;
  MEMORY_INFOR                  *MemInfor = NULL;
  ARM_SMC_ARGS                  ArmSmcArgs;
  UINT32                        MemBlockCnt = 0, Index, Index1;

  CacheAttributes = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;

  ASSERT (VirtualMemoryMap != NULL);
  VirtualMemoryTable = (ARM_MEMORY_REGION_DESCRIPTOR*)AllocatePages(EFI_SIZE_TO_PAGES (sizeof(ARM_MEMORY_REGION_DESCRIPTOR) * MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS));
  if (VirtualMemoryTable == NULL) {
    return;
  }

  ResourceAttributes =
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED;

  MemInfor = AllocatePages(1);
  ASSERT(MemInfor != NULL);

  ArmSmcArgs.Arg0 = PHYTIUM_OEM_SVC_MEM_REGIONS;
  ArmSmcArgs.Arg1 = (UINTN)MemInfor;
  ArmSmcArgs.Arg2 = EFI_PAGE_SIZE;
  ArmCallSmc (&ArmSmcArgs);
  if (ArmSmcArgs.Arg0 == 0) {
    MemBlockCnt = MemInfor->MemBlockCount;
    MemBlock = MemInfor->MemBlock;
  } else {
    ASSERT(FALSE);
  }

  //Soc Io Space
  VirtualMemoryTable[Index].PhysicalBase   = PcdGet64 (PcdSystemIoBase);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64 (PcdSystemIoBase);
  VirtualMemoryTable[Index].Length         = PcdGet64 (PcdSystemIoSize);
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  //
  // PCI Configuration Space
  //
  VirtualMemoryTable[++Index].PhysicalBase  = PcdGet64 (PcdPciConfigBase);
  VirtualMemoryTable[Index].VirtualBase     = PcdGet64 (PcdPciConfigBase);
  VirtualMemoryTable[Index].Length          = PcdGet64 (PcdPciConfigSize);
  VirtualMemoryTable[Index].Attributes      = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  //
  // PCI Memory Space
  //
  VirtualMemoryTable[++Index].PhysicalBase  = PcdGet64 (PcdPciIoBase) + PcdGet64(PcdPciIoTranslation);
  VirtualMemoryTable[Index].VirtualBase     = PcdGet64 (PcdPciIoBase) + PcdGet64(PcdPciIoTranslation);
  VirtualMemoryTable[Index].Length          = PcdGet64 (PcdPciIoSize);
  VirtualMemoryTable[Index].Attributes      = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  //
  // PCI Memory Space
  //
  VirtualMemoryTable[++Index].PhysicalBase  = PcdGet32 (PcdPciMmio32Base);
  VirtualMemoryTable[Index].VirtualBase     = PcdGet32 (PcdPciMmio32Base);
  VirtualMemoryTable[Index].Length          = PcdGet32 (PcdPciMmio32Size);
  VirtualMemoryTable[Index].Attributes      = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  //
  // 64-bit PCI Memory Space
  //
  VirtualMemoryTable[++Index].PhysicalBase  = PcdGet64 (PcdPciMmio64Base);
  VirtualMemoryTable[Index].VirtualBase     = PcdGet64 (PcdPciMmio64Base);
  VirtualMemoryTable[Index].Length          = PcdGet64 (PcdPciMmio64Size);
  VirtualMemoryTable[Index].Attributes      = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  //DDR
  for (Index1 = 0; Index1 < MemBlockCnt; Index1++) {
    VirtualMemoryTable[++Index].PhysicalBase = MemBlock->MemStart;
    VirtualMemoryTable[Index].VirtualBase    = MemBlock->MemStart;
    VirtualMemoryTable[Index].Length         = MemBlock->MemSize;
    VirtualMemoryTable[Index].Attributes     = CacheAttributes;

    BuildResourceDescriptorHob (
      EFI_RESOURCE_SYSTEM_MEMORY,
      ResourceAttributes,
      MemBlock->MemStart,
      MemBlock->MemSize);

    MemBlock ++;
  }

  // End of Table
  VirtualMemoryTable[++Index].PhysicalBase = 0;
  VirtualMemoryTable[Index].VirtualBase    = 0;
  VirtualMemoryTable[Index].Length         = 0;
  VirtualMemoryTable[Index].Attributes     = (ARM_MEMORY_REGION_ATTRIBUTES)0;

  ASSERT((Index + 1) <= MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS);

  for (Index1 = 0; Index1 < Index; Index1++) {
    DEBUG((DEBUG_ERROR, "PhysicalBase %12lx VirtualBase %12lx Length %12lx Attributes %12lx\n", VirtualMemoryTable[Index1].PhysicalBase,\
        VirtualMemoryTable[Index1].VirtualBase, VirtualMemoryTable[Index1].Length, VirtualMemoryTable[Index1].Attributes));
  }

  *VirtualMemoryMap = VirtualMemoryTable;
}
