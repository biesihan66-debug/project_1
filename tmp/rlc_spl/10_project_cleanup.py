from pathlib import Path
import re
import xml.etree.ElementTree as ET

from support import PROJECT, edit, read, replace, write


project = read('project.uvprojx')
project, count = re.subn(
    r'            <File>\n              <FileName>BSP_Registers\.h</FileName>\n'
    r'.*?            </File>\n', '', project, flags=re.S)
assert count == 3
tree = ET.fromstring(project)
targets = tree.findall('./Targets/Target')
groups = targets[0].findall('Groups/Group')
assert len(targets) == 3
paths_by_target = [[node.text for node in t.findall('Groups/Group/Files/File/FilePath')]
                   for t in targets]
assert all(paths == paths_by_target[0] for paths in paths_by_target)
assert len(set(paths_by_target[0])) == len(paths_by_target[0])
assert all((PROJECT / file_path).is_file() for file_path in paths_by_target[0])
write('project.uvprojx', project)

opt = replace(read('project.uvoptx'), 'startup_stm32f103c8.s', 'startup_stm32f10x_md.s', 2)
saved_groups = re.findall(r'(?ms)^  <Group>\n.*?^  </Group>\n', opt)
saved_group_headers = {}
saved_files = {}
for group in saved_groups:
    header, _ = group.split('    <File>\n', 1)
    name = re.search(r'<GroupName>(.*?)</GroupName>', header)[1]
    saved_group_headers[name] = header
    for item in re.findall(r'(?ms)^    <File>\n.*?^    </File>\n', group):
        saved_files[re.search(r'<PathWithFileName>(.*?)</PathWithFileName>', item)[1]] = item

new_groups = []
file_number = 0
for group_number, group in enumerate(groups, start=1):
    name = group.findtext('GroupName')
    new_group = saved_group_headers.get(name, f'''  <Group>
    <GroupName>{name}</GroupName>
    <tvExp>0</tvExp>
    <tvExpOptDlg>0</tvExpOptDlg>
    <cbSel>0</cbSel>
    <RteFlg>0</RteFlg>
''')
    for node in group.findall('Files/File'):
        file_number += 1
        file_path = node.findtext('FilePath')
        kind = node.findtext('FileType')
        name = node.findtext('FileName')
        item = saved_files.get(file_path)
        if item is None:
            item = f'''    <File>
      <GroupNumber>{group_number}</GroupNumber>
      <FileNumber>{file_number}</FileNumber>
      <FileType>{kind}</FileType>
      <tvExp>0</tvExp>
      <tvExpOptDlg>0</tvExpOptDlg>
      <bDave2>0</bDave2>
      <PathWithFileName>{file_path}</PathWithFileName>
      <FilenameWithoutPath>{name}</FilenameWithoutPath>
      <RteFlg>0</RteFlg>
      <bShared>0</bShared>
    </File>
'''
        else:
            item = re.sub(r'<GroupNumber>\d+</GroupNumber>',
                          f'<GroupNumber>{group_number}</GroupNumber>', item)
            item = re.sub(r'<FileNumber>\d+</FileNumber>',
                          f'<FileNumber>{file_number}</FileNumber>', item)
        new_group += item
    new_groups.append(new_group + '  </Group>\n')
opt = opt[:opt.index(saved_groups[0])] + '\n'.join(new_groups) + '\n' + opt[opt.index('</ProjectOpt>'):]
assert [n.text for n in ET.fromstring(opt).findall('./Group/File/PathWithFileName')] == paths_by_target[0]
write('project.uvoptx', opt)
edit('project.uvguix.1', 'startup_stm32f103c8.s', 'startup_stm32f10x_md.s')

for removed in ('System/BSP_Registers.h', 'startup/startup_stm32f103c8.s'):
    target = (PROJECT / removed).resolve()
    assert target.is_relative_to(PROJECT.resolve()) and target.is_file()
    target.unlink()

edit('System/BSP_PinMap.h', ' * 实际反馈阻容位于 BSP_Board.c。',
     ' * 板级驱动包含 stm32f10x.h 后使用这些宏。实际反馈阻容位于 BSP_Board.c。')
edit('System/ADC_Sample.c',
     '    /* ADC1->DR 是两路结果的 DMA 源地址，32 位读取才能保留 ADC2 的高半字。 */',
     '    /* SPL 需要 DMA 源地址。取 ADC1->DR 的地址，DMA 以 32 位搬运两路结果。 */')
edit('System/BSP_Board.c',
     ' * STM32F103 板级入口。负责时钟、SysTick、按键、串口及测量接口。',
     ' * 板级入口。调用时钟、GPIO、串口和测量驱动，本文件负责 SysTick 计时与测量协调。')
print(f'{len(paths_by_target[0])} files in each target; uVision project and options agree.')
