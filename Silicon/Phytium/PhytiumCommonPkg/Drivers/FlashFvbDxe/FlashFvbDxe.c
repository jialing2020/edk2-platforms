/** @file
  Phytium NorFlash Fvb Drivers.

  Copyright (c) 2011 - 2014, ARM Ltd. All rights reserved.<BR>
  Copyright (C) 2020, Phytium Technology Co Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Guid/VariableFormat.h>

#include "FlashFvbDxe.h"

STATIC EFI_EVENT       FvbVirtualAddrChangeEvent;
STATIC FT_FVB_DEVICE   *FvbDevice;
STATIC UINTN           mFlashNvStorageVariableBase;
STATIC UINTN           mFlashNvStorageFtwWorkingBase;
STATIC UINTN           mFlashNvStorageFtwSpareBase;
STATIC UINT32          mFlashNvStorageVariableSize;
STATIC UINT32          mFlashNvStorageFtwWorkingSize;
STATIC UINT32          mFlashNvStorageFtwSpareSize;

STATIC FT_FVB_DEVICE FvbFlashInstanceTemplate = {
  FVB_FLASH_SIGNATURE, // Signature
  NULL,                // Handle ... NEED TO BE FILLED
  0,                   // DeviceBaseAddress ... NEED TO BE FILLED
  0,                   // RegionBaseAddress ... NEED TO BE FILLED
  0,                   // Size ... NEED TO BE FILLED
  0,                   // StartLba
  {
    0,                 // MediaId ... NEED TO BE FILLED
    FALSE,             // RemovableMedia
    TRUE,              // MediaPresent
    FALSE,             // LogicalPartition
    FALSE,             // ReadOnly
    FALSE,             // WriteCaching;
    0,                 // BlockSize ... NEED TO BE FILLED
    4,                 // IoAlign
    0,                 // LastBlock ... NEED TO BE FILLED
    0,                 // LowestAlignedLba
    1,                 // LogicalBlocksPerPhysicalBlock
  },                   //Media;
  {
    FvbGetAttributes,      // GetAttributes
    FvbSetAttributes,      // SetAttributes
    FvbGetPhysicalAddress, // GetPhysicalAddress
    FvbGetBlockSize,       // GetBlockSize
    FvbRead,               // Read
    FvbWrite,              // Write
    FvbEraseBlocks,        // EraseBlocks
    NULL,                  // ParentHandle
  },                       // FvbProtoccol;

  {
    {
      {
        HARDWARE_DEVICE_PATH,
        HW_VENDOR_DP,
        {
          (UINT8) sizeof (VENDOR_DEVICE_PATH),
          (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8)
        }
      },
      {
        0x0, 0x0, 0x0, { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
      }, // GUID ... NEED TO BE FILLED
    },
    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        sizeof (EFI_DEVICE_PATH_PROTOCOL),
        0
      }
    }
  },     // DevicePath

  NULL,  // SpiFlashProtocol ... NEED TO BE FILLED
  NULL,  // ShadowBuffer     ... NEED TO BE FILLED
  0      // Fvb Size
};


/**
  Erases a single block of flash.

  @param[in] FlashInstance      The poiter of the fvb device sturct.

  @param[in] BlockAddress       Physical address of Lba to be erased.

  @retval EFI_SUCCESS           The erase single block request successfully completed.

**/
STATIC
EFI_STATUS
FvbFlashEraseSingleBlock (
  IN FT_FVB_DEVICE     *FlashInstance,
  IN UINTN             BlockAddress
  )
{
  EFI_STATUS      Status;
  UINTN           Index;
  EFI_TPL         OriginalTPL;

  if ( ! EfiAtRuntime ()) {
     OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  } else {
    OriginalTPL = TPL_HIGH_LEVEL;
  }

  Index = 0;

  do {
    Status = FlashInstance->SpiFlashProtocol->EraseSingleBlock (BlockAddress);
    Index++;
  } while ((Index < NOR_FLASH_ERASE_RETRY) && (Status == EFI_WRITE_PROTECTED));

  if (Index == NOR_FLASH_ERASE_RETRY) {
    DEBUG ((
      DEBUG_ERROR,
      "EraseSingleBlock(BlockAddress=0x%08x: BlockLocked Error (try to erase % d times)\n",
      BlockAddress,
      Index
      ));
  }

  if ( ! EfiAtRuntime ()) {
    gBS->RestoreTPL (OriginalTPL);
  }

  return Status;
}


/**
  Readed the specified number of bytes from the form the block to output buffer.

  @param[in]  FlashInstance        The pointer of FT_FVB_DEVICE instance.

  @param[in]  Lba                  The starting logical block index to write to.

  @param[in]  Offset               Offset into the block at which to begin writing.

  @param[in]  BufferSizeInBytes    The number of bytes to be writed.

  @param[out] Buffer               The pointer to a caller-allocated buffer that
                                   contains the source for the write.

  @retval EFI_SUCCESS              FvbFlashRead() is executed successfully.

**/
STATIC
EFI_STATUS
FvbFlashRead (
  IN FT_FVB_DEVICE   *FlashInstance,
  IN EFI_LBA          Lba,
  IN UINTN            Offset,
  IN UINTN            BufferSizeInBytes,
  OUT VOID            *Buffer
  )
{
  UINTN Address;

  Address = GET_DATA_OFFSET (
              FlashInstance->RegionBaseAddress,
              Lba,
              FlashInstance->Media.BlockSize
              ) + Offset;

  if (BufferSizeInBytes == 0) {
    return EFI_SUCCESS;
  }

  // The buffer must be valid
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  return FlashInstance->SpiFlashProtocol->Read (Address, Buffer, BufferSizeInBytes);
}


