#pragma once
// Host-only flash emulator. Never included by firmware builds.
#include <cstdint>
#include <cstring>
struct FLASH_EraseInitTypeDef { uint32_t TypeErase, Banks, Page, NbPages; };
constexpr unsigned FLASH_TYPEERASE_PAGES=0, FLASH_BANK_1=1, FLASH_TYPEPROGRAM_DOUBLEWORD=0;
constexpr int HAL_OK=0;
inline void HAL_FLASH_Unlock() {}
inline void HAL_FLASH_Lock() {}
inline int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *) {
    std::memset(reinterpret_cast<void *>(uintptr_t(0x08000000U+e->Page*2048U)), 0xff, e->NbPages*2048U); return HAL_OK;
}
inline int HAL_FLASH_Program(unsigned, uint32_t address, uint64_t value) {
    std::memcpy(reinterpret_cast<void *>(uintptr_t(address)), &value, sizeof(value)); return HAL_OK;
}
