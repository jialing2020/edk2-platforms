/** @file
  Phytium NorFlash Drivers.

  Copyright (C) 2020, Phytium Technology Co Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SpiNorFlashDxe.h"

typedef struct {
  UINT32 Flash_Index;
  UINT32 Flash_Write;
  UINT32 Flash_Erase;
  UINT32 Flash_Pp;
}FLASH_CMD_INFO;

STATIC EFI_EVENT       mSpiNorFlashVirtualAddrChangeEvent;
STATIC UINTN           mNorFlashControlBase;
STATIC UINT32          mCmd_Write;
STATIC UINT32          mCmd_Eares;
STATIC UINT32          mCmd_Pp;

#define SPI_FLASH_BASE           FixedPcdGet64 (PcdSpiFlashBase)
#define SPI_FLASH_SIZE           FixedPcdGet64 (PcdSpiFlashSize)

EFI_SPI_DRV_PROTOCOL *pSpiMasterProtocol;

NorFlash_Device *flash_Instance;
extern EFI_GUID gSpiMasterProtocolGuid;
extern EFI_GUID gSpiNorFlashProtocolGuid;

NOR_FLASH_DEVICE_DESCRIPTION mNorFlashDevices = {
    SPI_FLASH_BASE,   /* Device Base Address */
    SPI_FLASH_BASE,   /* Region Base Address */
    SIZE_1MB * 16,    /* Size */
    SIZE_64KB,        /* Block Size */
    {0xE7223039, 0x5836, 0x41E1, { 0xB5, 0x42, 0xD7, 0xEC, 0x73, 0x6C, 0x5E, 0x59 } }
};


/**
  This function writed up to 256 bytes to flash through spi driver.

  @param[in] Address             The address of the flash.
  @param[in] Buffer              The pointer of buffer to be writed.
  @param[in] BufferSizeInBytes   The bytes to be writed.

  @retval EFI_SUCCESS           NorFlashWrite256() is executed successfully.

**/
STATIC
EFI_STATUS
NorFlashWrite256 (
  IN UINTN            Address,
  IN VOID             *Buffer,
  IN UINT32           BufferSizeInBytes
  )
{
  UINT32     Index;
  UINT8      Cmd_id;
  UINT32     *TemBuffer;

  TemBuffer= Buffer;

  if(BufferSizeInBytes > 256) {
    DEBUG((DEBUG_ERROR, "The max length is 256 bytes.\n"));
    return EFI_INVALID_PARAMETER;
  }

  if(BufferSizeInBytes % 4 != 0) {
    DEBUG((DEBUG_ERROR, "The length must four bytes aligned.\n"));
    return EFI_INVALID_PARAMETER;
  }

  if(Address % 4 != 0) {
    DEBUG((DEBUG_ERROR, "The address must four bytes aligned.\n"));
    return EFI_INVALID_PARAMETER;
  }

  Cmd_id = (UINT8)(mCmd_Pp & 0xff);
  pSpiMasterProtocol->SpiSetConfig (Cmd_id, 0x400000, REG_CMD_PORT);
  pSpiMasterProtocol->SpiSetConfig (0, 0x1, REG_LD_PORT);

  Cmd_id = (UINT8)(mCmd_Write & 0xff);
  pSpiMasterProtocol->SpiSetConfig (Cmd_id, 0x000208, REG_WR_CFG);

  for(Index = 0; Index < BufferSizeInBytes / 4; Index++) {
    MmioWrite32(Address + Index * 4, TemBuffer[Index]);
  }

  pSpiMasterProtocol->SpiSetConfig (0, 0x1, REG_FLUSH_REG);

  pSpiMasterProtocol->SpiSetConfig (0, 0x0, REG_WR_CFG);

  return EFI_SUCCESS;
}

/**
  This function erased a sector of flash through spi driver.

  @param[in] BlockAddress  The sector address to be erased.

  @retval    None.

**/
STATIC
inline void
NorFlashPlatformEraseSector (
  IN  UINTN BlockAddress
  )
{
  UINT8 Cmd_id = 0;

  Cmd_id = (UINT8)(mCmd_Pp & 0xff);
  pSpiMasterProtocol->SpiSetConfig (Cmd_id, 0x400000, REG_CMD_PORT);
  pSpiMasterProtocol->SpiSetConfig (0, 0x1, REG_LD_PORT);

  Cmd_id = (UINT8)(mCmd_Eares & 0xff);
  pSpiMasterProtocol->SpiSetConfig (Cmd_id, 0x408000, REG_CMD_PORT);
  pSpiMasterProtocol->SpiSetConfig (0, BlockAddress, REG_ADDR_PORT);
  pSpiMasterProtocol->SpiSetConfig (0, 0x1, REG_LD_PORT);

}


/**
  Fixup internal data so that EFI can be call in virtual mode.
  Call the passed in Child Notify event and convert any pointers in
  lib to virtual mode.

  @param[in] Event   The Event that is being processed.

  @param[in] Context Event Context.

  @retval            None.

**/
VOID
EFIAPI
PlatformNorFlashVirtualNotifyEvent (
  IN EFI_EVENT            Event,
  IN VOID                 *Context
  )
{
  EfiConvertPointer (0x0, (VOID **)&mNorFlashControlBase);
  EfiConvertPointer (0x0, (VOID**)&pSpiMasterProtocol->SpiGetConfig);
  EfiConvertPointer (0x0, (VOID**)&pSpiMasterProtocol->SpiSetConfig);
  EfiConvertPointer (0x0, (VOID**)&pSpiMasterProtocol);
}


