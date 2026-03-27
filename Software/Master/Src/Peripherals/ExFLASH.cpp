#include "ExFLASH.h"

#define DEBUG_LVL 2
#include "debug.h"

QSPI_CommandTypeDef ExFLASH::getCommand()
{
    QSPI_CommandTypeDef command;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.Instruction = 0;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_8_BITS;
    command.Address = 0;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.AlternateBytes = 0;
    command.AlternateBytesSize = 0;
    command.DataMode = QSPI_DATA_NONE;
    command.DummyCycles = 0;
    command.NbData = 0;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return command;
}

ExFLASH::ExFLASH(QSPI_HandleTypeDef *qspiHandle) : mQspiHandle(qspiHandle)
{
}

void ExFLASH::setup()
{
    // HAL_QSPI_
}


bool ExFLASH::getID(uint8_t &manufacturerID, uint8_t &deviceID)
{
    uint8_t data[2];
    QSPI_CommandTypeDef command = getCommand();
    command.Instruction = READ_ID;
    command.Address = 0;
    if (HAL_QSPI_Command(mQspiHandle, &command, 10) != HAL_OK)
    {
        PRINTF_ERR("[ExFLASH] Failed to send command\n");
        return false;
    }

    if (HAL_QSPI_Receive(mQspiHandle, data, 20) != HAL_OK)
    {
        PRINTF_ERR("[ExFLASH] Failed to receive data, err: %u\n", mQspiHandle->ErrorCode);
        return false;
    }
    manufacturerID = data[0];
    deviceID = data[1];
    return true;
}

bool ExFLASH::read(uint32_t address, uint8_t *data, uint32_t size)
{
    // QSPI_CommandTypeDef command;
    // command.Instruction = 
    return true;
}