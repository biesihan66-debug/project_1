/*
 * 开短路校准的 Flash 日志。H743 Bank 2 的扇区 4、5各存一份，轮流更新。
 * 每份的第一个 32 B Flash 字为提交头，包含魔数、序号、数据字节数与 CRC。
 * V1.9 记录 DDS 时钟频率，旧激励配置的开短校准须重新采集。
 * data[i] 从偏移 32 起，写完数据后提交头。旧扇区保留到下一次保存。
 * readback[] 在 DMA 可访问的 D2 SRAM。保存成功后由 App 层更新 OLED 第 4 行。
 */
#include "Flash_Calibration.h"
#include "BSP_Board.h"
#include "BSP_PinMap.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_dma.h"

#include <string.h>

#define CALIBRATION_SECTOR_A 0x08180000u
#define CALIBRATION_SECTOR_B 0x081A0000u
#define CALIBRATION_SECTOR_BYTES 0x20000u
#define CALIBRATION_MAX_BYTES 1024u
#define RECORD_MAGIC 0x39434C52u
#define FLASH_WORD_BYTES 32u
#define FLASH_WORD_UINTS (FLASH_WORD_BYTES / sizeof(uint32_t))
#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu
#define FLASH_WRITE_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | \
                            FLASH_SR_INCERR | FLASH_SR_OPERR)
#define FLASH_READ_ERRORS (FLASH_SR_RDPERR | FLASH_SR_RDSERR | FLASH_SR_DBECCERR)
#define FLASH_BUSY_FLAGS (FLASH_SR_QW | FLASH_SR_BSY | FLASH_SR_WBNE)
#define CAL_DMA_STREAM LL_DMA_STREAM_0

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t size;
    uint32_t crc;
    uint32_t dds_clock_hz;
    uint32_t reserved[3];
} RecordHeader;

typedef struct
{
    uint32_t address;
    RecordHeader header;
    bool valid;
    bool read_failed;
} FlashRecord;

/* 此缓冲区仅由主循环使用，D-Cache 在本工程关闭。 */
static uint8_t readback[CALIBRATION_MAX_BYTES]
    __attribute__((section(".bss.cal_copy"), aligned(32), zero_init));

static uint32_t UpdateCRC(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t bit = 0u; bit < 8u; ++bit)
    {
        const uint32_t polynomial = (crc & 1u) != 0u ? 0xEDB88320u : 0u;
        crc = (crc >> 1) ^ polynomial;
    }
    return crc;
}

static uint32_t RecordCRC(const uint8_t *bytes, uint32_t size, uint32_t sequence)
{
    uint32_t crc = UINT32_MAX;
    for (uint32_t shift = 0u; shift < 32u; shift += 8u)
    {
        crc = UpdateCRC(crc, (uint8_t)(size >> shift));
        crc = UpdateCRC(crc, (uint8_t)(sequence >> shift));
    }
    for (uint32_t i = 0u; i < size; ++i)
    {
        crc = UpdateCRC(crc, bytes[i]);
    }
    return ~crc;
}

/* STM32H743VI 有 2 MiB Flash。校准区位于映射到 0x08100000 的物理 Bank 2。 */
static bool CanAccessCalibration(void)
{
    const uint16_t capacity_kib = *(const volatile uint16_t *)(uintptr_t)FLASHSIZE_BASE;
    return capacity_kib == 2048u && (FLASH->OPTSR_CUR & FLASH_OPTSR_SWAP_BANK_OPT) == 0u &&
           (FLASH->SR2 & FLASH_BUSY_FLAGS) == 0u;
}

