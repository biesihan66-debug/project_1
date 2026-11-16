from support import add_spl, edit, read, replace, replace_region, write


for path in ('System/ADC_Sample.h', 'System/ADC_Sample.c', 'System/BSP_Board.c',
             'Tests/test_board_deadline.c'):
    text = read(path)
    write(path, replace(text, 'ADC_Init(', 'BSP_ADC_Init('))

add_spl('adc')
path = 'System/ADC_Sample.c'
adc = read(path)
adc = replace_region(adc, '#include "stm32f10x.h"', '#define ADC_MAX_SAMPLE_PAIRS',
    '#include "ADC_Sample.h"\n#include "stm32f10x.h"\n#include "BSP_PinMap.h"\n'
    '#include "BSP_Board.h"\n\n')
adc = replace(adc, '#define ADC_MAX_SAMPLE_PAIRS 2048u\n',
              '#define ADC_MAX_SAMPLE_PAIRS 2048u\n'
              '#define ADC_FRAME_FLAGS (ADC_FLAG_AWD | ADC_FLAG_EOC | ADC_FLAG_JEOC | \\\n'
              '                         ADC_FLAG_JSTRT | ADC_FLAG_STRT)\n')
adc = replace(adc, 'CalibrateADC(uint32_t base)', 'CalibrateADC(ADC_TypeDef *adc)')
adc = replace(adc, '    ADC_CR2(base) |= ADC_RSTCAL;', '    ADC_ResetCalibration(adc);')
adc = replace(adc, 'while ((ADC_CR2(base) & ADC_RSTCAL) != 0u)',
              'while (ADC_GetResetCalibrationStatus(adc) == SET)')
adc = replace(adc, '    ADC_CR2(base) |= ADC_CAL;', '    ADC_StartCalibration(adc);')
adc = replace(adc, 'while ((ADC_CR2(base) & ADC_CAL) != 0u)',
              'while (ADC_GetCalibrationStatus(adc) == SET)')
adc = replace_region(adc, 'static void SetADCSampleTime', 'bool BSP_ADC_Init', '')
adc = replace(adc, '    GPIO_InitTypeDef gpio_init;\n',
              '    GPIO_InitTypeDef gpio_init;\n    ADC_InitTypeDef adc_init;\n')
adc = replace(adc, '    STM32_RCC_APB2ENR |= (1u << 9) | (1u << 10);',
              '    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_ADC2, ENABLE);\n'
              '    ADC_DeInit(ADC1);\n    ADC_DeInit(ADC2);')
adc = replace(adc, '    ADC_CR2(STM32_ADC1) = ADC_ADON;\n    ADC_CR2(STM32_ADC2) = ADC_ADON;',
              '''    adc_init.ADC_ScanConvMode = DISABLE;
    adc_init.ADC_ContinuousConvMode = DISABLE;
    adc_init.ADC_DataAlign = ADC_DataAlign_Right;
    adc_init.ADC_NbrOfChannel = 1u;

    /* ADC1 是主机，TIM3 更新事件触发一对转换。 */
    adc_init.ADC_Mode = ADC_Mode_RegSimult;
    adc_init.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;
    ADC_Init(ADC1, &adc_init);
    ADC_RegularChannelConfig(ADC1, RLC_ADC_DUT_CHANNEL, 1u, ADC_SampleTime_1Cycles5);

    /* ADC2 是从机，规则组由 ADC1 同步启动。 */
    adc_init.ADC_Mode = ADC_Mode_Independent;
    adc_init.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_Init(ADC2, &adc_init);
    ADC_RegularChannelConfig(ADC2, RLC_ADC_TIA_CHANNEL, 1u, ADC_SampleTime_1Cycles5);

    ADC_Cmd(ADC1, ENABLE);
    ADC_Cmd(ADC2, ENABLE);''')
adc = replace(adc, 'CalibrateADC(STM32_ADC1) || !CalibrateADC(STM32_ADC2)',
              'CalibrateADC(ADC1) || !CalibrateADC(ADC2)')
adc = replace_region(adc, '    ADC_SQR1(STM32_ADC1)', '    TIM_SelectOutputTrigger',
                     '    ADC_ExternalTrigConvCmd(ADC2, ENABLE);\n')
adc = replace(adc, '    ADC_CR2(STM32_ADC1) &= ~(ADC_EXTTRIG | ADC_DMA);',
              '    ADC_ExternalTrigConvCmd(ADC1, DISABLE);\n    ADC_DMACmd(ADC1, DISABLE);')
adc = replace(adc, '''    SetADCSampleTime(STM32_ADC1, RLC_ADC_DUT_CHANNEL, sample_time);
    SetADCSampleTime(STM32_ADC2, RLC_ADC_TIA_CHANNEL, sample_time);
    ADC_SR(STM32_ADC1) = 0u;
    ADC_SR(STM32_ADC2) = 0u;''',
              '''    ADC_RegularChannelConfig(ADC1, RLC_ADC_DUT_CHANNEL, 1u, sample_time);
    ADC_RegularChannelConfig(ADC2, RLC_ADC_TIA_CHANNEL, 1u, sample_time);
    ADC_ClearFlag(ADC1, ADC_FRAME_FLAGS);
    ADC_ClearFlag(ADC2, ADC_FRAME_FLAGS);''')
adc = replace(adc, '    ADC_CR2(STM32_ADC1) |= ADC_EXTTRIG | ADC_DMA;',
              '    ADC_DMACmd(ADC1, ENABLE);\n    ADC_ExternalTrigConvCmd(ADC1, ENABLE);')
adc = replace(adc, 'sample_time = 0u; /* 1.5 ADC 周期。 */', 'sample_time = ADC_SampleTime_1Cycles5;')
adc = replace(adc, 'sample_time = 2u; /* 13.5 ADC 周期。 */', 'sample_time = ADC_SampleTime_13Cycles5;')
adc = replace(adc, 'sample_time = 5u; /* 55.5 ADC 周期。 */', 'sample_time = ADC_SampleTime_55Cycles5;')
write(path, adc)
