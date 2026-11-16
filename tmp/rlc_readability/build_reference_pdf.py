from pathlib import Path
from xml.sax.saxutils import escape
import re

from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph, Table, TableStyle
from pypdf import PdfReader

ROOT = Path('C:/Users/1/Desktop/集训电赛/测量代码V1.1')
OUT = Path(__file__).resolve().parent / '代码与功能对照_V1.1_待校对.pdf'
pdfmetrics.registerFont(TTFont('YaHei', 'C:/Windows/Fonts/msyh.ttc'))
pdfmetrics.registerFont(TTFont('YaHeiBold', 'C:/Windows/Fonts/msyhbd.ttc'))
pdfmetrics.registerFont(TTFont('Code', 'C:/Windows/Fonts/consola.ttf'))
pdfmetrics.registerFontFamily('YaHei', normal='YaHei', bold='YaHeiBold', italic='YaHei', boldItalic='YaHeiBold')
TEAL = colors.HexColor('#096F68')
INK = colors.HexColor('#152F3A')
GRAY = colors.HexColor('#496775')
LIGHT = colors.HexColor('#EEF6F5')
LINE = colors.HexColor('#CADCDD')
W, H = 595.276, 841.89
M, WIDTH = 36, W - 72
c = canvas.Canvas(str(OUT), pagesize=(W, H))
c.setTitle('RLC电桥 V1.1 - 代码与功能对照（两页版）')
c.setAuthor('RLC 电桥项目')


def line(path, pattern):
    matches = [i for i, text in enumerate((ROOT / path).read_text(encoding='utf-8-sig').splitlines(), 1)
               if re.search(pattern, text)]
    assert matches, (path, pattern)
    return matches[0]


def where(path, pattern):
    return f'{Path(path).name}:{line(path, pattern)}'


def para(text, size=9, color=INK, bold=False, mono=False):
    return Paragraph(text, ParagraphStyle('p', fontName='Code' if mono else ('YaHeiBold' if bold else 'YaHei'),
                     fontSize=size, leading=size * 1.43, textColor=color, wordWrap='CJK',
                     spaceBefore=0, spaceAfter=0))


def draw_p(text, x, top, width, size=9, color=INK, bold=False, mono=False):
    p = para(text, size, color, bold, mono)
    _, height = p.wrap(width, 1000)
    p.drawOn(c, x, top-height)
    return top-height


def heading(text, y):
    return draw_p(text, M, y, WIDTH, 11.5, INK, True)-7


def table(headers, rows, widths, top, font_size=8.5, padding=6):
    contents = [[para(escape(h), 8.7, colors.white, True) for h in headers]]
    contents += [[cell if isinstance(cell, Paragraph) else para(cell, font_size) for cell in row] for row in rows]
    t = Table(contents, colWidths=widths, hAlign='LEFT')
    t.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), INK),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#F3F7F7')]),
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('LEFTPADDING', (0, 0), (-1, -1), padding),
        ('RIGHTPADDING', (0, 0), (-1, -1), padding),
        ('TOPPADDING', (0, 0), (-1, -1), padding-1),
        ('BOTTOMPADDING', (0, 0), (-1, -1), padding-1),
        ('LINEBELOW', (0, -1), (-1, -1), .5, LINE),
    ]))
    _, height = t.wrap(WIDTH, 1000)
    assert top-height > 53, f'table exceeds page bottom: {top-height}'
    t.drawOn(c, M, top-height)
    return top-height


def header(title, subtitle, section, page):
    c.setFillColor(TEAL)
    c.rect(0, H-7, W, 7, fill=1, stroke=0)
    draw_p('RLC 电桥 / V1.1', M, H-28, WIDTH, 9.5, TEAL, True)
    c.setFont('YaHei', 8.5)
    c.setFillColor(GRAY)
    c.drawRightString(W-M, H-39, section)
    draw_p(title, M, H-57, WIDTH, 22, INK, True)
    draw_p(subtitle, M, H-94, WIDTH, 9, GRAY)
    c.setStrokeColor(LINE)
    c.line(M, H-117, W-M, H-117)
    c.line(M, 42, W-M, 42)
    draw_p('对应桌面「集训电赛 / 测量代码V1.1」当前源码 · 2026-11-12', M, 31, WIDTH, 7.7, GRAY)
    c.setFont('Code', 8)
    c.drawRightString(W-M, 21, f'{page} / 2')


