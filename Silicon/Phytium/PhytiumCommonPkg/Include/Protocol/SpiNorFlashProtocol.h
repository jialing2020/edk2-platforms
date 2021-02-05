/** @file
  The Header of Protocol For NorFlash.

  Copyright (C) 2020, Phytium Technology Co Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef SPI_NORFALSH_H_
#define SPI_NORFALSH_H_

typedef struct _EFI_NORFLASH_DRV_PROTOCOL EFI_NORFLASH_DRV_PROTOCOL;
extern EFI_GUID gSpiNorFlashProtocolGuid;

typedef struct {
  UINTN       DeviceBaseAddress;    // Start address of the Device Base Address (DBA)
  UINTN       RegionBaseAddress;    // Start address of one single region
  UINTN       Size;
  UINTN       BlockSize;
  EFI_GUID    Guid;
} NOR_FLASH_DEVICE_DESCRIPTION;

typedef
EFI_STATUS
(EFIAPI *NORFLASH_PLATFORM_ERASE_INTERFACE) (
  IN UINT64                  Offset,
  IN UINT64                  Length
  );

typedef
EFI_STATUS
(EFIAPI *NORFLASH_PLATFORM_ERASESIGLEBLOCK_INTERFACE) (
  IN UINTN            BlockAddress
  );

typedef
EFI_STATUS
(EFIAPI *NORFLASH_PLATFORM_READ_INTERFACE) (
  IN UINTN                Address,
  IN VOID                 *Buffer,
  OUT UINT32              Len
  );

typedef
EFI_STATUS
(EFIAPI *NORFLASH_PLATFORM_WRITE_INTERFACE) (
  IN UINTN                Address,
  IN VOID                 *Buffer,
  IN UINT32               Len
  );

typedef
EFI_STATUS
(EFIAPI *NORFLASH_PLATFORM_GETDEVICE_INTERFACE) (
  OUT NOR_FLASH_DEVICE_DESCRIPTION *NorFlashDevices
  );

typedef
EFI_STATUS
(EFIAPI *NORFLASH_PLATFORM_INIT_INTERFACE) (
  VOID
  );

struct _EFI_NORFLASH_DRV_PROTOCOL{
  NORFLASH_PLATFORM_INIT_INTERFACE       Initialization;
  NORFLASH_PLATFORM_GETDEVICE_INTERFACE  GetDevices;
  NORFLASH_PLATFORM_ERASE_INTERFACE       Erase;
  NORFLASH_PLATFORM_ERASESIGLEBLOCK_INTERFACE  EraseSingleBlock;
  NORFLASH_PLATFORM_READ_INTERFACE        Read;
  NORFLASH_PLATFORM_WRITE_INTERFACE       Write;
};

#endif // SPI_NORFALSH_H_
