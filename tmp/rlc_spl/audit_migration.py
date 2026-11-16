from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET
import zipfile

from support import ARCHIVE, PROJECT, read


scratch = Path(__file__).resolve().parent
before = scratch / 'before'
drivers = {'rcc', 'flash', 'gpio', 'usart', 'spi', 'tim', 'dma', 'adc', 'i2c'}
driver_files = {f'stm32f10x_{driver}{suffix}' for driver in drivers for suffix in ('.c', '.h')}

# Prove that application, RLC math, classification, software calibration and public headers
# did not change. The sampling initializer is the single public header change.
changed_board_code = {'ADC_Sample.c', 'ADC_Sample.h', 'BSP_Board.c', 'BSP_PinMap.h',
                      'DDS_Signal.c', 'Flash_Calibration.c', 'OLED_I2C.c', 'Range_Switch.c',
                      'BSP_Registers.h'}
for source in (before / 'System').iterdir():
    if source.is_file() and source.name not in changed_board_code:
        assert source.read_bytes() == (PROJECT / 'System' / source.name).read_bytes(), source
for file in ('USER/main.c', 'startup/stm32f103c8_flash.sct', 'Tests/run_host_tests.ps1',
             'Tests/test_rlc_measurement.c', 'Tests/test_key.c'):
    assert (before / file).read_bytes() == (PROJECT / file).read_bytes(), file
assert read('System/ADC_Sample.h') == (before / 'System/ADC_Sample.h').read_text(encoding='utf-8').replace(
    'bool ADC_Init(void);', 'bool BSP_ADC_Init(void);')
print('App/main, algorithm, calibration math, status enums, display and public APIs match baseline, except BSP_ADC_Init.')

assert {p.name for p in (PROJECT / 'Library').iterdir()} == driver_files
with zipfile.ZipFile(ARCHIVE) as archive:
    for name in driver_files:
        matches = [n for n in archive.namelist() if 'finalwork_2.1' in n and n.endswith('/Library/' + name)]
        assert len(matches) == 1 and archive.read(matches[0]) == (PROJECT / 'Library' / name).read_bytes(), name
    for name in ('core_cm3.h', 'stm32f10x.h', 'system_stm32f10x.h'):
        matches = [n for n in archive.namelist() if 'finalwork_2.1' in n and n.endswith('/start/' + name)]
        assert len(matches) == 1 and archive.read(matches[0]) == (PROJECT / 'startup' / name).read_bytes(), name
assert set(re.findall(r'^#include "(stm32f10x_\w+\.h)"', read('USER/stm32f10x_conf.h'), re.M)) == {
    f'stm32f10x_{driver}.h' for driver in drivers}
print('Reference SPL V3.5.0: nine driver pairs match the ZIP byte for byte.')

def defines(path):
    return dict(re.findall(r'^#define\s+(RLC_\w+)\s+([^\r\n]+)', path.read_text(encoding='utf-8'), re.M))


old_pins = defines(before / 'System/BSP_PinMap.h')
new_pins = defines(PROJECT / 'System/BSP_PinMap.h')
for name, value in old_pins.items():
    if name.endswith('_GPIO'):
        value = value.removeprefix('STM32_')
    if name.endswith('_PIN'):
        value = 'GPIO_Pin_' + value.removesuffix('u')
    assert value == new_pins[name], (name, value, new_pins[name])
print('All pin, clock, baud, ADC and Flash configuration constants match baseline.')

before_targets = ET.parse(before / 'project.uvprojx').findall('./Targets/Target')
targets = ET.parse(PROJECT / 'project.uvprojx').findall('./Targets/Target')
options = ET.parse(PROJECT / 'project.uvoptx')
previous_options = ET.parse(before / 'project.uvoptx')
assert len(targets) == len(before_targets) == 3
assert [ET.tostring(n) for n in options.findall('./Target')] == [
    ET.tostring(n) for n in previous_options.findall('./Target')]
opt_files = [x.text for x in options.findall('./Group/File/PathWithFileName')]
production_sources = {p.resolve() for folder in ('System', 'USER', 'Library', 'startup')
                      for p in (PROJECT / folder).iterdir() if p.suffix in ('.c', '.h', '.s', '.sct')}
assert len(opt_files) == len(production_sources)