def code(text, size=8.0):
    return para(escape(text), size, mono=True)


header('模块与调用速查', 'STM32F103C8T6 / ST-LINK（SWD）/ Keil µVision / OLED', '模块与实际接口', 1)
flow_top, box_h, gap = 711, 43, 12
box_w = (WIDTH-gap*3)/4
for index, (title, detail) in enumerate([
    ('按键触发', '短按测量 / 长按校准'),
    ('自动设置', '1 kHz 起测，选频与选档'),
    ('采样与计算', '双 ADC + DMA / I、Q 解调'),
    ('结果输出', 'OLED 4 行 + USART 诊断'),
]):
    x = M + index*(box_w+gap)
    c.setFillColor(LIGHT)
    c.roundRect(x, flow_top-box_h, box_w, box_h, 5, fill=1, stroke=0)
    draw_p(title, x+8, flow_top-6, box_w-16, 9.4, TEAL, True)
    draw_p(detail, x+8, flow_top-24, box_w-16, 7.7, GRAY)
    if index < 3:
        c.setFont('YaHeiBold', 12)
        c.setFillColor(TEAL)
        c.drawString(x+box_w+3, flow_top-26, '>')

y = heading('先找文件，再看入口', 654)
y = draw_p('除 main.c 外均在 System。同名 .h 说明接口，.c 实现功能；括号为函数定义行号。', M, y, WIDTH, 8, GRAY)-7
modules = [
    ('USER/main.c', '初始化与主循环；只含 App_RLC.h', [('main', 'USER/main.c')]),
    ('App_RLC.c / Key.c', '测量/校准页面；按键去抖、长短按', [('App_Init', 'System/App_RLC.c'), ('App_Run', 'System/App_RLC.c'), ('Key_GetEvent', 'System/Key.c')]),
    ('RLC_Measure.c', '试测、选频选档、复测、超时', [('RLC_MeasureAuto', 'System/RLC_Measure.c')]),
    ('DDS_Signal.c /\nRange_Switch.c', 'AD9833 激励、反馈档切换；板级协调稳定时间', [('DDS_SetFrequency', 'System/DDS_Signal.c'), ('Range_Set', 'System/Range_Switch.c')]),
    ('ADC_Sample.c', '双 ADC 同步采样，DMA 交付采样帧', [('ADC_Capture', 'System/ADC_Sample.c')]),
    ('RLC_Calculate.c', '去直流、解调相量<br/>Z = -Vdut · Zf / Vtia', [('RLC_Demodulate', 'System/RLC_Calculate.c'), ('RLC_CalculateImpedance', 'System/RLC_Calculate.c')]),
    ('RLC_Calibration.c', '按频率/反馈档补偿夹具寄生', [('Calibration_Apply', 'System/RLC_Calibration.c')]),
    ('Flash_Calibration.c', '双页备份，校验后加载/保存校准', [('Flash_LoadCalibration', 'System/Flash_Calibration.c'), ('Flash_SaveCalibration', 'System/Flash_Calibration.c')]),
    ('OLED_Display.c /\nOLED_I2C.c', '四行排版、工程单位、错误提示；I2C 发送与恢复', [('OLED_ShowResult', 'System/OLED_Display.c'), ('OLED_I2C_Write', 'System/OLED_I2C.c')]),
    ('Serial_Debug.c /\nText_Format.c', '串口幅相、阻抗、用时；OLED/串口共用数值格式化', [('Serial_PrintResult', 'System/Serial_Debug.c'), ('Text_AddScientific', 'System/Text_Format.c')]),
    ('BSP_Board.c', '时钟、GPIO、计时；直接设档与采样', [('BSP_SelectMeasurement', 'System/BSP_Board.c'), ('BSP_CaptureFrame', 'System/BSP_Board.c')]),
]
rows = []
for filename, duty, funcs in modules:
    functions = []
    for name, file in funcs:
        source_line = line(file, rf'^(?:static\s+)?(?:const\s+)?\w+\s*\*?\s*{name}\(')
        functions.append(f'{name}()  ({source_line})')
    rows.append([para(escape(filename).replace('\n','<br/>'), 8, mono=True), para(duty, 8.5), para('<br/>'.join(map(escape, functions)), 8, mono=True)])
