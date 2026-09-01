#include "RuntimeConfiguration.h"

#include "stm32g4xx_hal.h"

#include <cstddef>
#include <cstring>

namespace RuntimeConfiguration
{
    namespace
    {
        constexpr uint32_t FLASH_BASE_ADDRESS = 0x08000000UL;
        constexpr uint32_t FLASH_TOTAL_BYTES = 512U * 1024U;
        constexpr uint32_t FLASH_PAGE_BYTES = 2U * 1024U;
        constexpr uint32_t CONFIG_BYTES = 4U * 1024U;
        constexpr uint32_t CONFIG_BASE_ADDRESS = FLASH_BASE_ADDRESS + FLASH_TOTAL_BYTES - CONFIG_BYTES;
        constexpr uint32_t SLOT_BYTES = FLASH_PAGE_BYTES;
        constexpr uint32_t RECORD_MAGIC = 0x46424331UL; // "FBC1"
        constexpr uint32_t MICROOHMS_PER_OHM = 1'000'000U;

        struct Record
        {
            uint32_t magic;
            uint16_t version;
            uint16_t reserved;
            uint32_t generation;
            uint8_t slaveCount;
            uint8_t currentSenseSlave;
            uint8_t invertCurrent;
            uint8_t balanceEnabled;
            uint32_t shuntResistanceMicroOhms;
            uint32_t batteryCapacityMilliAh;
            uint32_t reservedData;
            uint32_t crc;
        };
        static_assert(sizeof(Record) == 32U);
        static_assert(alignof(Record) <= 8U);

        constexpr Values DEFAULT_VALUES = {
            .slaveCount = 1U,
            .currentSenseSlave = 1U,
            .shuntResistanceMicroOhms = 10'000U,
            .batteryCapacityMilliAh = 314'000U,
            .invertCurrent = false,
            .balanceEnabled = true,
        };

        uint32_t crc32(const uint8_t *data, size_t length)
        {
            uint32_t crc = 0xFFFFFFFFU;
            for (size_t index = 0U; index < length; ++index)
            {
                crc ^= data[index];
                for (uint8_t bit = 0U; bit < 8U; ++bit)
                {
                    crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
                }
            }
            return crc ^ 0xFFFFFFFFU;
        }

        bool isBlank(const Record &record)
        {
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
            for (size_t index = 0U; index < sizeof(record); ++index)
            {
                if (bytes[index] != 0xFFU) return false;
            }
            return true;
        }

        bool isRecordValid(const Record &record)
        {
            return record.magic == RECORD_MAGIC &&
                   record.version != 0U &&
                   record.balanceEnabled <= 1U &&
                   crc32(reinterpret_cast<const uint8_t *>(&record), offsetof(Record, crc)) == record.crc;
        }

        Values valuesFromRecord(const Record &record)
        {
            return {
                .slaveCount = record.slaveCount,
                .currentSenseSlave = record.currentSenseSlave,
                .shuntResistanceMicroOhms = record.shuntResistanceMicroOhms,
                .batteryCapacityMilliAh = record.batteryCapacityMilliAh,
                .invertCurrent = record.invertCurrent != 0U,
                .balanceEnabled = record.balanceEnabled != 0U,
            };
        }

        Record recordFromValues(const Values &values, uint32_t generation)
        {
            Record record{};
            record.magic = RECORD_MAGIC;
            record.version = CONFIG_VERSION;
            record.generation = generation;
            record.slaveCount = values.slaveCount;
            record.currentSenseSlave = values.currentSenseSlave;
            record.invertCurrent = values.invertCurrent ? 1U : 0U;
            record.shuntResistanceMicroOhms = values.shuntResistanceMicroOhms;
            record.batteryCapacityMilliAh = values.batteryCapacityMilliAh;
            record.balanceEnabled = values.balanceEnabled ? 1U : 0U;
            record.crc = crc32(reinterpret_cast<const uint8_t *>(&record), offsetof(Record, crc));
            return record;
        }

        const Record &recordAt(uint32_t address)
        {
            return *reinterpret_cast<const Record *>(address);
        }

        uint32_t pageForAddress(uint32_t address)
        {
            return (address - FLASH_BASE_ADDRESS) / FLASH_PAGE_BYTES;
        }

        uint32_t bankForAddress(uint32_t address)
        {
            (void)address;
            return FLASH_BANK_1;
        }

        bool eraseSlot(uint32_t address)
        {
            FLASH_EraseInitTypeDef erase{};
            erase.TypeErase = FLASH_TYPEERASE_PAGES;
            erase.Banks = bankForAddress(address);
            erase.Page = pageForAddress(address);
            erase.NbPages = 1U;
            uint32_t pageError = 0U;
            return HAL_FLASHEx_Erase(&erase, &pageError) == HAL_OK;
        }

