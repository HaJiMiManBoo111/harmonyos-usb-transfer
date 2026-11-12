# -*- coding: utf-8 -*-
"""Generate 20 app-icon candidates for the Flux x Lingxi merged build."""
import os
import math
from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = r"C:\Users\Administrator\Desktop\Quick\deskflow-lingxi"
OUT = os.path.join(ROOT, r"命名与图标_20261109")
os.makedirs(OUT, exist_ok=True)

S = 1024                      # supersample canvas
W = 60                        # glyph stroke width
WHITE = (255, 255, 255, 242)
FONT = "C:/Windows/Fonts/msyh.ttc"
FONT_BD = "C:/Windows/Fonts/msyhbd.ttc"


# ---------------------------------------------------------------- helpers
def hexrgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def line(d, pts, w=W, fill=WHITE):
    d.line(pts, fill=fill, width=w, joint="curve")
    r = w / 2.0
    for x, y in (pts[0], pts[-1]):
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def dot(d, x, y, r=42, fill=WHITE):
    d.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def rr(d, box, radius=70, fill=WHITE, width=0, outline=None):
    if width:
        d.rounded_rectangle(box, radius=radius, outline=outline or fill, width=width)
    else:
        d.rounded_rectangle(box, radius=radius, fill=fill)


def star(d, cx, cy, r=70, fill=WHITE):
    pts = []
    for i in range(8):
        a = math.pi * i / 4 - math.pi / 2
        rad = r if i % 2 == 0 else r * 0.34
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    d.polygon(pts, fill=fill)


# ---------------------------------------------------------------- 20 glyphs
def g01(d):  # interlocking rings
    d.ellipse([210, 322, 590, 702], outline=WHITE, width=W)
    d.ellipse([434, 322, 814, 702], outline=WHITE, width=W)


def g02(d):  # two screens + bolt
    rr(d, [140, 300, 452, 660], 54, fill=WHITE)
    rr(d, [572, 300, 884, 660], 54, fill=WHITE)
    d.polygon([(500, 300), (430, 530), (505, 530), (452, 740), (600, 490), (520, 490), (572, 300)], fill=(255, 255, 255, 255))


def g03(d):  # two screens + arrow
    rr(d, [130, 330, 420, 670], 50, fill=WHITE)
    rr(d, [604, 330, 894, 670], 50, fill=WHITE)
    line(d, [(452, 500), (572, 500)], 54)
    d.polygon([(572, 440), (660, 500), (572, 560)], fill=WHITE)


def g04(d):  # concentric ripples
    for inset in (0, 110, 220):
        d.ellipse([180 + inset, 280 + inset, 844 - inset, 744 - inset], outline=WHITE, width=W - 10)
    dot(d, 512, 512, 46)


def g05(d):  # bezier-ish curve between two points
    line(d, [(190, 660), (300, 430), (450, 620), (600, 350), (760, 520), (840, 430)], 56)
    dot(d, 190, 660, 56)
    dot(d, 840, 430, 56)


def g06(d):  # merged rectangles
    rr(d, [170, 330, 560, 690], 60, fill=WHITE)
    rr(d, [464, 330, 854, 690], 60, fill=None, width=W)
    dot(d, 512, 510, 40)


def g07(d):  # bolt between nodes
    dot(d, 250, 330, 52)
    dot(d, 774, 694, 52)
    d.polygon([(560, 250), (390, 540), (510, 540), (430, 790), (660, 470), (540, 470), (610, 250)], fill=WHITE)


def g08(d):  # breaking the frame
    rr(d, [210, 270, 814, 754], 70, fill=None, width=W)
    d.rectangle([640, 240, 850, 330], fill=(0, 0, 0, 0))
    line(d, [(400, 620), (760, 280)], 56)
    d.polygon([(760, 220), (860, 220), (860, 320)], fill=WHITE)


def g09(d):  # arch bridge
    d.arc([170, 330, 854, 830], 185, 355, fill=WHITE, width=W)
    line(d, [(170, 620), (854, 620)], 40)
    line(d, [(300, 620), (300, 760)], 40)
    line(d, [(724, 620), (724, 760)], 40)
    line(d, [(170, 790), (854, 790)], 26)


def g10(d):  # two paper planes
    d.polygon([(170, 400), (470, 320), (400, 620), (320, 470)], fill=WHITE)
    d.polygon([(854, 624), (554, 704), (624, 404), (704, 554)], fill=WHITE)


def g11(d):  # flowing lines
    for off in (0, 150, 300):
        y = 350 + off
        line(d, [(160, y + 60), (300, y - 40), (450, y + 70), (620, y - 50), (780, y + 50), (880, y - 10)], 42)


def g12(d):  # hub
    dot(d, 512, 512, 90)
    for a in (-90, 30, 150):
        rad = math.radians(a)
        ex, ey = 512 + 300 * math.cos(rad), 512 + 300 * math.sin(rad)
        line(d, [(512 + 100 * math.cos(rad), 512 + 100 * math.sin(rad)), (ex, ey)], 50)
        dot(d, ex, ey, 60)


