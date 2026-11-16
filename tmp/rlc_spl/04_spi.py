from support import add_spl, read, replace, replace_region, write


add_spl('spi')
path = 'System/DDS_Signal.c'
dds = read(path)
dds = replace(dds, '#include "BSP_Registers.h"\n', '')
dds = replace_region(dds, '#define SPI_CR1', '#define AD9833_B28', '')
dds = replace(dds, 'static bool SpiWait(uint32_t mask, bool set, uint32_t deadline_ms)',
              'static bool SpiWait(uint16_t flag, FlagStatus expected, uint32_t deadline_ms)')
dds = replace(dds, 'while (((SPI_SR & mask) != 0u) != set)',
              'while (SPI_I2S_GetFlagStatus(SPI1, flag) != expected)')
dds = replace(dds, 'SpiWait(SPI_TXE, true, deadline_ms)',
              'SpiWait(SPI_I2S_FLAG_TXE, SET, deadline_ms)', 2)
dds = replace(dds, 'SpiWait(SPI_BSY, false, deadline_ms)',
              'SpiWait(SPI_I2S_FLAG_BSY, RESET, deadline_ms)')
dds = replace(dds, '    SPI_DR = word;', '    SPI_I2S_SendData(SPI1, word);')
dds = replace(dds, '    complete = SpiWait(SPI_I2S_FLAG_TXE, SET, deadline_ms) && SpiWait(SPI_I2S_FLAG_BSY, RESET, deadline_ms);',
              '    complete = SpiWait(SPI_I2S_FLAG_TXE, SET, deadline_ms) &&\n'
              '               SpiWait(SPI_I2S_FLAG_BSY, RESET, deadline_ms);')
dds = replace(dds, '    (void)SPI_DR; /* 全双工 SPI 的接收内容未连接，每帧读出清 RXNE。 */\n'
                   '    (void)SPI_SR; /* 清除可能残留的 OVR。 */',
              '    /* 全双工接收端未接线，先读 DR，再读 SR，清除 RXNE 和可能残留的 OVR。 */\n'
              '    (void)SPI_I2S_ReceiveData(SPI1);\n'
              '    (void)SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_OVR);')
dds = replace(dds, '    GPIO_InitTypeDef gpio_init;\n\n    STM32_RCC_APB2ENR |= 1u << 12;',
              '    GPIO_InitTypeDef gpio_init;\n    SPI_InitTypeDef spi_init;\n\n'
              '    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);\n'
              '    SPI_I2S_DeInit(SPI1);')
dds = replace(dds, '    STM32_RCC_CFGR = (STM32_RCC_CFGR & ~(7u << 24)) | (6u << 24);',
              '    RCC_MCOConfig(RCC_MCO_HSE);')
dds = replace(dds, '    SPI_CR1 = (1u << 11) | (1u << 9) | (1u << 8) | (4u << 3) | (1u << 2) | (1u << 1);\n'
                   '    SPI_CR1 |= 1u << 6;',
              '''    spi_init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi_init.SPI_Mode = SPI_Mode_Master;
    spi_init.SPI_DataSize = SPI_DataSize_16b;
    spi_init.SPI_CPOL = SPI_CPOL_High;
    spi_init.SPI_CPHA = SPI_CPHA_1Edge;
    spi_init.SPI_NSS = SPI_NSS_Soft;
    spi_init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
    spi_init.SPI_FirstBit = SPI_FirstBit_MSB;
    spi_init.SPI_CRCPolynomial = 7u;
    SPI_Init(SPI1, &spi_init);
    SPI_Cmd(SPI1, ENABLE);''')
write(path, dds)