y = table(['文件', '职责', '实际入口（行号）'], rows, [157, 151, WIDTH-308], y, padding=5)-12
y = heading('应用层的实际调用方式', y)
calls = [
    'result = RLC_MeasureAuto(&active_calibration, measurement_mode);',
    'result = RLC_MeasureFixed(&active_calibration,',
    '                           RLC_FREQUENCY_1_KHZ, RLC_RANGE_1_KOHM);',
    'status = RLC_CaptureCalibration(&pending_calibration, open, measurement_mode);',
    'OLED_ShowResult(&result, feedback_ohm);',
]
c.setFillColor(LIGHT)
c.roundRect(M, y-75, WIDTH, 75, 5, fill=1, stroke=0)
for i, text in enumerate(calls):
    draw_p(escape(text).replace(' ', '&nbsp;'), M+10, y-7-i*12, WIDTH-20, 8, mono=True)
y -= 83
y = draw_p('measurement_mode 选择基础或扩展模式；返回结果的 status 为 OK 时数值有效。'
           'open=true 采开路、false 采短路；两步成功并存入 Flash 后才更新生效校准。',
           M, y, WIDTH, 8.5, GRAY)
y = draw_p('阅读顺序：main.c → App_RLC.h/.c → RLC_Measure.h/.c。函数按“模块_动作”命名，'
           '变量带含义和单位，文件头注明职责、关键变量位置及显示行号。', M, y-7, WIDTH, 8.5, TEAL)
assert y > 53, y
c.showPage()

header('变量位置与 OLED 行号', '位置格式为“文件:行号”；变量以当前实际名称为准，行号从 1 开始。', '变量、显示与联调', 2)
c.setFillColor(INK)
c.roundRect(M, 619, 196, 92, 6, fill=1, stroke=0)
for i, text in enumerate(['R  RESISTANCE', '1 kOHM', '1 kHz', 'RF 1 kOHM CAL']):
    draw_p(f'{i+1:02}', M+12, 700-i*20, 18, 8.5, colors.HexColor('#88C9C2'), mono=True)
    draw_p(escape(text), M+39, 701-i*20, 150, 10.5, colors.white, mono=True)
display_notes = [
    '第 1 行：元件类型；失败时显示 MEASURE ERROR。',
    '第 2 行：元件值及单位；失败时显示 ---。',
    '第 3 行：当前标称测试频率。',
    '第 4 行：反馈档 + CAL/RAW；失败时改为原因。',
]
for i, text in enumerate(display_notes):
    draw_p(text, M+210, 708-i*20, WIDTH-210, 9)
y = draw_p('逻辑行 1/2/3/4 对应 SSD1306 页 0/2/4/6，起始 y=0/16/32/48 像素；'
           '版面定义在 '+where('System/OLED_Display.c', r'^    OLED_TYPE_PAGE'), M, 609, WIDTH, 8, GRAY)-10
