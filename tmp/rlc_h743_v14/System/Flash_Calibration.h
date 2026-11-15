/*
 * 校准记录持久化。双扇区地址、记录校验和序号在 Flash_Calibration.c。
 * 典型调用：Flash_SaveCalibration(&pending_calibration, sizeof pending_calibration)。
 * 成功保存后 App_RLC.c 才更新生效表，并在 OLED 第 4 行提示 CAL SAVED。
 */
#ifndef FLASH_CALIBRATION_H
#define FLASH_CALIBRATION_H

#include <stdbool.h>
#include <stddef.h>

/* data 指向接收校准表的变量，size 填 sizeof(该变量)；有效记录存在时返回 true。 */
bool Flash_LoadCalibration(void *data, size_t size);
/* data 指向要保存的完整校准表。写入另一扇区并校验，返回成功或失败。 */
bool Flash_SaveCalibration(const void *data, size_t size);

#endif
