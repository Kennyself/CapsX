"""生成 CapsX 应用图标 (.ico)

图标设计：蓝色圆形 + 白色 "CX" 文字，与 tray_icon_drawer.cpp 中任务栏图标一致。
包含 16x16、32x32、48x48、256x256 四种尺寸，满足 Windows exe 图标规范。
"""
from PIL import Image, ImageDraw, ImageFont
import os

sizes = [16, 32, 48, 256]

# 为每个尺寸创建图标图像
icon_images = {}
for sz in sizes:
    img = Image.new('RGBA', (sz, sz), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # 绘制蓝色圆形，与 tray_icon_drawer.cpp 中 active 状态一致: RGB(46, 89, 163)
    circle_color = (46, 89, 163, 255)
    draw.ellipse([0, 0, sz - 1, sz - 1], fill=circle_color)

    # 绘制白色 'CX' 文字
    text_color = (255, 255, 255, 255)
    font_size = int(sz * 9 / 16)
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', font_size)
    except Exception:
        try:
            font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', font_size)
        except Exception:
            font = ImageFont.load_default()

    draw.text((sz // 2, sz // 2), 'CX', fill=text_color, font=font, anchor='mm')
    icon_images[sz] = img

# 保存为 .ico 文件，包含所有尺寸
ico_path = os.path.join('f:', os.sep, '09_Projects', 'CapsX', 'res', 'app.ico')
os.makedirs(os.path.dirname(ico_path), exist_ok=True)

# Pillow ICO 格式：sizes 参数指定嵌入的所有尺寸，
# 每个尺寸对应的图像由 icon_images 提供
icon_images[256].save(
    ico_path,
    format='ICO',
    sizes=[(s, s) for s in sizes],
    append_images=[icon_images[s] for s in sizes[:-1]]
)
print('图标已保存到:', ico_path)
print('包含尺寸:', [(s, s) for s in sizes])

# 验证生成的 ico 文件
verify = Image.open(ico_path)
print('验证 - ICO 格式:', verify.format)
print('验证 - 嵌入尺寸:', verify.info.get('sizes', 'unknown'))