y = heading('关键变量：在哪里、存什么、显示到哪里', y)
variables = [
    ('packed_adc_samples[]', where('System/ADC_Sample.c', r'^static uint32_t packed_adc_samples'),
     'DMA 缓冲；每项低 16 位为 DUT 电压，高 16 位为 TIA 电压，交算法解调。'),
    ('phasors / impedance_ohm', where('System/RLC_Types.h', r'^    RLC_Complex impedance_ohm'),
     'RLC_Result 成员；相量单位 V、阻抗单位 Ω，用于计算与串口核对。'),
    ('component / status', where('System/RLC_Types.h', r'^    RLC_Status status'),
     '测量结果的类型/状态；类型到第 1 行，错误原因到第 4 行。仅 OK 时数值有效。'),
    ('component_value', where('System/RLC_Types.h', r'^    float component_value'),
     '结果值单位 Ω/H/F；显示模块自动换算为 kΩ、mH、uH、uF、nF、pF 等，第 2 行。'),
    ('frequency_hz', where('System/RLC_Types.h', r'^    uint32_t frequency_hz'),
     '结果的标称频率，Hz；第 3 行。频率表 rlc_frequency_hz[] 定义在 RLC_Measure.c。'),
    ('range_index / calibrated', where('System/RLC_Types.h', r'^    uint8_t range_index'),
     '反馈档下标/是否已补偿；第 4 行显示 RF 及 CAL/RAW。'),
    ('bsp_feedback_ohm[] /\nbsp_feedback_capacitance_f[]', where('System/BSP_Board.c', r'^const float bsp_feedback_ohm'),
     '两组只读板级参数，按反馈档下标查询。分别填实测有效反馈电阻（Ω）和并联电容（F）。'),
    ('open_ohm[][] / short_ohm[][]', where('System/RLC_Calibration.h', r'^    RLC_Complex open_ohm'),
     'RLC_Calibration 成员，下标 [频率][反馈档]；open_mask、short_mask、ideal_open_mask 标记有效项。'),
    ('active_calibration /\npending_calibration', where('System/App_RLC.c', r'^static RLC_Calibration active_calibration'),
     '应用静态变量；分别为正在使用/待提交的校准表，失败或取消保留旧表。'),
    ('button / app_page', where('System/Key.c', r'^static ButtonState button')+' / '+where('System/App_RLC.c', r'^static AppPage app_page'),
     '按键状态在 Key.c，时间单位 ms；页面状态在 App_RLC.c，取测量/开路/短路校准。'),
]
rows = [[para(escape(name).replace('\n', '<br/>'), 7.6, mono=True), code(location, 7.3), para(description, 8.2)]
        for name, location, description in variables]
y = table(['变量 / 结构体成员', '定义位置', '含义与去向'], rows, [135, 153, WIDTH-288], y, padding=5)-11
y = heading('联调只需记住这些', y)
notes = [
    '<b>Keil：</b>打开 project.uvprojx。Full=四频自动；Basic=100 Hz/1 kHz/10 kHz；'
    'Fixed1k=1 kHz、1 kΩ 固定档。选目标后 Rebuild，用 ST-LINK 下载；HEX 位于 Firmware/RLC_V1.1_&lt;目标名&gt;.hex。',
    '<b>按键：</b>PB10 短按测量；长按 900 ms 进入校准，依次空夹具、短接夹具后短按采集；'
    '长按取消。Fixed1k 长按仅显示 BENCH TEST。首次联调用 1 kΩ 标准电阻。',
    '<b>接线与参数：</b>BSP_PinMap.h 集中定义：ADC PA0/PA1；OLED PB6/PB7；'
    'DDS PA4/PA5/PA7、MCLK PA8；档位 PB0/PB1/PB11；串口 TX PA9；SWD PA13/PA14。',
    '<b>验证：</b>3606 点基础/扩展扫描、非整数周期解调、校准回滚、DDS 超时、按键及 Keil 三目标构建通过。'
    '实际误差与响应时间仍需实物确认；改线或反馈元件变化时同步修改板级参数。',
]
for note in notes:
    y = draw_p(note, M, y, WIDTH, 8.4)-5
assert y > 53, y
c.save()
assert len(PdfReader(OUT).pages) == 2
print('PDF generated: 2 pages; all source locations resolved.')
