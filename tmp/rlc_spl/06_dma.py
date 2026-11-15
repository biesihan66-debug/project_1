from support import add_spl, read, replace, replace_region, write


add_spl('dma')
path = 'System/ADC_Sample.c'
adc = read(path)
adc = replace_region(adc, '#define DMA_ISR', '#define ADC_MAX_SAMPLE_PAIRS', '')
adc = replace(adc, '    STM32_RCC_AHBENR |= 1u;',
              '    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);\n'
              '    DMA_DeInit(DMA1_Channel1);')
adc = replace(adc, '    DMA_CH1_CCR = 0u;\n    DMA_IFCR = 0xFu;',
              '    DMA_Cmd(DMA1_Channel1, DISABLE);\n    DMA_ClearFlag(DMA1_FLAG_GL1);')
adc = replace(adc, '''    DMA_CH1_CPAR = (uint32_t)(uintptr_t)&ADC_DR(STM32_ADC1);
    DMA_CH1_CMAR = (uint32_t)(uintptr_t)packed_adc_samples;
    DMA_CH1_CNDTR = pairs;
    DMA_CH1_CCR = DMA_CH1_CONFIG | 1u;''', '''    /* ADC1->DR 是两路结果的 DMA 源地址，32 位读取才能保留 ADC2 的高半字。 */
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)&ADC1->DR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)(uintptr_t)packed_adc_samples;
    dma_init.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma_init.DMA_BufferSize = pairs;
    dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    dma_init.DMA_Mode = DMA_Mode_Normal;
    dma_init.DMA_Priority = DMA_Priority_High;
    dma_init.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma_init);
    DMA_Cmd(DMA1_Channel1, ENABLE);''')
adc = replace(adc, 'static void StartAcquisition(uint32_t timer_ticks, uint16_t pairs, uint8_t sample_time)\n{\n',
              'static void StartAcquisition(uint32_t timer_ticks, uint16_t pairs, uint8_t sample_time)\n{\n'
              '    DMA_InitTypeDef dma_init;\n\n')
adc = replace(adc, '#define ADC_DR(base)    STM32_REG32((base) + 0x4Cu)\n', '')
adc = replace(adc, '    uint32_t dma_state;', '    bool dma_error;\n    bool dma_complete;')
adc = replace(adc, '''        dma_state = DMA_ISR;
        if (!BSP_BeforeDeadline(deadline_ms))''', '''        dma_complete = DMA_GetFlagStatus(DMA1_FLAG_TC1) == SET;
        dma_error = DMA_GetFlagStatus(DMA1_FLAG_TE1) == SET;
        if (!BSP_BeforeDeadline(deadline_ms))''')
adc = replace(adc, '} while ((dma_state & (DMA_CH1_COMPLETE | DMA_CH1_ERROR)) == 0u);',
              '} while (!dma_complete && !dma_error);')
adc = replace(adc, 'if ((dma_state & DMA_CH1_ERROR) != 0u)', 'if (dma_error)')
write(path, adc)