def g13(d):  # heart + file badge
    d.ellipse([230, 320, 512, 600], fill=WHITE)
    d.ellipse([512, 320, 794, 600], fill=WHITE)
    d.polygon([(230, 470), (794, 470), (512, 790)], fill=WHITE)
    rr(d, [690, 620, 870, 810], 24, fill=(255, 255, 255, 255))
    line(d, [(730, 680), (830, 680)], 22)
    line(d, [(730, 730), (830, 730)], 22)


def g14(d):  # waveform
    pts = []
    for i in range(41):
        x = 150 + i * 18
        y = 512 - 190 * math.sin(i * 0.32)
        pts.append((x, y))
    line(d, pts, 48)


def g15(d):  # big + small screen
    rr(d, [180, 260, 700, 680], 60, fill=None, width=W)
    rr(d, [560, 520, 860, 800], 44, fill=WHITE)


def g16(d):  # bridge + star
    d.arc([190, 430, 834, 900], 190, 350, fill=WHITE, width=W - 10)
    line(d, [(190, 700), (834, 700)], 36)
    star(d, 512, 300, 110)


def g17(d):  # star-linked nodes
    dot(d, 250, 660, 58)
    dot(d, 512, 320, 58)
    dot(d, 790, 640, 58)
    line(d, [(250, 660), (512, 320), (790, 640)], 40)
    star(d, 512, 320, 46)


def g18(d):  # two halves closing
    d.arc([240, 240, 784, 784], 96, 264, fill=WHITE, width=W)
    d.arc([240, 240, 784, 784], 276, 444, fill=WHITE, width=W)
    dot(d, 512, 512, 34)


def g19(d):  # feather + chain
    line(d, [(300, 760), (720, 260)], 46)
    for i in range(5):
        t = i / 5.0
        bx, by = 300 + 420 * t, 760 - 500 * t
        line(d, [(bx, by), (bx - 160, by - 60)], 26)
        line(d, [(bx, by), (bx + 130, by - 110)], 26)
    dot(d, 250, 810, 34)
    dot(d, 300, 760, 34)


def g20(d):  # F with flow arc
    line(d, [(360, 260), (360, 760)], 66)
    line(d, [(360, 290), (700, 290)], 62)
    line(d, [(360, 510), (640, 510)], 62)
    d.arc([560, 300, 940, 780], 275, 85, fill=WHITE, width=44)


GLYPHS = [g01, g02, g03, g04, g05, g06, g07, g08, g09, g10,
          g11, g12, g13, g14, g15, g16, g17, g18, g19, g20]

# idx, cn, en, tagline, symbol, color-from, color-to
NAMES = [
    ("01", "灵犀联", "Linkxi", "心有灵犀，一联即通", "双环相扣", "#3B5BFF", "#22D3EE"),
    ("02", "屏犀", "Screenxi", "跨屏如犀角一点即透", "双屏闪电", "#7C3AED", "#EC4899"),
    ("03", "跨屏", "Kuaping", "一套键鼠，跨屏直达", "双屏箭头", "#FB923C", "#FDE047"),
    ("04", "通犀", "Tongxi", "一点波纹，两端相通", "同心波纹", "#0EA5E9", "#2563EB"),
    ("05", "一线牵", "OneLine", "一线连两端，文件随行", "曲线连点", "#0F172A", "#4F46E5"),
    ("06", "融屏", "Rongping", "两块屏融成一个工作台", "融合矩形", "#F43F5E", "#FB923C"),
    ("07", "瞬联", "Shunlian", "秒连、秒传、秒切换", "闪电节点", "#10B981", "#06B6D4"),
    ("08", "无界屏", "Boundless", "屏幕没有边界", "破框箭头", "#F59E0B", "#EF4444"),
    ("09", "屏桥", "Pingqiao", "在两台设备间架一座桥", "拱桥", "#6366F1", "#0EA5E9"),
    ("10", "双栖", "Shuangqi", "PC 与鸿蒙，双端同栖", "双向纸飞机", "#334155", "#14B8A6"),
    ("11", "犀流", "Xiliu", "数据像水流一样过去", "流线", "#38BDF8", "#8B5CF6"),
    ("12", "连枢", "Lianshu", "以一台为枢，连起所有端", "枢纽", "#4C1D95", "#2563EB"),
    ("13", "心传", "Xinchuan", "心之所想，文件即传", "心形文件", "#F472B6", "#8B5CF6"),
    ("14", "同频", "Tongpin", "两端同频，操作同步", "波形", "#065F46", "#34D399"),
    ("15", "易屏", "Yiping", "跨屏这件事变简单了", "大小双屏", "#475569", "#3B82F6"),
    ("16", "灵犀桥", "LingxiQiao", "灵犀之上再架一桥", "桥与星", "#FBBF24", "#F97316"),
    ("17", "星链屏", "Starlink", "多端如星，连成一张屏", "星点连线", "#1E1B4B", "#6366F1"),
    ("18", "合一", "Heyi", "键鼠共享与传文件合而为一", "合环", "#DC2626", "#F472B6"),
    ("19", "轻联", "Qinglian", "轻得像没连过，却一直连着", "羽毛链", "#0F766E", "#22D3EE"),
    ("20", "流犀", "Flowxi", "Flux 的流 + 灵犀的犀", "F 流线", "#0E7490", "#67E8F9"),
]


