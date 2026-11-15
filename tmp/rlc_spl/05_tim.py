from support import add_spl, read, replace, replace_region, write


add_spl('tim')
path = 'System/ADC_Sample.c'
adc = read(path)
adc = replace_region(adc, '#define TIM3_CR1', '#define ADC_MAX_SAMPLE_PAIRS', '')
adc = replace(adc, '    STM32_RCC_APB1ENR |= 1u << 1;',
              '    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);\n'
              '    TIM_DeInit(TIM3);')
adc = replace(adc, '    TIM3_CR2 = 2u << 4; /* 更新事件输出 TRGO。 */',
              '    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);')
adc = replace(adc, '    TIM3_CR1 = 0u;', '    TIM_Cmd(TIM3, DISABLE);')
adc = replace(adc, '    TIM3_PSC = 0u;\n'
                   '    TIM3_ARR = timer_ticks - 1u;\n'
                   '    TIM3_EGR = 1u; /* 先更新定时器，再开启 ADC 外部触发。 */\n'
                   '    TIM3_SR = 0u;',
              '''    TIM_SetAutoreload(TIM3, (uint16_t)(timer_ticks - 1u));
    /* 先加载分频和周期，再开启 ADC 外部触发，配置时的 UG 不计作第一个样点。 */
    TIM_PrescalerConfig(TIM3, 0u, TIM_PSCReloadMode_Immediate);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);''')
adc = replace(adc, '    TIM3_CR1 = 1u;', '    TIM_Cmd(TIM3, ENABLE);')
write(path, adc)