for original, target in zip(before_targets, targets):
    name = target.findtext('TargetName')
    assert name == original.findtext('TargetName')
    controls = 'TargetOption/TargetArmAds/Cads/VariousControls/'
    defines = set(target.findtext(controls + 'Define').split(','))
    old_defines = set(filter(None, original.findtext(controls + 'Define').split(',')))
    assert defines == old_defines | {'STM32F10X_MD', 'USE_STDPERIPH_DRIVER'}
    assert target.findtext(controls + 'IncludePath') == r'.\startup;.\USER;.\Library;.\System'
    for key in ('Device', 'PackID', 'Cpu', 'FlashDriverDll'):
        p = 'TargetOption/TargetCommonOption/' + key
        assert target.findtext(p) == original.findtext(p)
    files = [x.text for x in target.findall('Groups/Group/Files/File/FilePath')]
    assert files == opt_files
    assert {(PROJECT / p).resolve() for p in files} == production_sources
    assert not any(re.search(r'(?i)stm32.*_hal|cubemx', p) for p in files)

    obj = PROJECT / ('Objects_' + name)
    artifact = f'RLC_V1_1_{name}'
    log = (obj / 'keil_build.log').read_text()
    assert f"Rebuild target '{name}'" in log and '0 Error(s), 0 Warning(s).' in log
    assert 'assembling startup_stm32f10x_md.s...' in log and 'compiling system_stm32f10x.c...' in log
    assert all(f'compiling stm32f10x_{driver}.c...' in log for driver in drivers)
    assert all(p.stat().st_mtime <= (obj / 'keil_build.log').stat().st_mtime for p in production_sources)
    sizes = tuple(map(int, re.search(r'Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', log).groups()))
    flash_bytes = sum(sizes[:3])
    ram_bytes = sum(sizes[2:])
    assert flash_bytes <= 62 * 1024 and ram_bytes <= 20 * 1024

    firmware = PROJECT / 'Firmware' / f'RLC_V1.1_{name}.hex'
    assert firmware.read_bytes() == (obj / f'{artifact}.hex').read_bytes()
    image = {}
    base = 0
    eof = False
    for line in firmware.read_text().splitlines():
        raw = bytes.fromhex(line[1:])
        assert not eof and line.startswith(':') and sum(raw) % 256 == 0 and len(raw) == raw[0] + 5
        size, offset, kind = raw[0], int.from_bytes(raw[1:3], 'big'), raw[3]
        data = raw[4:4 + size]
        if kind == 0:
            address = base + offset
            assert 0x08000000 <= address < address + size <= 0x0800F800
            image.update(zip(range(address, address + size), data))
        elif kind in (2, 4):
            base = int.from_bytes(data, 'big') << (4 if kind == 2 else 16)
        elif kind == 1:
            eof = True
    assert eof and len(image) == flash_bytes
    stack, reset, *_, systick = struct.unpack('<16I', bytes(image[x] for x in range(0x08000000, 0x08000040)))
    symbols = (obj / f'{artifact}.map').read_text(errors='replace')
    for address, symbol, obj_name in ((reset, 'Reset_Handler', 'startup_stm32f10x_md.o'),
                                       (systick, 'SysTick_Handler', 'bsp_board.o')):
        assert 0x08000000 <= address < 0x0800F800 and address & 1
        assert re.search(rf'^\s+{symbol}\s+0x{address:08x}\s+Thumb Code\s+\d+\s+{re.escape(obj_name)}\(',
                         symbols, flags=re.M | re.I), (symbol, address)
    assert 0x20000800 <= stack <= 0x20005000 and stack % 8 == 0
    print(f'{name}: {len(files)} project files, {flash_bytes} Flash bytes, {ram_bytes} SRAM bytes. Vectors and HEX verified.')

for file in production_sources:
    if file.parent.name not in ('System', 'USER') or file.suffix not in ('.c', '.h'):
        continue
    assert not re.search(r'\b(?:STM32_REG\d+|BSP_Registers|HAL_\w+|MX_\w+)', file.read_text(encoding='utf-8'))

for stage in ('01-clock', '02-gpio', '03-usart', '04-spi', '05-tim3', '06-dma',
              '07-adc', '08-i2c', '09-flash'):
    log = (scratch / f'{stage}-Full.log').read_text()
    assert '0 Error(s), 0 Warning(s).' in log, stage
print('Nine separate peripheral migration builds passed.')