# ---------------------------------------------------------------- rendering
def gradient(c1, c2):
    a, b = hexrgb(c1), hexrgb(c2)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for y in range(S):
        t = y / (S - 1)
        col = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,)
        d.line([(0, y), (S, y)], fill=col)
    # top gloss
    gloss = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gloss)
    gd.ellipse([120, -560, 920, 300], fill=(255, 255, 255, 26))
    img = Image.alpha_composite(img, gloss)
    return img


def make_icon(idx, c1, c2, glyph):
    base = gradient(c1, c2)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([36, 36, S - 36, S - 36], radius=224, fill=255)
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    canvas.paste(base, (0, 0), mask)

    g = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    glyph(ImageDraw.Draw(g))

    # soft drop shadow for the glyph
    a = g.getchannel("A").point(lambda v: int(v * 0.5))
    sh = Image.new("RGBA", (S, S), (8, 16, 40, 0))
    sh.putalpha(a)
    off = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    off.paste(sh, (10, 16))
    canvas = Image.alpha_composite(canvas, off)
    canvas = Image.alpha_composite(canvas, g)

    small = canvas.resize((256, 256), Image.LANCZOS)
    png = os.path.join(OUT, "图标_%s_%s.png" % (idx, NAMES[int(idx) - 1][1]))
    ico = os.path.join(OUT, "图标_%s_%s.ico" % (idx, NAMES[int(idx) - 1][1]))
    small.save(png)
    small.save(ico, format="ICO", sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    return small


def make_sheet(icons):
    cols, rows = 5, 4
    cw, ch = 300, 340
    pad, top = 40, 110
    W_, H_ = cols * cw + pad * 2, rows * ch + top + 50
    sheet = Image.new("RGB", (W_, H_), (11, 16, 32))
    d = ImageDraw.Draw(sheet)
    f_title = ImageFont.truetype(FONT_BD, 42)
    f_sub = ImageFont.truetype(FONT, 22)
    f_cn = ImageFont.truetype(FONT_BD, 30)
    f_en = ImageFont.truetype(FONT, 18)
    d.text((pad, 28), "Flux × 灵犀 · 命名与图标候选（20）", font=f_title, fill=(240, 246, 255))
    d.text((pad, 78), "编号对应下方清单；名字与图标可自由搭配  ·  2026-11-09", font=f_sub, fill=(148, 163, 184))
    for i, (idx, cn, en, _tag, _sym, _c1, _c2) in enumerate(NAMES):
        c, r = i % cols, i // cols
        x, y = pad + c * cw, top + r * ch
        d.rounded_rectangle([x + 6, y + 6, x + cw - 14, y + ch - 18], radius=22, fill=(22, 30, 51))
        tile = icons[i].resize((220, 220), Image.LANCZOS)
        sheet.paste(tile, (x + 40, y + 14), tile)
        d.text((x + 40, y + 246), "%s  %s" % (idx, cn), font=f_cn, fill=(240, 246, 255))
        d.text((x + 40, y + 288), en, font=f_en, fill=(148, 163, 184))
    path = os.path.join(OUT, "图标总览_20261109.png")
    sheet.save(path)
    return path


def make_md():
    lines = [
        "# Flux × 灵犀 合并版 · 命名候选 20 个",
        "",
        "生成日期：2026-11-09。图标与名字按编号对应，可自由搭配。",
        "",
        "| 编号 | 名称 | 英文/代号 | 一句话定位 | 图标符号 | 配色 |",
        "|---|---|---|---|---|---|",
    ]
    for idx, cn, en, tag, sym, c1, c2 in NAMES:
        lines.append("| %s | %s | %s | %s | %s | %s → %s |" % (idx, cn, en, tag, sym, c1, c2))
    lines += [
        "",
        "## 图标文件",
        "",
        "- 每个候选输出 `图标_NN_名称.png`（256×256）与 `图标_NN_名称.ico`（16/32/48/64/128/256 多尺寸）。",
        "- `图标总览_20261109.png` 为 5×4 联系表。",
        "",
        "## 用法",
        "",
        "选定编号后，把对应 `.ico` 设为桌面快捷方式图标，并把 `DisplayName` / 窗口标题改成选定名称。",
    ]
    path = os.path.join(OUT, "命名清单_20261109.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return path


def main():
    icons = []
    for i, (idx, cn, en, tag, sym, c1, c2) in enumerate(NAMES):
        icons.append(make_icon(idx, c1, c2, GLYPHS[i]))
    sheet = make_sheet(icons)
    md = make_md()
    print("OUT_DIR=%s" % OUT)
    print("PNG_ICO=%d" % (len(NAMES) * 2))
    print("SHEET=%s" % sheet)
    print("MD=%s" % md)
    print("GEN_DONE=1")


if __name__ == "__main__":
    main()