/**
  This function inited the flash platform.

  @param None.

  @retval EFI_SUCCESS           NorFlashPlatformInitialization() is executed successfully.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformInitialization (
  VOID
  )
{

  mCmd_Write = 0x2;
  mCmd_Eares = 0xD8;
  mCmd_Pp =    0x6;

  mNorFlashControlBase = FixedPcdGet64 (PcdSpiControllerBase);

  return EFI_SUCCESS;
}


/**
  This function geted the flash device information.

  @param[out] NorFlashDevices    the pointer to store flash device information.
  @param[out] Count              the number of the flash device.

  @retval EFI_SUCCESS           NorFlashPlatformGetDevices() is executed successfully.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformGetDevices (
  OUT NOR_FLASH_DEVICE_DESCRIPTION   *NorFlashDevices
  )
{

  *NorFlashDevices = mNorFlashDevices;

  return EFI_SUCCESS;
}


/**
  This function readed flash content form the specified area of flash.

  @param[in] Address             The address of the flash.
  @param[in] Buffer              The pointer of the Buffer to be stored.
  @param[out] Len                The bytes readed form flash.

  @retval EFI_SUCCESS            NorFlashPlatformRead() is executed successfully.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformRead (
  IN UINTN                Address,
  IN VOID                 *Buffer,
  OUT UINT32              Len
  )
{

  DEBUG((DEBUG_BLKIO, "NorFlashPlatformRead: Address: 0x%lx Buffer:0x%p Len:0x%x\n", Address, Buffer, Len));

  CopyMem ((VOID *)Buffer, (VOID *)Address, Len);

  return EFI_SUCCESS;
}


/**
  This function erased one block flash content.

  @param[in] BlockAddress        the BlockAddress to be erased.

  @retval EFI_SUCCESS            NorFlashPlatformEraseSingleBlock() is executed successfully.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformEraseSingleBlock (
  IN UINTN            BlockAddress
  )
{

  NorFlashPlatformEraseSector (BlockAddress);

  return EFI_SUCCESS;
}


/**
  This function erased the flash content of the specified area.

  @param[in] Offset              the offset of the flash.
  @param[in] Length              length to be erased.

  @retval EFI_SUCCESS            NorFlashPlatformErase() is executed successfully.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformErase (
  IN UINT64                  Offset,
  IN UINT64                  Length
  )
{
  EFI_STATUS     Status;
  UINT64         Index;
  UINT64         Count;

  Status = EFI_SUCCESS;
  if ((Length % SIZE_64KB) == 0) {
    Count = Length / SIZE_64KB;
    for (Index = 0; Index < Count; Index++) {
      NorFlashPlatformEraseSingleBlock (Offset);
      Offset += SIZE_64KB;
    }
  } else {
    Status = EFI_INVALID_PARAMETER;
  }

  return Status;
}


/**
  This function writed data to flash.

  @param[in] Address             the address of the flash.

  @param[in] Buffer              the pointer of the Buffer to be writed.

  @param[in] BufferSizeInBytes   the bytes of the Buffer.

  @retval EFI_SUCCESS            NorFlashPlatformWrite() is executed successfully.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformWrite (
  IN UINTN            Address,
  IN VOID             *Buffer,
  IN UINT32           BufferSizeInBytes
  )
{
  UINT32 Index;
  UINT32 Remainder;
  UINT32 Quotient;
  EFI_STATUS Status;
  UINTN TmpAddress;

  Index = 0;
  Remainder = 0;
  Quotient = 0;
  TmpAddress = Address;
  Remainder  = BufferSizeInBytes % 256;
  Quotient   = BufferSizeInBytes / 256;

  if(BufferSizeInBytes <= 256) {
    Status = NorFlashWrite256 (TmpAddress, Buffer, BufferSizeInBytes);
  } else {
    for(Index = 0; Index < Quotient; Index++) {
        Status = NorFlashWrite256 (TmpAddress, Buffer, 256);
        TmpAddress += 256;
        Buffer += 256;
    }

    if(Remainder != 0) {
      Status = NorFlashWrite256 (TmpAddress, Buffer, Remainder);
    }
  }

  if(EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
  }

  return EFI_SUCCESS;

}


/**
  This function inited the flash driver protocol.

  @param[in] NorFlashProtocol    A pointer to the norflash protocol struct.

  @retval EFI_SUCCESS       NorFlashPlatformInitProtocol() is executed successfully.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformInitProtocol (
  IN EFI_NORFLASH_DRV_PROTOCOL *NorFlashProtocol
  )
{
  NorFlashProtocol->Initialization    = NorFlashPlatformInitialization;
  NorFlashProtocol->GetDevices        = NorFlashPlatformGetDevices;
  NorFlashProtocol->Erase             = NorFlashPlatformErase;
  NorFlashProtocol->EraseSingleBlock  = NorFlashPlatformEraseSingleBlock;
  NorFlashProtocol->Read              = NorFlashPlatformRead;
  NorFlashProtocol->Write             = NorFlashPlatformWrite;

  return EFI_SUCCESS;
}


/**
  This function is the entrypoint of the norflash driver.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.

  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.

  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
NorFlashPlatformEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS     Status;

  Status = gBS->LocateProtocol (
    &gSpiMasterProtocolGuid,
    NULL,
    (VOID **)&pSpiMasterProtocol
  );
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  flash_Instance = AllocateRuntimeZeroPool (sizeof (NorFlash_Device));
  if (flash_Instance == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NorFlashPlatformInitProtocol (&flash_Instance->FlashProtocol);

  flash_Instance->Signature = NORFLASH_SIGNATURE;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &(flash_Instance->Handle),
                  &gSpiNorFlashProtocolGuid,
                  &(flash_Instance->FlashProtocol),
                  NULL
                  );
  ASSERT_EFI_ERROR(Status);

  //Register for the virtual address change event
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  PlatformNorFlashVirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mSpiNorFlashVirtualAddrChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

