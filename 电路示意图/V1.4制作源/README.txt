V1.4 图纸制作源（不用于烧录）

layout_V1.4.json：唯一的元件、接口、分阶段走线坐标来源。
make_artifacts.py：Python 标准库生成器，先核对连通与交叉，再生成相邻目录的 V1.4 SVG/TXT。
geometry.py：逐孔路径展开。
render.cjs：用已安装的 Node.js / sharp 将 V1.4 SVG 转成 PNG；不处理旧版图片。
verification_V1.4.json：最近一次图纸核对结果。

只核对：python make_artifacts.py --check-only
重出 SVG/TXT：python make_artifacts.py
重出 PNG：node render.cjs（sharp 需在 Node 的模块路径中）

生成器还读取上级目录的 V1.3 接线 TXT，以保留面包板与 H743 接口的既定内容。
模型中 bottom 线路连接所有经过孔，top 绝缘线仅连接两个端点。
背面旁路电容的长引脚必须套管；图纸检查不替代实测。
