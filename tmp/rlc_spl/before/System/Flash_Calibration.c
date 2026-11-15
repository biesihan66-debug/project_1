/*
 * 开短校准记录的 Flash 日志。BSP_PinMap.h 保留末尾两页。
 * 每页偏移 0/4/8/12/16 分别保存魔数、序号、字节数、CRC 和提交字。
 * 偏移 20 开始保存调用方提供的 data[i]。写入时最后提交，掉电时保留旧页。
 * 记录数据由校准模块读取，校准状态在 OLED 第 1/4 行，页地址不显示。
 */
#include "BSP_PinMap.h"
#include "Flash_Calibration.h"
#include "BSP_Board.h"

#define FLASH_KEYR STM32_REG32(STM32_FLASH + 0x04u)
#define FLASH_SR   STM32_REG32(STM32_FLASH + 0x0Cu)
#define FLASH_CR   STM32_REG32(STM32_FLASH + 0x10u)
#define FLASH_AR   STM32_REG32(STM32_FLASH + 0x14u)

#define FLASH_BUSY       1u
#define FLASH_ERRORS     ((1u << 2) | (1u << 4))
#define FLASH_EOP        (1u << 5)
#define FLASH_PROGRAM    1u
#define FLASH_PAGE_ERASE (1u << 1)
#define FLASH_START      (1u << 6)
#define FLASH_LOCK       (1u << 7)
#define FLASH_KEY_1      0x45670123u
#define FLASH_KEY_2      0xCDEF89ABu

#define RECORD_MAGIC       0x31434C52u
#define RECORD_COMMITTED   0xA56Cu
#define RECORD_SEQUENCE_AT 4u
#define RECORD_SIZE_AT     8u
#define RECORD_CRC_AT      12u
#define RECORD_COMMIT_AT   16u
#define RECORD_DATA_AT     20u

typedef struct
{
    uint32_t address;
    uint32_t sequence;
    uint32_t size;
    bool valid;
} FlashRecord;

static uint32_t UpdateCRC(uint32_t crc, uint8_t byte)
{
    uint8_t bit;

    crc ^= byte;
    for (bit = 0u; bit < 8u; ++bit)
    {
        uint32_t polynomial = (crc & 1u) != 0u ? 0xEDB88320u : 0u;
        crc = (crc >> 1) ^ polynomial;
    }
    return crc;
}

static uint32_t RecordCRC(const volatile uint8_t *bytes, uint32_t size, uint32_t sequence)
{
    uint32_t crc = UINT32_MAX;
    uint32_t i;

    for (i = 0u; i < 32u; i += 8u)
    {
        crc = UpdateCRC(crc, (uint8_t)(size >> i));
        crc = UpdateCRC(crc, (uint8_t)(sequence >> i));
    }
    for (i = 0u; i < size; ++i)
    {
        crc = UpdateCRC(crc, bytes[i]);
    }
    return ~crc;
}

static FlashRecord ReadRecord(uint32_t address)
{
    FlashRecord record;

    record.address = address;
    record.sequence = STM32_REG32(address + RECORD_SEQUENCE_AT);
    record.size = STM32_REG32(address + RECORD_SIZE_AT);
    record.valid = STM32_REG32(address) == RECORD_MAGIC &&
                   STM32_REG16(address + RECORD_COMMIT_AT) == RECORD_COMMITTED &&
                   record.size != 0u && record.size <= RLC_FLASH_PAGE_BYTES - RECORD_DATA_AT &&
                   RecordCRC((const volatile uint8_t *)(uintptr_t)(address + RECORD_DATA_AT),
                             record.size, record.sequence) == STM32_REG32(address + RECORD_CRC_AT);
    return record;
}

static FlashRecord LatestRecord(void)
{
    FlashRecord a = ReadRecord(RLC_CALIBRATION_PAGE_A);
    FlashRecord b = ReadRecord(RLC_CALIBRATION_PAGE_B);
    uint32_t difference = b.sequence - a.sequence;

    if (b.valid && (!a.valid || (difference != 0u && difference < 0x80000000u)))
    {
        return b;
    }
    return a;
}