/**
  Write a full or portion of a block. It must not span block boundaries; that is,
  Offset + *NumBytes <= FlashInstance->Media.BlockSize.

  @param[in]  FlashInstance        The pointer of FT_FVB_DEVICE instance.

  @param[in]  Lba                  The starting logical block index to write to.

  @param[in]  Offset               Offset into the block at which to begin writing.

  @param[in]  BufferSizeInBytes    The number of bytes to be writed.

  @param[out] Buffer               The pointer to a caller-allocated buffer that
                                   contains the source for the write.

  @retval EFI_SUCCESS              FvbWriteBlock() is executed successfully.

  @retval EFI_BAD_BUFFER_SIZE      The write spaned block boundaries.

**/
STATIC
EFI_STATUS
FvbWriteBlock (
  IN FT_FVB_DEVICE   *FlashInstance,
  IN EFI_LBA          Lba,
  IN UINTN            Offset,
  IN UINTN            BufferSizeInBytes,
  IN UINT8            *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       BlockSize;
  UINTN       BlockAddress;

  // Detect WriteDisabled state
  if (FlashInstance->Media.ReadOnly == TRUE) {
    DEBUG ((
      DEBUG_ERROR,
      "FvbWriteBlock: ERROR - Can not write:Device is in WriteDisabled state.\n"
      ));
    // It is in WriteDisabled state, return an error right away
    return EFI_ACCESS_DENIED;
  }

  // Cache the block size to avoid de-referencing pointers all the time
  BlockSize = FlashInstance->Media.BlockSize;

  // The write must not span block boundaries.
  // We need to check each variable individually because adding two large values together overflows.
  if ((Offset                       >= BlockSize) ||
      (BufferSizeInBytes            >  BlockSize) ||
      ((Offset + BufferSizeInBytes) >  BlockSize))
  {
    DEBUG ((
      DEBUG_ERROR,
      "FvbWriteBlock: ERROR - EFI_BAD_BUFFER_SIZE: (Offset =0x %x + NumBytes =0x%x) > BlockSize =0x%x\n",
      Offset,
      BufferSizeInBytes,
      BlockSize
      ));
    return EFI_BAD_BUFFER_SIZE;
  }

  // We must have some bytes to write
  if (BufferSizeInBytes == 0) {
    DEBUG ((
      DEBUG_ERROR,
      "FvbWriteBlock: ERROR - EFI_BAD_BUFFER_SIZE: NumBytes == 0\n"
      ));
    return EFI_BAD_BUFFER_SIZE;
  }

  // Check we did get some memory. Buffer is BlockSize.
  if (FlashInstance->ShadowBuffer == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "FvbWriteBlock: ERROR - ShadowBuffer is NULL!\n"
      ));
    return EFI_DEVICE_ERROR;
  }

  //
  // Write the word to NOR.
  //
  BlockAddress = GET_DATA_OFFSET (
                   FlashInstance->RegionBaseAddress,
                   Lba,
                   FlashInstance->Media.BlockSize
                   );

  // Read NOR Flash data into shadow buffer
  Status = FlashInstance->SpiFlashProtocol->Read (
             BlockAddress,
             FlashInstance->ShadowBuffer,
             BlockSize
             );
  if (EFI_ERROR (Status)) {
    // Return one of the pre-approved error statuses
    return EFI_DEVICE_ERROR;
  }

  // Put the data at the appropriate location inside the buffer area
  CopyMem (
    (VOID *) ((UINTN)FlashInstance->ShadowBuffer + Offset),
    Buffer,
    BufferSizeInBytes
    );

  Status = FlashInstance->SpiFlashProtocol->EraseSingleBlock (BlockAddress);
  if (EFI_ERROR (Status)) {
    // Return one of the pre-approved error statuses
    return EFI_DEVICE_ERROR;
  }

  // Write the modified buffer back to the NorFlash
  Status = FlashInstance->SpiFlashProtocol->Write (BlockAddress,
             FlashInstance->ShadowBuffer,
             BlockSize
             );
  if (EFI_ERROR (Status)) {
    // Return one of the pre-approved error statuses
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}


