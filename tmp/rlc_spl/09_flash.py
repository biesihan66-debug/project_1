from support import read, replace, replace_region, write


path = 'System/Flash_Calibration.c'
flash = read(path)
flash = replace_region(flash, '#include "BSP_PinMap.h"', '#define RECORD_MAGIC',
    '''#include "Flash_Calibration.h"
#include "stm32f10x.h"
#include "BSP_PinMap.h"

#define CALIBRATION_FLASH_FLAGS (FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR)

''')
flash = replace(flash, 'static FlashRecord ReadRecord(uint32_t address)\n',
    '''/* 记录位于映射到内存的 Flash 页，SPL 的 Flash 接口只负责擦写和状态。 */
static uint16_t ReadFlashHalfword(uint32_t address)
{
    return *(const volatile uint16_t *)(uintptr_t)address;
}

static uint32_t ReadFlashWord(uint32_t address)
{
    return *(const volatile uint32_t *)(uintptr_t)address;
}

static FlashRecord ReadRecord(uint32_t address)
''')
flash = replace_region(flash, 'static bool WaitFlash', 'static bool ProgramWord',
    '''static bool ErasePage(uint32_t address)
{
    FLASH_ClearFlag(CALIBRATION_FLASH_FLAGS);
    return FLASH_ErasePage(address) == FLASH_COMPLETE;
}

static bool ProgramHalfword(uint32_t address, uint16_t value)
{
    if (value == UINT16_MAX)
    {
        return ReadFlashHalfword(address) == UINT16_MAX;
    }
    FLASH_ClearFlag(CALIBRATION_FLASH_FLAGS);
    return FLASH_ProgramHalfWord(address, value) == FLASH_COMPLETE &&
           ReadFlashHalfword(address) == value;
}

''')
flash = flash.replace('STM32_REG32(', 'ReadFlashWord(').replace('STM32_REG16(', 'ReadFlashHalfword(')
flash = replace(flash, '(FLASH_SR & FLASH_BUSY) != 0u', 'FLASH_GetFlagStatus(FLASH_FLAG_BSY) == SET')
flash = replace(flash, '''    if ((FLASH_CR & FLASH_LOCK) != 0u)
    {
        FLASH_KEYR = FLASH_KEY_1;
        FLASH_KEYR = FLASH_KEY_2;
    }
    if ((FLASH_CR & FLASH_LOCK) != 0u)''',
    '''    /* SPL 解锁接口不返回结果，也没有查询 LOCK 的接口，直接读取该状态位。 */
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u)
    {
        FLASH_Unlock();
    }
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u)''')
flash = replace(flash, '    FLASH_CR = FLASH_LOCK;', '    FLASH_Lock();')
write(path, flash)
