from pathlib import Path
import shutil


project = Path('C:/Users/1/Desktop/集训电赛/代码V1.3')
snapshot = Path(__file__).resolve().parent / 'before_v13_label'
changes = {
    'project.uvprojx': [(b'RLC_V1_2_', b'RLC_V1_3_', 3)],
    'build_firmware.ps1': [(b'RLC_V1_2_', b'RLC_V1_3_', 1), (b'RLC_V1.2_', b'RLC_V1.3_', 1)],
    'USER/main.c': [('RLC 电桥 V1.2'.encode(), 'RLC 电桥 V1.3'.encode(), 1)],
    'System/BSP_PinMap.h': [('V1.2 候选板级'.encode(), 'V1.3 候选板级'.encode(), 1)],
    'System/OLED.c': [('RLC V1.2 适配'.encode(), 'RLC V1.3 适配'.encode(), 1)],
    'Tests/run_host_tests.ps1': [(b'rlc-v1-2-', b'rlc-v1-3-', 1)],
    'startup/startup_stm32f10x_md.s': [(b'; RLC V1.1: Reset', b'; RLC V1.3: Reset', 1)],
    'startup/system_stm32f10x.c': [(b'/* RLC V1.1: use HSI', b'/* RLC V1.3: use HSI', 1)],
}

original = {}
updated = {}
for relative, replacements in changes.items():
    old = (project / relative).read_bytes()
    original[relative] = old
    new = old
    for before, after, count in replacements:
        assert new.count(before) == count, (relative, before)
        new = new.replace(before, after)
    updated[relative] = new

assert not snapshot.exists()
snapshot.mkdir()
for relative, old in original.items():
    path = snapshot / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(old)
shutil.copytree(project / 'Firmware', snapshot / 'Firmware')
shutil.copy2(project / '代码与功能对照_V1.2.pdf', snapshot)
for relative, new in updated.items():
    (project / relative).write_bytes(new)
print(f'{len(updated)} files updated with version labels. Old files backed up outside the project.')