/**
  Writes the specified number of bytes from the input buffer to the block.

  @param[in] FlashInstance        The pointer of FT_FVB_DEVICE instance.

  @param[in] Lba                  The starting logical block index to write to.

  @param[in] Offset               Offset into the block at which to begin writing.

  @param[in] BufferSizeInBytes    The number of bytes to be writed.

  @param[in] Buffer               The pointer to a caller-allocated buffer that
                                  contains the source for the write.

  @retval EFI_SUCCESS             FvbFlashWrite() is executed successfully.

  @retval EFI_WRITE_PROTECTED     Flash state is in the WriteDisabled state.

  @retval EFI_INVALID_PARAMETER   The pointer of Buffer is NULL.

**/
STATIC
EFI_STATUS
FvbFlashWrite (
  IN FT_FVB_DEVICE    *FlashInstance,
  IN EFI_LBA          Lba,
  IN UINTN            Offset,
  IN UINTN            BufferSizeInBytes,
  IN VOID             *Buffer
  )
{
  EFI_STATUS      Status;
  UINT32          BlockSize;
  UINT32          BlockOffset;
  UINTN           RemainingBytes;
  UINTN           WriteSize;

  if (FlashInstance->Media.ReadOnly == TRUE) {
    return EFI_WRITE_PROTECTED;
  }

  if (BufferSizeInBytes == 0) {
    return EFI_SUCCESS;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status         = EFI_SUCCESS;
  BlockSize      = FlashInstance->Media.BlockSize;
  BlockOffset    = Offset;
  RemainingBytes = BufferSizeInBytes;

  // The write must not span block boundaries.
  // We need to check each variable individually because adding
  // two large values together overflows.
  if (Offset >= BlockSize) {
    DEBUG ((
      DEBUG_ERROR,
      "FvbFlashWrite: ERROR - EFI_BAD_BUFFER_SIZE: Offset =0x%x > BlockSize =0x%x\n",
      Offset,
      BlockSize
      ));
    return EFI_BAD_BUFFER_SIZE;
  }

  // We must have some bytes to read
  // Write either all the remaining bytes, or the number of bytes that bring
  // us up to a block boundary, whichever is less.
  // (DiskOffset | (BlockSize - 1)) + 1) rounds DiskOffset up to the next
  // block boundary (even if it is already on one).
  WriteSize = MIN (RemainingBytes, BlockSize - BlockOffset);

  do {
    Status = FvbWriteBlock (
               FlashInstance,
               Lba,
               BlockOffset,
               WriteSize,
               Buffer
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    // Now continue writing either all the remaining bytes or single blocks.
    RemainingBytes -= WriteSize;
    Buffer = (UINT8 *) Buffer + WriteSize;
    Lba++;
    BlockOffset = 0;
    WriteSize = MIN (RemainingBytes, BlockSize);
  } while (RemainingBytes);

  return Status;
}


/**
  Initialises the FV Header and Variable Store Header
  to support variable operations.

  @param[in] Ptr       Location to initialise the headers.

  @retval EFI_SUCCESS  FvbInitFvAndVariableStoreHeaders()
                       is executed successfully.

**/
STATIC
EFI_STATUS
FvbInitFvAndVariableStoreHeaders (
  IN FT_FVB_DEVICE *FlashInstance
  )
{
  EFI_STATUS                  Status;
  VOID *                       Headers;
  UINTN                       HeadersLength;
  UINT32                      TempAttributes;
  EFI_FIRMWARE_VOLUME_HEADER  *FirmwareVolumeHeader;
  VARIABLE_STORE_HEADER       *VariableStoreHeader;

  HeadersLength = sizeof (EFI_FIRMWARE_VOLUME_HEADER) +
                  sizeof (EFI_FV_BLOCK_MAP_ENTRY) +
                  sizeof (VARIABLE_STORE_HEADER);

  Headers = AllocateZeroPool (HeadersLength);

  // FirmwareVolumeHeader->FvLength is declared to have the Variable area
  // AND the FTW working area AND the FTW Spare contiguous.
  ASSERT (mFlashNvStorageVariableBase + mFlashNvStorageVariableSize == mFlashNvStorageFtwWorkingBase);
  ASSERT (mFlashNvStorageFtwWorkingBase + mFlashNvStorageFtwWorkingSize == mFlashNvStorageFtwSpareBase);

  // Check if the size of the area is at least one block size
  ASSERT ((mFlashNvStorageVariableSize > 0) && (mFlashNvStorageVariableSize / FlashInstance->Media.BlockSize > 0));
  ASSERT ((mFlashNvStorageFtwWorkingSize > 0) && (mFlashNvStorageFtwWorkingSize / FlashInstance->Media.BlockSize > 0));
  ASSERT ((mFlashNvStorageFtwSpareSize > 0) && (mFlashNvStorageFtwSpareSize / FlashInstance->Media.BlockSize > 0));

  // Ensure the Variable area Base Addresses are aligned on a block size boundaries
  ASSERT (mFlashNvStorageVariableBase % FlashInstance->Media.BlockSize == 0);
  ASSERT (mFlashNvStorageFtwWorkingBase % FlashInstance->Media.BlockSize == 0);
  ASSERT (mFlashNvStorageFtwSpareBase % FlashInstance->Media.BlockSize == 0);

  //
  // EFI_FIRMWARE_VOLUME_HEADER
  //
  FirmwareVolumeHeader = (EFI_FIRMWARE_VOLUME_HEADER *)Headers;
  CopyGuid (&FirmwareVolumeHeader->FileSystemGuid, &gEfiSystemNvDataFvGuid);
  FirmwareVolumeHeader->FvLength = FlashInstance->FvbSize;

  TempAttributes = (
                     EFI_FVB2_READ_ENABLED_CAP   | // Reads may be enabled
                     EFI_FVB2_READ_STATUS        | // Reads are currently enabled
                     EFI_FVB2_STICKY_WRITE       | // A block erase is required to
                     EFI_FVB2_MEMORY_MAPPED      | // It is memory mapped
                     EFI_FVB2_ERASE_POLARITY     | // After erasure all bits take this value
                     EFI_FVB2_WRITE_STATUS       | // Writes are currently enabled
                     EFI_FVB2_WRITE_ENABLED_CAP    // Writes may be enabled
                   );

  FirmwareVolumeHeader->Signature = EFI_FVH_SIGNATURE;
  FirmwareVolumeHeader->Attributes = (EFI_FVB_ATTRIBUTES_2) TempAttributes;

  FirmwareVolumeHeader->HeaderLength = sizeof (EFI_FIRMWARE_VOLUME_HEADER) + sizeof (EFI_FV_BLOCK_MAP_ENTRY);
  FirmwareVolumeHeader->Revision = EFI_FVH_REVISION;
  FirmwareVolumeHeader->BlockMap[0].NumBlocks = FlashInstance->Media.LastBlock + 1;
  FirmwareVolumeHeader->BlockMap[0].Length    = FlashInstance->Media.BlockSize;
  FirmwareVolumeHeader->BlockMap[1].NumBlocks = 0;
  FirmwareVolumeHeader->BlockMap[1].Length    = 0;
  FirmwareVolumeHeader->Checksum = CalculateCheckSum16 (
                                     (UINT16 *)FirmwareVolumeHeader,
                                     FirmwareVolumeHeader->HeaderLength
                                     );

  //
  // VARIABLE_STORE_HEADER
  //
  VariableStoreHeader = (VARIABLE_STORE_HEADER *) ((UINTN)Headers + FirmwareVolumeHeader->HeaderLength);
  CopyGuid (&VariableStoreHeader->Signature, &gEfiAuthenticatedVariableGuid);
  VariableStoreHeader->Size   = mFlashNvStorageVariableSize - FirmwareVolumeHeader->HeaderLength;
  VariableStoreHeader->Format = VARIABLE_STORE_FORMATTED;
  VariableStoreHeader->State  = VARIABLE_STORE_HEALTHY;

  // Install the combined super-header in the NorFlash
  Status = FvbWrite (&FlashInstance->FvbProtocol, 0, 0, &HeadersLength, Headers);

  FreePool (Headers);

  return Status;
}


/**
  Check the integrity of firmware volume header.

  @param[in] FwVolHeader   A pointer to a firmware volume header

  @retval  EFI_SUCCESS     The firmware volume is consistent

  @retval  EFI_NOT_FOUND   The firmware volume has been corrupted.

**/
STATIC
EFI_STATUS
FvbValidateFvHeader (
  IN  FT_FVB_DEVICE *FlashInstance
  )
{
  UINT16                      Checksum;
  EFI_FIRMWARE_VOLUME_HEADER  *FwVolHeader;
  VARIABLE_STORE_HEADER       *VariableStoreHeader;
  UINTN                       VariableStoreLength;
  UINTN                       FvLength;

  FwVolHeader = (EFI_FIRMWARE_VOLUME_HEADER *)GET_DATA_OFFSET (FlashInstance->RegionBaseAddress,
                                                FlashInstance->StartLba,
                                                FlashInstance->Media.BlockSize
                                                );
  FvLength = FlashInstance->FvbSize;


  if ((FwVolHeader->Revision  != EFI_FVH_REVISION)  ||
      (FwVolHeader->Signature != EFI_FVH_SIGNATURE) ||
      (FwVolHeader->FvLength  != FvLength))
  {
    DEBUG ((
      DEBUG_ERROR,
      "ValidateFvHeader: No Firmware Volume header present\n"
      ));
    return EFI_NOT_FOUND;
  }

  // Check the Firmware Volume Guid
  if ( CompareGuid (&FwVolHeader->FileSystemGuid, &gEfiSystemNvDataFvGuid) == FALSE ) {
    DEBUG ((
      DEBUG_ERROR,
      "ValidateFvHeader: Firmware Volume Guid non-compatible\n"
      ));
    return EFI_NOT_FOUND;
  }

  // Verify the header checksum
  Checksum = CalculateSum16 ((UINT16 *)FwVolHeader, FwVolHeader->HeaderLength);
  if (Checksum != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "ValidateFvHeader: FV checksum is invalid (Checksum:0x%X)\n",
      Checksum));
    return EFI_NOT_FOUND;
  }

  VariableStoreHeader = (VARIABLE_STORE_HEADER *) ((UINTN)FwVolHeader + FwVolHeader->HeaderLength);

  // Check the Variable Store Guid
  if ( ! CompareGuid (&VariableStoreHeader->Signature, &gEfiVariableGuid) &&
       ! CompareGuid (&VariableStoreHeader->Signature,
       &gEfiAuthenticatedVariableGuid))
  {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Variable Store Guid non-compatible\n"
      ));
    return EFI_NOT_FOUND;
  }

  VariableStoreLength = mFlashNvStorageVariableSize - FwVolHeader->HeaderLength;
  if (VariableStoreHeader->Size != VariableStoreLength) {
    DEBUG ((
     DEBUG_ERROR,
     "ValidateFvHeader: Variable Store Length does not match\n"
     ));
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}


