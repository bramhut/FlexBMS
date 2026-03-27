#pragma once

#include "main.h"
#include "quadspi.h"

class ExFLASH
{
    QSPI_HandleTypeDef * const mQspiHandle;

    // List of commands available
    enum OpCodes_t{
        WRITE_DISABLE = 0x04,
        WRITE_ENABLE = 0x06,
        BLOCK_ERASE = 0xD8,
        PROGRAM_LOAD = 0x02,
        PROGRAM_LOADx4 = 0x32,
        PROGRAM_EXECUTE = 0x10,
        PROGRAM_LOAD_RANDOM_DATA = 0x84,
        PROGRAM_LOAD_RANDOM_DATAx4 = 0xC4,
        PROGRAM_LOAD_RANDOM_DATA_QUAD = 0x72,
        PAGE_READ = 0x13,
        READ_FROM_CACHEx1 = 0x03,
        READ_FROM_CACHEx2 = 0x3B,
        READ_FROM_CACHEx4 = 0x6B,
        READ_FROM_CACHE_DUAL = 0xBB,
        READ_FROM_CACHE_QUAD = 0xEB,
        READ_ID = 0x9F,
        RESET = 0xFF,
        GET_FEATURE = 0x0F,
        SET_FEATURE = 0x1F
    };
private:
    // Private methods
    QSPI_CommandTypeDef getCommand();

public:
    ExFLASH(QSPI_HandleTypeDef *qspiHandle);
    
    void setup();
    bool getID(uint8_t &manufacturerID, uint8_t &deviceID);
    bool read(uint32_t address, uint8_t *data, uint32_t size);
};
