from pathlib import Path
import re
import zipfile


PROJECT = Path('C:/Users/1/Desktop/集训电赛/测量代码V1.1')
ARCHIVE = Path('C:/Users/1/Desktop/马铭君曾写的代码等300个文件 (3).zip')


def read(path):
    return (PROJECT / path).read_text(encoding='utf-8')


def write(path, content):
    destination = PROJECT / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(content, encoding='utf-8', newline='\r\n')


def replace(text, old, new, count=1):
    assert text.count(old) == count, (old[:100], text.count(old), count)
    return text.replace(old, new)


def edit(path, old, new, count=1):
    write(path, replace(read(path), old, new, count))


def replace_region(text, start, end, content):
    first = text.index(start)
    last = text.index(end, first)
    return text[:first] + content + text[last:]


def copy_reference(source, destination=None):
    dest = PROJECT / (destination or source)
    with zipfile.ZipFile(ARCHIVE) as archive:
        suffix = '/' + source
        matches = [name for name in archive.namelist() if 'finalwork_2.1' in name and name.endswith(suffix)]
        assert len(matches) == 1, matches
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(archive.read(matches[0]))


def file_entry(path):
    name = Path(path).name
    kind = {'.c': 1, '.s': 2}.get(Path(path).suffix, 5)
    file_path = '.\\' + path.replace('/', '\\')
    return (f'            <File>\n'
            f'              <FileName>{name}</FileName>\n'
            f'              <FileType>{kind}</FileType>\n'
            f'              <FilePath>{file_path}</FilePath>\n'
            f'            </File>\n')


def add_project_files(group, paths):
    entries = ''.join(file_entry(path) for path in paths)
    project = read('project.uvprojx')
    pattern = rf'(<GroupName>{re.escape(group)}</GroupName>\s*<Files>\n)(.*?)(          </Files>)'
    updated, count = re.subn(pattern, lambda m: m[1] + m[2] + entries + m[3], project, flags=re.S)
    if count == 0:
        updated = replace(project, '      </Groups>',
                          f'        <Group>\n          <GroupName>{group}</GroupName>\n'
                          f'          <Files>\n{entries}          </Files>\n        </Group>\n'
                          '      </Groups>', 3)
    else:
        assert count == 3, (group, count)
    write('project.uvprojx', updated)


def add_spl(*drivers):
    paths = []
    for driver in drivers:
        name = f'stm32f10x_{driver}'
        for suffix in ('.c', '.h'):
            path = f'Library/{name}{suffix}'
            copy_reference(path)
            paths.append(path)
    add_project_files('Library', paths)
    conf = read('USER/stm32f10x_conf.h')
    names = set(re.findall(r'^#include "(stm32f10x_\w+)\.h"', conf, re.M))
    names.update(f'stm32f10x_{driver}' for driver in drivers)
    includes = ''.join(f'#include "{name}.h"\n' for name in sorted(names))
    conf = replace_region(conf, '/* SPL drivers used by this project. */\n', '/* Exported types',
                          '/* SPL drivers used by this project. */\n' + includes + '\n')
    write('USER/stm32f10x_conf.h', conf)