/**
  The FvbGetAttributes() function retrieves the attributes and
  current settings of the block.

  @param This         Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

  @param Attributes   Pointer to EFI_FVB_ATTRIBUTES_2 in which the attributes and
                      current settings are returned.
                      Type EFI_FVB_ATTRIBUTES_2 is defined in
                      EFI_FIRMWARE_VOLUME_HEADER.

  @retval EFI_SUCCESS The firmware volume attributes were returned.

**/
EFI_STATUS
EFIAPI
FvbGetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL    *This,
  OUT       EFI_FVB_ATTRIBUTES_2                   *Attributes
  )
{
  EFI_FVB_ATTRIBUTES_2  FlashFvbAttributes;
  CONST FT_FVB_DEVICE    *FlashInstance;

  FlashInstance = INSTANCE_FROM_FVB_THIS (This);

  FlashFvbAttributes = (EFI_FVB_ATTRIBUTES_2) (
      EFI_FVB2_READ_ENABLED_CAP | // Reads may be enabled
      EFI_FVB2_READ_STATUS      | // Reads are currently enabled
      EFI_FVB2_STICKY_WRITE     | // A block erase is required to flip bits into EFI_FVB2_ERASE_POLARITY
      EFI_FVB2_MEMORY_MAPPED    | // It is memory mapped
      EFI_FVB2_ERASE_POLARITY     // After erasure all bits take this value (i.e. '1')
      );

  // Check if it is write protected
  if (FlashInstance->Media.ReadOnly != TRUE) {
    FlashFvbAttributes = FlashFvbAttributes         |
                         EFI_FVB2_WRITE_STATUS      | // Writes are currently enabled
                         EFI_FVB2_WRITE_ENABLED_CAP;  // Writes may be enabled
  }

  *Attributes = FlashFvbAttributes;

  return EFI_SUCCESS;
}