        bool programRecord(uint32_t address, const Record &record)
        {
            const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
            for (uint32_t offset = 0U; offset < sizeof(record); offset += sizeof(uint64_t))
            {
                uint64_t doubleWord = UINT64_MAX;
                std::memcpy(&doubleWord, bytes + offset, sizeof(doubleWord));
                if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + offset, doubleWord) != HAL_OK)
                {
                    return false;
                }
            }
            return std::memcmp(reinterpret_cast<const void *>(address), &record, sizeof(record)) == 0;
        }

    }

    const Values &defaults()
    {
        return DEFAULT_VALUES;
    }

    bool validate(const Values &values)
    {
        if (values.slaveCount == 0U || values.slaveCount > MAX_SLAVES ||
            values.currentSenseSlave > values.slaveCount ||
            values.shuntResistanceMicroOhms == 0U || values.shuntResistanceMicroOhms > MICROOHMS_PER_OHM ||
            values.batteryCapacityMilliAh == 0U || values.batteryCapacityMilliAh > 10'000'000U)
        {
            return false;
        }
        return true;
    }

    LoadResult load()
    {
        const uint32_t firstAddress = CONFIG_BASE_ADDRESS;
        const uint32_t secondAddress = CONFIG_BASE_ADDRESS + SLOT_BYTES;
        const Record &first = recordAt(firstAddress);
        const Record &second = recordAt(secondAddress);
        const bool firstValid = isRecordValid(first) && first.version == CONFIG_VERSION && validate(valuesFromRecord(first));
        const bool secondValid = isRecordValid(second) && second.version == CONFIG_VERSION && validate(valuesFromRecord(second));

        if (firstValid || secondValid)
        {
            const Record &selected = firstValid && (!secondValid || first.generation >= second.generation) ? first : second;
            return {LoadStatus::Valid, selected.version, valuesFromRecord(selected)};
        }

        const bool firstBlank = isBlank(first);
        const bool secondBlank = isBlank(second);
        const bool firstHasExpectedVersion = !firstBlank && first.magic == RECORD_MAGIC && first.version == CONFIG_VERSION;
        const bool secondHasExpectedVersion = !secondBlank && second.magic == RECORD_MAGIC && second.version == CONFIG_VERSION;
        if (firstHasExpectedVersion || secondHasExpectedVersion)
        {
            return {LoadStatus::Corrupt, CONFIG_VERSION, {}};
        }
        const Record *metadata = nullptr;
        if (!firstBlank && isRecordValid(first)) metadata = &first;
        if (!secondBlank && isRecordValid(second) && (metadata == nullptr || second.generation >= metadata->generation)) metadata = &second;
        if (metadata != nullptr && metadata->version != CONFIG_VERSION)
        {
            return {LoadStatus::VersionMismatch, metadata->version, {}};
        }
        if (firstBlank && secondBlank) return {LoadStatus::Blank, 0U, {}};
        return {LoadStatus::Corrupt, metadata == nullptr ? static_cast<uint16_t>(0U) : metadata->version, {}};
    }

    bool save(const Values &values)
    {
        if (!validate(values)) return false;

        const uint32_t firstAddress = CONFIG_BASE_ADDRESS;
        const uint32_t secondAddress = CONFIG_BASE_ADDRESS + SLOT_BYTES;
        const Record &first = recordAt(firstAddress);
        const Record &second = recordAt(secondAddress);
        const bool firstValid = isRecordValid(first) && first.version == CONFIG_VERSION && validate(valuesFromRecord(first));
        const bool secondValid = isRecordValid(second) && second.version == CONFIG_VERSION && validate(valuesFromRecord(second));
        const uint32_t generation = (firstValid && secondValid) ? (first.generation >= second.generation ? first.generation : second.generation) + 1U :
                                     (firstValid ? first.generation + 1U : (secondValid ? second.generation + 1U : 1U));
        const uint32_t targetAddress = firstValid && (!secondValid || first.generation >= second.generation) ? secondAddress :
                                       secondValid ? firstAddress :
                                       isBlank(first) ? firstAddress :
                                       isBlank(second) ? secondAddress : firstAddress;

        HAL_FLASH_Unlock();
        const bool erased = eraseSlot(targetAddress);
        const Record record = recordFromValues(values, generation);
        const bool programmed = erased && programRecord(targetAddress, record);
        HAL_FLASH_Lock();
        return programmed;
    }

    bool updateBalanceEnabled(bool enabled)
    {
        const LoadResult current = load();
        if (current.status != LoadStatus::Valid) return false;
        if (current.values.balanceEnabled == enabled) return true;

        Values updated = current.values;
        updated.balanceEnabled = enabled;
        return save(updated);
    }
}