bool Flash_LoadCalibration(void *data, size_t size)
{
    FlashRecord record = LatestRecord();
    uint8_t *bytes = data;
    size_t i;

    if (!record.valid || data == NULL || size != record.size)
    {
        return false;
    }
    for (i = 0u; i < size; ++i)
    {
        bytes[i] = *(const volatile uint8_t *)(uintptr_t)(record.address + RECORD_DATA_AT + i);
    }
    return true;
}

static bool WaitFlash(void)
{
    uint32_t attempts = 1000000u;

    while ((FLASH_SR & FLASH_BUSY) != 0u && attempts != 0u)
    {
        --attempts;
    }
    return (FLASH_SR & (FLASH_BUSY | FLASH_ERRORS)) == 0u;
}

static bool ErasePage(uint32_t address)
{
    bool erased;

    FLASH_SR = FLASH_ERRORS | FLASH_EOP;
    FLASH_CR = FLASH_PAGE_ERASE;
    FLASH_AR = address;
    FLASH_CR |= FLASH_START;
    erased = WaitFlash();
    FLASH_CR = 0u;
    return erased;
}

static bool ProgramHalfword(uint32_t address, uint16_t value)
{
    bool programmed;

    if (value == UINT16_MAX)
    {
        return STM32_REG16(address) == UINT16_MAX;
    }
    FLASH_SR = FLASH_ERRORS | FLASH_EOP;
    FLASH_CR = FLASH_PROGRAM;
    STM32_REG16(address) = value;
    programmed = WaitFlash();
    FLASH_CR = 0u;
    return programmed && STM32_REG16(address) == value;
}

static bool ProgramWord(uint32_t address, uint32_t value)
{
    return ProgramHalfword(address, (uint16_t)value) &&
           ProgramHalfword(address + 2u, (uint16_t)(value >> 16));
}

static bool WriteRecord(uint32_t address, uint32_t sequence, const uint8_t *bytes, uint32_t size)
{
    uint32_t crc = RecordCRC(bytes, size, sequence);
    uint32_t i;

    if (!ErasePage(address) || !ProgramWord(address, RECORD_MAGIC) ||
        !ProgramWord(address + RECORD_SEQUENCE_AT, sequence) ||
        !ProgramWord(address + RECORD_SIZE_AT, size) || !ProgramWord(address + RECORD_CRC_AT, crc))
    {
        return false;
    }
    for (i = 0u; i < size; i += 2u)
    {
        uint16_t high = i + 1u < size ? bytes[i + 1u] : UINT8_MAX;
        uint16_t value = (uint16_t)((high << 8) | bytes[i]);

        if (!ProgramHalfword(address + RECORD_DATA_AT + i, value))
        {
            return false;
        }
    }
    return ProgramHalfword(address + RECORD_COMMIT_AT, RECORD_COMMITTED) &&
           ReadRecord(address).valid;
}

bool Flash_SaveCalibration(const void *data, size_t size)
{
    FlashRecord previous;
    uint32_t destination;
    uint32_t sequence;
    bool saved;

    if (data == NULL || size == 0u || size > RLC_FLASH_PAGE_BYTES - RECORD_DATA_AT ||
        (FLASH_SR & FLASH_BUSY) != 0u)
    {
        return false;
    }
    previous = LatestRecord();
    destination = previous.valid && previous.address == RLC_CALIBRATION_PAGE_A
                      ? RLC_CALIBRATION_PAGE_B
                      : RLC_CALIBRATION_PAGE_A;
    sequence = previous.valid ? previous.sequence + 1u : 1u;

    if ((FLASH_CR & FLASH_LOCK) != 0u)
    {
        FLASH_KEYR = FLASH_KEY_1;
        FLASH_KEYR = FLASH_KEY_2;
    }
    if ((FLASH_CR & FLASH_LOCK) != 0u)
    {
        return false;
    }
    saved = WriteRecord(destination, sequence, data, (uint32_t)size);
    FLASH_CR = FLASH_LOCK;
    return saved;
}