/**
  The FvbSetAttributes() function sets configurable firmware volume attributes
  and returns the new settings of the firmware volume.

  @param This                     EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

  @param Attributes               On input, Attributes is a pointer to
                                  EFI_FVB_ATTRIBUTES_2 that contains the desired
                                  firmware volume settings.
                                  On successful return, it contains the new
                                  settings of the firmware volume.

  @retval EFI_SUCCESS             The firmware volume attributes were returned.

  @retval EFI_INVALID_PARAMETER   The attributes requested are in conflict with
                                  the capabilities as declared in the firmware
                                  volume header.

**/
EFI_STATUS
EFIAPI
FvbSetAttributes (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN OUT    EFI_FVB_ATTRIBUTES_2                 *Attributes
  )
{
  return EFI_UNSUPPORTED;
}


/**
  The FvbGetPhysicalAddress() function retrieves the base address of
  a memory-mapped firmware volume. This function should be called
  only for memory-mapped firmware volumes.

  @param This               EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

  @param Address            Pointer to a caller-allocated
                            EFI_PHYSICAL_ADDRESS that, on successful
                            return from GetPhysicalAddress(), contains the
                            base address of the firmware volume.

  @retval EFI_SUCCESS       The firmware volume base address was returned.

  @retval EFI_NOT_SUPPORTED The firmware volume is not memory mapped.

**/
EFI_STATUS
EFIAPI
FvbGetPhysicalAddress (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  OUT       EFI_PHYSICAL_ADDRESS                 *Address
  )
{
  ASSERT (Address != NULL);

  *Address = mFlashNvStorageVariableBase;

  return EFI_SUCCESS;
}


/**
  The FvbGetBlockSize() function retrieves the size of the requested
  block. It also returns the number of additional blocks with
  the identical size. The FvbGetBlockSize() function is used to
  retrieve the block map (see EFI_FIRMWARE_VOLUME_HEADER).


  @param This                     EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

  @param Lba                      Indicates the block whose size to return.

  @param BlockSize                Pointer to a caller-allocated UINTN in which
                                  the size of the block is returned.

  @param NumberOfBlocks           Pointer to a caller-allocated UINTN in
                                  which the number of consecutive blocks,
                                  starting with Lba, is returned. All
                                  blocks in this range have a size of
                                  BlockSize.


  @retval EFI_SUCCESS             The firmware volume base address was returned.

  @retval EFI_INVALID_PARAMETER   The requested LBA is out of range.

**/
EFI_STATUS
EFIAPI
FvbGetBlockSize (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL  *This,
  IN        EFI_LBA                              Lba,
  OUT       UINTN                                *BlockSize,
  OUT       UINTN                                *NumberOfBlocks
  )
{
  EFI_STATUS Status;
  FT_FVB_DEVICE *FlashInstance;

  FlashInstance = INSTANCE_FROM_FVB_THIS (This);

  if (Lba > FlashInstance->Media.LastBlock) {
    Status = EFI_INVALID_PARAMETER;
  } else {
    // This is easy because in this platform each NorFlash device has equal sized blocks.
    *BlockSize = (UINTN) FlashInstance->Media.BlockSize;
    *NumberOfBlocks = (UINTN) (FlashInstance->Media.LastBlock - Lba + 1);
    Status = EFI_SUCCESS;
  }

  return Status;
}


