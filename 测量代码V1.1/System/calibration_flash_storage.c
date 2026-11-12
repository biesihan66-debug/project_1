/*
 * 开短校准记录的 Flash 日志。board_pin_map.h 保留末尾两页。
 * 每页偏移 0/4/8/12/16 分别保存魔数、序号、字节数、CRC 和提交字。
 * 偏移 20 开始保存调用方提供的 data[i]。写入时最后提交，掉电时保留旧页。
 * 记录数据由校准模块读取，校准状态在 OLED 第 1/4 行，页地址不显示。
 */
#include "board_pin_map.h"
#include "rlc_board_internal.h"

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

typedef struct {
    uint32_t address;
    uint32_t sequence;
    uint32_t size;
    bool valid;
} FlashRecord;

static uint32_t update_crc(uint32_t crc, uint8_t byte)
{
    uint8_t bit;

    crc ^= byte;
    for (bit = 0u; bit < 8u; ++bit) {
        uint32_t polynomial = (crc & 1u) != 0u ? 0xEDB88320u : 0u;
        crc = (crc >> 1) ^ polynomial;
    }
    return crc;
}

static uint32_t record_crc(const volatile uint8_t *bytes, uint32_t size, uint32_t sequence)
{
    uint32_t crc = UINT32_MAX;
    uint32_t i;

    for (i = 0u; i < 32u; i += 8u) {
        crc = update_crc(crc, (uint8_t)(size >> i));
        crc = update_crc(crc, (uint8_t)(sequence >> i));
    }
    for (i = 0u; i < size; ++i) {
        crc = update_crc(crc, bytes[i]);
    }
    return ~crc;
}

static FlashRecord read_record(uint32_t address)
{
    FlashRecord record;

    record.address = address;
    record.sequence = STM32_REG32(address + RECORD_SEQUENCE_AT);
    record.size = STM32_REG32(address + RECORD_SIZE_AT);
    record.valid = STM32_REG32(address) == RECORD_MAGIC
                && STM32_REG16(address + RECORD_COMMIT_AT) == RECORD_COMMITTED
                && record.size != 0u && record.size <= RLC_FLASH_PAGE_BYTES - RECORD_DATA_AT
                && record_crc((const volatile uint8_t *)(uintptr_t)(address + RECORD_DATA_AT),
                              record.size, record.sequence) == STM32_REG32(address + RECORD_CRC_AT);
    return record;
}

static FlashRecord latest_record(void)
{
    FlashRecord a = read_record(RLC_CALIBRATION_PAGE_A);
    FlashRecord b = read_record(RLC_CALIBRATION_PAGE_B);
    uint32_t difference = b.sequence - a.sequence;

    if (b.valid && (!a.valid || (difference != 0u && difference < 0x80000000u))) {
        return b;
    }
    return a;
}

bool rlc_board_load_calibration(void *data, size_t size)
{
    FlashRecord record = latest_record();
    uint8_t *bytes = data;
    size_t i;

    if (!record.valid || data == NULL || size != record.size) {
        return false;
    }
    for (i = 0u; i < size; ++i) {
        bytes[i] = *(const volatile uint8_t *)(uintptr_t)(record.address + RECORD_DATA_AT + i);
    }
    return true;
}

static bool wait_flash(void)
{
    uint32_t attempts = 1000000u;

    while ((FLASH_SR & FLASH_BUSY) != 0u && attempts != 0u) {
        --attempts;
    }
    return (FLASH_SR & (FLASH_BUSY | FLASH_ERRORS)) == 0u;
}

static bool erase_page(uint32_t address)
{
    bool erased;

    FLASH_SR = FLASH_ERRORS | FLASH_EOP;
    FLASH_CR = FLASH_PAGE_ERASE;
    FLASH_AR = address;
    FLASH_CR |= FLASH_START;
    erased = wait_flash();
    FLASH_CR = 0u;
    return erased;
}

static bool program_halfword(uint32_t address, uint16_t value)
{
    bool programmed;

    if (value == UINT16_MAX) {
        return STM32_REG16(address) == UINT16_MAX;
    }
    FLASH_SR = FLASH_ERRORS | FLASH_EOP;
    FLASH_CR = FLASH_PROGRAM;
    STM32_REG16(address) = value;
    programmed = wait_flash();
    FLASH_CR = 0u;
    return programmed && STM32_REG16(address) == value;
}

static bool program_word(uint32_t address, uint32_t value)
{
    return program_halfword(address, (uint16_t)value)
        && program_halfword(address + 2u, (uint16_t)(value >> 16));
}

static bool write_record(uint32_t address, uint32_t sequence, const uint8_t *bytes, uint32_t size)
{
    uint32_t crc = record_crc(bytes, size, sequence);
    uint32_t i;

    if (!erase_page(address)
        || !program_word(address, RECORD_MAGIC)
        || !program_word(address + RECORD_SEQUENCE_AT, sequence)
        || !program_word(address + RECORD_SIZE_AT, size)
        || !program_word(address + RECORD_CRC_AT, crc)) {
        return false;
    }
    for (i = 0u; i < size; i += 2u) {
        uint16_t high = i + 1u < size ? bytes[i + 1u] : UINT8_MAX;
        uint16_t value = (uint16_t)((high << 8) | bytes[i]);

        if (!program_halfword(address + RECORD_DATA_AT + i, value)) {
            return false;
        }
    }
    return program_halfword(address + RECORD_COMMIT_AT, RECORD_COMMITTED)
        && read_record(address).valid;
}

bool rlc_board_save_calibration(const void *data, size_t size)
{
    FlashRecord previous;
    uint32_t destination;
    uint32_t sequence;
    bool saved;

    if (data == NULL || size == 0u || size > RLC_FLASH_PAGE_BYTES - RECORD_DATA_AT
        || (FLASH_SR & FLASH_BUSY) != 0u) {
        return false;
    }
    previous = latest_record();
    destination = previous.valid && previous.address == RLC_CALIBRATION_PAGE_A
                ? RLC_CALIBRATION_PAGE_B : RLC_CALIBRATION_PAGE_A;
    sequence = previous.valid ? previous.sequence + 1u : 1u;

    if ((FLASH_CR & FLASH_LOCK) != 0u) {
        FLASH_KEYR = FLASH_KEY_1;
        FLASH_KEYR = FLASH_KEY_2;
    }
    if ((FLASH_CR & FLASH_LOCK) != 0u) {
        return false;
    }
    saved = write_record(destination, sequence, data, (uint32_t)size);
    FLASH_CR = FLASH_LOCK;
    return saved;
}
