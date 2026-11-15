/*
 * 开短校准记录的 Flash 日志。BSP_PinMap.h 保留末尾两页。
 * 每页偏移 0/4/8/12/16 分别保存魔数、序号、字节数、CRC 和提交字。
 * 偏移 20 开始保存调用方提供的 data[i]。写入时最后提交，掉电时保留旧页。
 * 记录数据由校准模块读取，校准状态在 OLED 第 1/4 行，页地址不显示。
 */
#include "Flash_Calibration.h"
#include "stm32f10x.h"
#include "BSP_PinMap.h"

#define CALIBRATION_FLASH_FLAGS (FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR)

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

/* 记录位于映射到内存的 Flash 页，SPL 的 Flash 接口只负责擦写和状态。 */
static uint16_t ReadFlashHalfword(uint32_t address)
{
    return *(const volatile uint16_t *)(uintptr_t)address;
}

static uint32_t ReadFlashWord(uint32_t address)
{
    return *(const volatile uint32_t *)(uintptr_t)address;
}

static FlashRecord ReadRecord(uint32_t address)
{
    FlashRecord record;

    record.address = address;
    record.sequence = ReadFlashWord(address + RECORD_SEQUENCE_AT);
    record.size = ReadFlashWord(address + RECORD_SIZE_AT);
    record.valid = ReadFlashWord(address) == RECORD_MAGIC &&
                   ReadFlashHalfword(address + RECORD_COMMIT_AT) == RECORD_COMMITTED &&
                   record.size != 0u && record.size <= RLC_FLASH_PAGE_BYTES - RECORD_DATA_AT &&
                   RecordCRC((const volatile uint8_t *)(uintptr_t)(address + RECORD_DATA_AT),
                             record.size, record.sequence) == ReadFlashWord(address + RECORD_CRC_AT);
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

static bool ErasePage(uint32_t address)
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
        FLASH_GetFlagStatus(FLASH_FLAG_BSY) == SET)
    {
        return false;
    }
    previous = LatestRecord();
    destination = previous.valid && previous.address == RLC_CALIBRATION_PAGE_A
                      ? RLC_CALIBRATION_PAGE_B
                      : RLC_CALIBRATION_PAGE_A;
    sequence = previous.valid ? previous.sequence + 1u : 1u;

    /* SPL 解锁接口不返回结果，也没有查询 LOCK 的接口，直接读取该状态位。 */
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u)
    {
        FLASH_Unlock();
    }
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u)
    {
        return false;
    }
    saved = WriteRecord(destination, sequence, data, (uint32_t)size);
    FLASH_Lock();
    return saved;
}