/**
  Reads the specified number of bytes into a buffer from the specified block.

  The FvbRead() function reads the requested number of bytes from the
  requested block and stores them in the provided buffer.

  @param This                 EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

  @param Lba                  The starting logical block index from which to read.

  @param Offset               Offset into the block at which to begin reading.

  @param NumBytes             Pointer to a UINTN.
                              At entry, *NumBytes contains the total size of the
                              buffer.
                              At exit, *NumBytes contains the total number of
                              bytes read.

  @param Buffer               Pointer to a caller-allocated buffer that will be
                              used to hold the data that is read.

  @retval EFI_SUCCESS         The firmware volume was read successfully, and
                              contents are in Buffer.

  @retval EFI_BAD_BUFFER_SIZE Read attempted across an LBA boundary.
                              On output, NumBytes contains the total number of
                              bytes returned in Buffer.

  @retval EFI_ACCESS_DENIED   The firmware volume is in the ReadDisabled state.

  @retval EFI_DEVICE_ERROR    The block device is not functioning correctly and
                              could not be read.
**/
EFI_STATUS
EFIAPI
FvbRead (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL   *This,
  IN        EFI_LBA                               Lba,
  IN        UINTN                                 Offset,
  IN OUT    UINTN                                 *NumBytes,
  IN OUT    UINT8                                 *Buffer
  )
{
  UINTN              BlockSize;
  FT_FVB_DEVICE      *FlashInstance;
  EFI_STATUS         Status;

  FlashInstance = INSTANCE_FROM_FVB_THIS (This);

  // Cache the block size to avoid de-referencing pointers all the time
  BlockSize = FlashInstance->Media.BlockSize;

  // The read must not span block boundaries.
  // We need to check each variable individually because adding two large values together overflows.
  if ((Offset               >= BlockSize) ||
      (*NumBytes            >  BlockSize) ||
      ((Offset + *NumBytes) >  BlockSize)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // We must have some bytes to read
  if (*NumBytes == 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = FvbFlashRead (
             FlashInstance,
             FlashInstance->StartLba + Lba,
             Offset,
             *NumBytes,
             Buffer
             );
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}


/**
  Writes the specified number of bytes from the input buffer to the block.

  The FvbWrite() function writes the specified number of bytes from
  the provided buffer to the specified block and offset.

  @param This                 EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

  @param Lba                  The starting logical block index to write to.

  @param Offset               Offset into the block at which to begin writing.

  @param NumBytes             The pointer to a UINTN.
                              At entry, *NumBytes contains the total size of the
                              buffer.
                              At exit, *NumBytes contains the total number of
                              bytes actually written.

  @param Buffer               The pointer to a caller-allocated buffer that
                              contains the source for the write.

  @retval EFI_SUCCESS         The firmware volume was written successfully.

  @retval EFI_BAD_BUFFER_SIZE The write was attempted across an LBA boundary.
                              On output, NumBytes contains the total number of
                              bytes actually written.

  @retval EFI_ACCESS_DENIED   The firmware volume is in the WriteDisabled state.

  @retval EFI_DEVICE_ERROR    The block device is malfunctioning and could not be
                              written.

**/
EFI_STATUS
EFIAPI
FvbWrite (
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL   *This,
  IN        EFI_LBA                               Lba,
  IN        UINTN                                 Offset,
  IN OUT    UINTN                                 *NumBytes,
  IN        UINT8                                 *Buffer
  )
{
  FT_FVB_DEVICE *FlashInstance;

  FlashInstance = INSTANCE_FROM_FVB_THIS (This);

  return FvbFlashWrite (FlashInstance,
           FlashInstance->StartLba + Lba,
           Offset,
           *NumBytes,
           Buffer
           );
}


/**
  Erases and initialises a firmware volume block.

  The FvbEraseBlocks() function erases one or more blocks as denoted
  by the variable argument list.

  @param This                     EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL
                                  instance.

  @param ...                      The variable argument list is a list of tuples.
                                  Each tuple describes a range of LBAs to erase
                                  and consists of the following:
                                  An EFI_LBA that indicates the starting LBA
                                  A UINTN that indicates the number of blocks
                                  to erase.

                                  The list is terminated with an
                                  EFI_LBA_LIST_TERMINATOR.

  @retval EFI_SUCCESS             The erase request successfully completed.

  @retval EFI_ACCESS_DENIED       The firmware volume is in the WriteDisabled
                                  state.

  @retval EFI_DEVICE_ERROR        The block device is not functioning correctly
                                  and could not be written.
                                  The firmware device may have been partially
                                  erased.

  @retval EFI_INVALID_PARAMETER   One or more of the LBAs listed in the variable
                                  argument list do not exist in the firmware
                                  volume.

**/
EFI_STATUS
EFIAPI
FvbEraseBlocks (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL *This,
  ...
  )
{
  EFI_STATUS         Status;
  VA_LIST            Args;
  UINTN              BlockAddress; // Physical address of Lba to erase
  EFI_LBA            StartingLba;  // Lba from which we start erasing
  UINTN              NumOfLba;     // Number of Lba blocks to erase
  FT_FVB_DEVICE      *Instance;

  Instance = INSTANCE_FROM_FVB_THIS (This);

  Status = EFI_SUCCESS;

  // Detect WriteDisabled state
  if (Instance->Media.ReadOnly == TRUE) {
    // Firmware volume is in WriteDisabled state
    DEBUG ((
      DEBUG_ERROR,
      "FvbEraseBlocks: ERROR - Device is in WriteDisabled state.\n"
      ));
    return EFI_ACCESS_DENIED;
  }

  // Before erasing, check the entire list of parameters to ensure all specified blocks are valid

  VA_START (Args, This);
  do {
    // Get the Lba from which we start erasing
    StartingLba = VA_ARG (Args, EFI_LBA);

    // Have we reached the end of the list?
    if (StartingLba == EFI_LBA_LIST_TERMINATOR) {
      //Exit the while loop
      break;
    }

    // How many Lba blocks are we requested to erase?
    NumOfLba = VA_ARG (Args, UINT32);

    // All blocks must be within range
    if ((NumOfLba == 0) || ((Instance->StartLba + StartingLba + NumOfLba - 1) > Instance->Media.LastBlock)) {
      VA_END (Args);
      DEBUG ((
        DEBUG_ERROR,
        "FvbEraseBlocks: ERROR - Lba range goes past the last Lba.\n"
        ));
      Status = EFI_INVALID_PARAMETER;
      goto EXIT;
    }
  } while (TRUE);

  VA_END (Args);

  //
  // To get here, all must be ok, so start erasing
  //
  VA_START (Args, This);
  do {
    // Get the Lba from which we start erasing
    StartingLba = VA_ARG (Args, EFI_LBA);

    // Have we reached the end of the list?
    if (StartingLba == EFI_LBA_LIST_TERMINATOR) {
      // Exit the while loop
      break;
    }

    // How many Lba blocks are we requested to erase?
    NumOfLba = VA_ARG (Args, UINT32);

    // Go through each one and erase it
    while (NumOfLba > 0) {
      // Get the physical address of Lba to erase
      BlockAddress = GET_DATA_OFFSET (
                       Instance->RegionBaseAddress,
                       Instance->StartLba + StartingLba,
                       Instance->Media.BlockSize
                       );

      // Erase it
      Status = FvbFlashEraseSingleBlock (Instance, BlockAddress);
      if (EFI_ERROR (Status)) {
        VA_END (Args);
        Status = EFI_DEVICE_ERROR;
        goto EXIT;
      }

      // Move to the next Lba
      StartingLba++;
      NumOfLba--;
    }
  } while (TRUE);

  VA_END (Args);

EXIT:
  return Status;
}


/**
  This function inited the NorFlash instance.

  @param[in][out] FlashInstance   The pointer of FT_FVB_DEVICE instance.

  @retval EFI_SUCCESS             PhytNorFlashFvbInitialize() is executed successfully.

**/
STATIC
EFI_STATUS
PhytNorFlashFvbInitialize (
  IN OUT FT_FVB_DEVICE *FlashInstance
  )
{
  EFI_STATUS     Status;
  UINT32         FvbNumLba;
  EFI_BOOT_MODE  BootMode;
  UINTN          TotalFvbSize;

  // Declare the Non-Volatile storage as EFI_MEMORY_RUNTIME
  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  mFlashNvStorageVariableBase,
                  FlashInstance->FvbSize,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gDS->SetMemorySpaceAttributes (
                  mFlashNvStorageVariableBase,
                  FlashInstance->FvbSize,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  ASSERT_EFI_ERROR (Status);

  TotalFvbSize = FlashInstance->FvbSize;

  // Set the index of the first LBA for the FVB
  FlashInstance->StartLba = (mFlashNvStorageVariableBase - FlashInstance->RegionBaseAddress) / FlashInstance->Media.BlockSize;

  BootMode = GetBootModeHob ();
  if (BootMode == BOOT_WITH_DEFAULT_SETTINGS) {
    Status = EFI_INVALID_PARAMETER;
  } else {
    // Determine if there is a valid header at the beginning of the NorFlash
    Status = FvbValidateFvHeader (FlashInstance);
  }

  // Install the Default FVB header if required
  if (EFI_ERROR (Status)) {
    // There is no valid header, so time to install one.
    DEBUG ((
      DEBUG_ERROR,
      "NorFlashFvbInitialize: ERROR - The FVB Header is invalid. Installing a correct one for this volume.\n"
      ));

    // Erase all the NorFlash that is reserved for variable storage
    FvbNumLba = TotalFvbSize / FlashInstance->Media.BlockSize;

    Status = FvbEraseBlocks (
               &FlashInstance->FvbProtocol,
               (EFI_LBA)0,
               FvbNumLba,
               EFI_LBA_LIST_TERMINATOR
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    // Install all appropriate headers
    Status = FvbInitFvAndVariableStoreHeaders (FlashInstance);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return Status;
}


/**
  The CreateInstance() function Create Fvb Instance.

  @retval  EFI_SUCCESS         Create Instance successfully.

  @retval  other               Create Instance failed.

**/
STATIC
EFI_STATUS
CreateInstance (
  VOID
  )
{
  EFI_STATUS Status;
  NOR_FLASH_DEVICE_DESCRIPTION   *NorFlashDevice;

  // Locate flash protocols
  Status = gBS->LocateProtocol (&gSpiNorFlashProtocolGuid,
                  NULL,
                  (VOID **)&FvbDevice->SpiFlashProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "Cannot locate NorFlash protocol.\n"
      ));
    return Status;
  }

  NorFlashDevice = AllocateRuntimePool (sizeof (NOR_FLASH_DEVICE_DESCRIPTION));
  if (NorFlashDevice == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "Cannot Allocate NorFlashDevice Pool.\n"
      ));
    return Status;
  }

  Status = FvbDevice->SpiFlashProtocol->GetDevices (NorFlashDevice);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FvbDevice->SpiFlashProtocol->Initialization ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FvbDevice->DeviceBaseAddress = NorFlashDevice->DeviceBaseAddress;
  FvbDevice->RegionBaseAddress = NorFlashDevice->RegionBaseAddress;
  FvbDevice->Size = NorFlashDevice->Size;

  FvbDevice->Media.MediaId = 0;
  FvbDevice->Media.BlockSize = NorFlashDevice->BlockSize;
  FvbDevice->Media.LastBlock = (FvbDevice->Size / FvbDevice->Media.BlockSize) - 1;
  FvbDevice->FvbSize = mFlashNvStorageVariableSize +
                       mFlashNvStorageFtwWorkingSize +
                       mFlashNvStorageFtwSpareSize;

  CopyGuid (&FvbDevice->DevicePath.Vendor.Guid, &NorFlashDevice->Guid);

  FvbDevice->ShadowBuffer = AllocateRuntimePool (FvbDevice->Media.BlockSize);
  if (FvbDevice->ShadowBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
             &FvbDevice->Handle,
             &gEfiDevicePathProtocolGuid,
             &FvbDevice->DevicePath,
             &gEfiFirmwareVolumeBlockProtocolGuid,
             &FvbDevice->FvbProtocol,
             NULL
             );
  if (EFI_ERROR (Status)) {
    FreePool (FvbDevice);
    return Status;
  }

  Status = PhytNorFlashFvbInitialize (FvbDevice);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "PhytNorFlashFvbInitialize: Fail to init NorFlash devices\n"
      ));
    return Status;
  }

  FreePool (NorFlashDevice);

  return Status;
}


