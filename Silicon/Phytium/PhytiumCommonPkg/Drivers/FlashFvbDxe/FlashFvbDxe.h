/** @file
  Phytium NorFlash Fvb Drivers Header.

  Copyright (C) 2020, Phytium Technology Co Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#ifndef FVB_FLASH_DXE_H_
#define FVB_FLASH_DXE_H_

#include <Protocol/BlockIo.h>
#include <Protocol/FirmwareVolumeBlock.h>
#include <Protocol/SpiNorFlashProtocol.h>

#define GET_DATA_OFFSET(BaseAddr, Lba, LbaSize) ((BaseAddr) + (UINTN)((Lba) * (LbaSize)))
#define FVB_FLASH_SIGNATURE                       SIGNATURE_32('S', 'N', 'O', 'R')
#define INSTANCE_FROM_FVB_THIS(a)                 CR(a, FT_FVB_DEVICE, FvbProtocol, FVB_FLASH_SIGNATURE)

typedef struct _FT_FVB_DEVICE    FT_FVB_DEVICE;

#define NOR_FLASH_ERASE_RETRY         10

typedef struct {
  VENDOR_DEVICE_PATH                  Vendor;
  EFI_DEVICE_PATH_PROTOCOL            End;
  } FT_FVB_DEVICE_PATH;

struct _FT_FVB_DEVICE {
  UINT32                              Signature;
  EFI_HANDLE                          Handle;

  UINTN                               DeviceBaseAddress;
  UINTN                               RegionBaseAddress;
  UINTN                               Size;
  EFI_LBA                             StartLba;
  EFI_BLOCK_IO_MEDIA                  Media;

  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL FvbProtocol;

  FT_FVB_DEVICE_PATH                  DevicePath;
  EFI_NORFLASH_DRV_PROTOCOL          *SpiFlashProtocol;
  VOID                               *ShadowBuffer;
  UINTN                               FvbSize;
  };

EFI_STATUS
EFIAPI
FvbGetAttributes (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *     This,
    OUT       EFI_FVB_ATTRIBUTES_2 *                    Attributes
  );

EFI_STATUS
EFIAPI
FvbSetAttributes (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *     This,
    IN OUT    EFI_FVB_ATTRIBUTES_2 *                    Attributes
  );

EFI_STATUS
EFIAPI
FvbGetPhysicalAddress (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *     This,
    OUT       EFI_PHYSICAL_ADDRESS *                    Address
  );

EFI_STATUS
EFIAPI
FvbGetBlockSize (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *     This,
    IN        EFI_LBA                                  Lba,
    OUT       UINTN *                                   BlockSize,
    OUT       UINTN *                                   NumberOfBlocks
  );

EFI_STATUS
EFIAPI
FvbRead (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *     This,
    IN        EFI_LBA                                  Lba,
    IN        UINTN                                    Offset,
    IN OUT    UINTN *                                   NumBytes,
    IN OUT    UINT8 *                                   Buffer
  );

EFI_STATUS
EFIAPI
FvbWrite (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *     This,
    IN        EFI_LBA                                  Lba,
    IN        UINTN                                    Offset,
    IN OUT    UINTN *                                   NumBytes,
    IN        UINT8 *                                   Buffer
  );

EFI_STATUS
EFIAPI
FvbEraseBlocks (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *     This,
    ...
  );

#endif // FVB_FLASH_DXE_H_