static bool StopReadDMA(void)
{
    LL_DMA_DisableStream(DMA2, CAL_DMA_STREAM);
    const uint32_t deadline_ms = BSP_GetMillis() + 3u;
    while (LL_DMA_IsEnabledStream(DMA2, CAL_DMA_STREAM) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

/* 用 DMA 读校准区。掉电留下的双位 ECC 错误由 DMA 报告，CPU 可以继续读另一份。 */
static bool ReadFlash(uint32_t address, uint32_t size, bool *read_failed)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
    if (!StopReadDMA())
    {
        *read_failed = true;
        return false;
    }
    FLASH->CCR2 = FLASH_READ_ERRORS | FLASH_SR_SNECCERR;
    LL_DMA_ClearFlag_TC0(DMA2);
    LL_DMA_ClearFlag_TE0(DMA2);
    LL_DMA_ClearFlag_FE0(DMA2);
    LL_DMA_ClearFlag_HT0(DMA2);
    LL_DMA_ConfigTransfer(DMA2, CAL_DMA_STREAM,
                         LL_DMA_DIRECTION_MEMORY_TO_MEMORY | LL_DMA_MODE_NORMAL |
                         LL_DMA_PERIPH_INCREMENT | LL_DMA_MEMORY_INCREMENT |
                         LL_DMA_PDATAALIGN_WORD | LL_DMA_MDATAALIGN_WORD);
    LL_DMA_EnableFifoMode(DMA2, CAL_DMA_STREAM);
    LL_DMA_SetFIFOThreshold(DMA2, CAL_DMA_STREAM, LL_DMA_FIFOTHRESHOLD_1_4);
    LL_DMA_SetPeriphAddress(DMA2, CAL_DMA_STREAM, address);
    LL_DMA_SetMemoryAddress(DMA2, CAL_DMA_STREAM, (uint32_t)(uintptr_t)readback);
    LL_DMA_SetDataLength(DMA2, CAL_DMA_STREAM, (size + 3u) / 4u);
    LL_DMA_EnableStream(DMA2, CAL_DMA_STREAM);

    const uint32_t deadline_ms = BSP_GetMillis() + 5u;
    while (LL_DMA_IsActiveFlag_TC0(DMA2) == 0u)
    {
        if (LL_DMA_IsActiveFlag_TE0(DMA2) != 0u || LL_DMA_IsActiveFlag_FE0(DMA2) != 0u ||
            !BSP_BeforeDeadline(deadline_ms))
        {
            break;
        }
    }
    const uint32_t flash_errors = FLASH->SR2 & FLASH_READ_ERRORS;
    const bool complete = LL_DMA_IsActiveFlag_TC0(DMA2) != 0u &&
                          LL_DMA_IsActiveFlag_TE0(DMA2) == 0u &&
                          LL_DMA_IsActiveFlag_FE0(DMA2) == 0u &&
                          flash_errors == 0u;
    const bool stopped = StopReadDMA();
    FLASH->CCR2 = FLASH_READ_ERRORS | FLASH_SR_SNECCERR;
    __DMB();
    /* 只有已确认的双位 ECC 错误可当成损坏记录。通信故障时保留两个扇区。 */
    *read_failed = !stopped || (!complete && flash_errors != FLASH_SR_DBECCERR);
    return complete && stopped;
}

static FlashRecord ReadRecord(uint32_t address)
{
    FlashRecord record = {0};
    record.address = address;
    if (!ReadFlash(address, sizeof record.header, &record.read_failed))
    {
        return record;
    }
    memcpy(&record.header, readback, sizeof record.header);
    const uint32_t size = record.header.size;
    record.valid = record.header.magic == RECORD_MAGIC && size != 0u &&
                   size <= CALIBRATION_MAX_BYTES &&
                   ReadFlash(address + FLASH_WORD_BYTES, size, &record.read_failed) &&
                   record.header.crc == RecordCRC(readback, size, record.header.sequence);
    return record;
}

static FlashRecord LatestRecord(bool *read_failed)
{
    const FlashRecord a = ReadRecord(CALIBRATION_SECTOR_A);
    const FlashRecord b = ReadRecord(CALIBRATION_SECTOR_B);
    *read_failed = a.read_failed || b.read_failed;
    const uint32_t difference = b.header.sequence - a.header.sequence;
    if (b.valid && (!a.valid || (difference != 0u && difference < 0x80000000u)))
    {
        return b;
    }
    return a;
}

bool Flash_LoadCalibration(void *data, size_t size)
{
    if (data == NULL || size == 0u || size > CALIBRATION_MAX_BYTES || !CanAccessCalibration())
    {
        return false;
    }
    bool read_failed;
    const FlashRecord latest = LatestRecord(&read_failed);
    if (!latest.valid || size != latest.header.size ||
        latest.header.dds_clock_hz != RLC_AD9833_MCLK_HZ || !ReadRecord(latest.address).valid)
    {
        return false;
    }
    memcpy(data, readback, size);
    return true;
}

/* 通过 CMSIS 按 RM0433 的 Bank 2 擦除和编程顺序操作 Flash。 */
static bool WaitForFlash(uint32_t timeout_ms)
{
    const uint32_t deadline_ms = BSP_GetMillis() + timeout_ms;
    while ((FLASH->SR2 & FLASH_BUSY_FLAGS) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return (FLASH->SR2 & FLASH_WRITE_ERRORS) == 0u;
}

static bool EraseSector(uint32_t address)
{
    const uint32_t sector = (address - FLASH_BANK2_BASE) / CALIBRATION_SECTOR_BYTES;
    FLASH->CCR2 = FLASH_SR_EOP | FLASH_WRITE_ERRORS;
    MODIFY_REG(FLASH->CR2, FLASH_CR_PG | FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_PSIZE,
               FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos) | FLASH_CR_PSIZE_1);
    SET_BIT(FLASH->CR2, FLASH_CR_START);
    const bool erased = WaitForFlash(5000u);
    CLEAR_BIT(FLASH->CR2, FLASH_CR_SER | FLASH_CR_SNB);
    return erased;
}

static bool ProgramFlashWord(uint32_t address, const uint32_t words[FLASH_WORD_UINTS])
{
    volatile uint32_t *const destination = (volatile uint32_t *)(uintptr_t)address;
    FLASH->CCR2 = FLASH_SR_EOP | FLASH_WRITE_ERRORS;
    SET_BIT(FLASH->CR2, FLASH_CR_PG);
    __DSB();
    __ISB();
    for (uint32_t i = 0u; i < FLASH_WORD_UINTS; ++i)
    {
        destination[i] = words[i];
    }
    __DSB();
    __ISB();
    const bool written = WaitForFlash(100u);
    CLEAR_BIT(FLASH->CR2, FLASH_CR_PG);
    return written;
}

static bool WriteRecord(uint32_t address, uint32_t sequence, const uint8_t *bytes, uint32_t size)
{
    uint32_t words[FLASH_WORD_UINTS];
    if (!EraseSector(address))
    {
        return false;
    }
    for (uint32_t offset = 0u; offset < size; offset += FLASH_WORD_BYTES)
    {
        const uint32_t remaining = size - offset;
        const uint32_t count = remaining < FLASH_WORD_BYTES ? remaining : FLASH_WORD_BYTES;
        memset(words, 0xFF, sizeof words);
        memcpy(words, bytes + offset, count);
        if (!ProgramFlashWord(address + FLASH_WORD_BYTES + offset, words))
        {
            return false;
        }
    }
    const RecordHeader header = {
        .magic = RECORD_MAGIC,
        .sequence = sequence,
        .size = size,
        .crc = RecordCRC(bytes, size, sequence),
        .dds_clock_hz = RLC_AD9833_MCLK_HZ,
    };
    memcpy(words, &header, sizeof words);
    return ProgramFlashWord(address, words) && ReadRecord(address).valid;
}

bool Flash_SaveCalibration(const void *data, size_t size)
{
    if (data == NULL || size == 0u || size > CALIBRATION_MAX_BYTES || !CanAccessCalibration())
    {
        return false;
    }
    bool read_failed;
    const FlashRecord previous = LatestRecord(&read_failed);
    if (read_failed || !CanAccessCalibration())
    {
        return false;
    }
    const uint32_t destination = previous.valid && previous.address == CALIBRATION_SECTOR_A
                                     ? CALIBRATION_SECTOR_B
                                     : CALIBRATION_SECTOR_A;
    const uint32_t sequence = previous.valid ? previous.header.sequence + 1u : 1u;
    if ((FLASH->CR2 & FLASH_CR_LOCK) != 0u)
    {
        FLASH->KEYR2 = FLASH_KEY1;
        FLASH->KEYR2 = FLASH_KEY2;
    }
    if ((FLASH->CR2 & FLASH_CR_LOCK) != 0u)
    {
        return false;
    }
    const bool saved = WriteRecord(destination, sequence, data, (uint32_t)size);
    SET_BIT(FLASH->CR2, FLASH_CR_LOCK);
    return saved;
}