/**
  Fixup internal data so that EFI can be call in virtual mode.
  Call the passed in Child Notify event and convert any pointers
  in lib to virtual mode.

  @param[in]    Event   The Event that is being processed.

  @param[in]    Context Event Context.

  @retval       None.

**/
STATIC
VOID
EFIAPI
FvbVirtualNotifyEvent (
  IN EFI_EVENT        Event,
  IN VOID             *Context
  )
{
  // Convert SpiFlashProtocol
  EfiConvertPointer (0x0, (VOID **)&FvbDevice->SpiFlashProtocol->Erase);
  EfiConvertPointer (0x0, (VOID **)&FvbDevice->SpiFlashProtocol->Write);
  EfiConvertPointer (0x0, (VOID **)&FvbDevice->SpiFlashProtocol->Read);
  EfiConvertPointer (0x0, (VOID **)&FvbDevice->SpiFlashProtocol->GetDevices);
  EfiConvertPointer (0x0, (VOID **)&FvbDevice->SpiFlashProtocol->Initialization);
  EfiConvertPointer (0x0, (VOID **)&FvbDevice->SpiFlashProtocol->EraseSingleBlock);
  EfiConvertPointer (0x0, (VOID **)&FvbDevice->SpiFlashProtocol);

  EfiConvertPointer (0x0, (VOID **)&mFlashNvStorageVariableBase);
  EfiConvertPointer (0x0, (VOID **)&mFlashNvStorageFtwWorkingBase);
  EfiConvertPointer (0x0, (VOID **)&mFlashNvStorageFtwSpareBase);
  EfiConvertPointer (0x0, (VOID **)&FvbDevice);

  return;
}


/**
  This function is the entrypoint of the fvb driver.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.

  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.

  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
FvbEntryPoint (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE    *SystemTable
  )
{
  EFI_STATUS                     Status;

  FvbDevice = AllocateRuntimeCopyPool (
                sizeof (FvbFlashInstanceTemplate),
                &FvbFlashInstanceTemplate
                );
  if (FvbDevice == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mFlashNvStorageVariableBase   = FixedPcdGet64 (PcdFlashNvStorageVariableBase64);
  mFlashNvStorageFtwWorkingBase = FixedPcdGet64 (PcdFlashNvStorageFtwWorkingBase64);
  mFlashNvStorageFtwSpareBase   = FixedPcdGet64 (PcdFlashNvStorageFtwSpareBase64);
  mFlashNvStorageVariableSize   = FixedPcdGet32 (PcdFlashNvStorageVariableSize);
  mFlashNvStorageFtwWorkingSize = FixedPcdGet32 (PcdFlashNvStorageFtwWorkingSize);
  mFlashNvStorageFtwSpareSize   = FixedPcdGet32 (PcdFlashNvStorageFtwSpareSize);

  Status = CreateInstance ();
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "CreateInstance: Fail to create instance for NorFlash\n"
      ));
  }

//
// Register for the virtual address change event
//
  Status = gBS->CreateEventEx (
             EVT_NOTIFY_SIGNAL,
             TPL_NOTIFY,
             FvbVirtualNotifyEvent,
             NULL,
             &gEfiEventVirtualAddressChangeGuid,
             &FvbVirtualAddrChangeEvent
             );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
