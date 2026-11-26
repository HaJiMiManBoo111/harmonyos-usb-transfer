# -*- coding: utf-8 -*-
"""Round 2: 20 global / high-tech name candidates, with the user-selected icon locked."""
import os
import shutil
import zipfile
from PIL import Image, ImageDraw, ImageFont

ROOT = r"C:\Users\Administrator\Desktop\Quick\deskflow-lingxi"
SRC = os.path.join(ROOT, "命名与图标_20261109")
OUT = os.path.join(ROOT, "命名与图标_全球化_20261109")
os.makedirs(OUT, exist_ok=True)

# the icon the user picked = round-1 candidate 06 (orange/red gradient, white rounded bar)
SEL_PNG = os.path.join(SRC, "图标_06_融屏.png")
SEL_ICO = os.path.join(SRC, "图标_06_融屏.ico")


def font(paths, size):
    for p in paths:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


LATIN = ["C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeui.ttf",
         "C:/Windows/Fonts/calibrib.ttf", "C:/Windows/Fonts/arialbd.ttf"]
LATIN_R = ["C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/calibri.ttf",
           "C:/Windows/Fonts/arial.ttf"]
CJK = ["C:/Windows/Fonts/msyhbd.ttc", "C:/Windows/Fonts/msyh.ttc",
       "C:/Windows/Fonts/simhei.ttf"]
CJK_R = ["C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"]

# idx, name, cn positioning, construction/imagery, kind
NAMES = [
    ("01", "Desklink", "桌面直连，跨端如一台", "Desk + Link（承接 Flux 血缘）", "合成词"),
    ("02", "Synapse", "突触级响应，零感延迟", "神经突触，信号瞬间跨越", "现成词"),
    ("03", "Nexora", "所有端点汇入一个核心", "Nexus + aura（连接 + 光环）", "品牌造词"),
    ("04", "Vectra", "矢量直达，路径最短", "Vector + -a（方向性传输）", "品牌造词"),
    ("05", "Aether", "无形之网，连接不在场", "以太，看不见的传输介质", "现成词"),
    ("06", "Omniflow", "一端到所有端，万物皆流", "Omni + Flow", "合成词"),
    ("07", "Quanta", "分包即达，微小而确定", "Quantum 复数，量化传输", "现成词"),
    ("08", "Orbit", "多端同轨，围绕一个工作台", "轨道", "现成词"),
    ("09", "Synova", "同步的一瞬间，新连接点亮", "Syn(chronize) + Nova", "品牌造词"),
    ("10", "Relay", "无缝中继，文件不落地", "中继站", "现成词"),
    ("11", "Meshwork", "网状互联，没有单点", "Mesh + Work", "合成词"),
    ("12", "Pulse", "两端同频，一次心跳即同步", "脉冲", "现成词"),
    ("13", "Span", "一屏跨两端，边界消失", "跨距 / 跨度", "现成词"),
    ("14", "Conduit", "数据导管，静默贯通", "导管 / 通道", "现成词"),
    ("15", "Portal", "跨屏之门，文件一越而过", "传送门", "现成词"),
    ("16", "Axora", "像轴突一样传导，无损直达", "Axon + aura", "品牌造词"),
    ("17", "Flux", "流式传输，不必等待", "通量 / 流动", "现成词"),
    ("18", "Helix", "两端绞合，一体双生", "双螺旋", "现成词"),
    ("19", "Lumio", "光速可达，看得见的快", "Lumen + -io", "品牌造词"),
    ("20", "Continuum", "连续无界，操作不中断", "连续体", "现成词"),
]

DARK = (11, 16, 32)
CARD = (22, 30, 51)
WHITE = (240, 246, 255)
GREY = (148, 163, 184)
ACCENT = (56, 189, 248)


def build_sheet(icon):
    cols, rows = 5, 4
    cw, ch = 300, 340
    pad, top = 40, 112
    W, H = cols * cw + pad * 2, rows * ch + top + 50
    sheet = Image.new("RGB", (W, H), DARK)
    d = ImageDraw.Draw(sheet)
    f_title = font(LATIN, 40)
    f_title_cjk = font(CJK, 22)
    f_sub = font(CJK_R, 20)
    f_name = font(LATIN, 31)
    f_cn = font(CJK_R, 18)
    f_num = font(LATIN_R, 17)
    f_kind = font(CJK_R, 15)

    d.text((pad, 24), "Global Naming Candidates  /  20", font=f_title, fill=WHITE)
    d.text((pad, 74), "高科技 · 世界化命名；图标已锁定为选定款（同一枚）  ·  2026-11-09",
           font=f_sub, fill=GREY)

    tile = icon.resize((216, 216), Image.LANCZOS)
    for i, (idx, name, cn, cons, kind) in enumerate(NAMES):
        c, r = i % cols, i // cols
        x, y = pad + c * cw, top + r * ch
        d.rounded_rectangle([x + 6, y + 6, x + cw - 14, y + ch - 18],
                            radius=22, fill=CARD)
        sheet.paste(tile, (x + 42, y + 12), tile)
        d.text((x + 20, y + 16), idx, font=f_num, fill=ACCENT)
        d.text((x + 40, y + 240), name, font=f_name, fill=WHITE)
        d.text((x + 40, y + 282), cn, font=f_cn, fill=GREY)
        d.text((x + 40, y + 308), kind, font=f_kind, fill=(100, 116, 139))
    path = os.path.join(OUT, "命名总览_全球化_20261109.png")
    sheet.save(path)
    return path


def build_icon_preview(icon):
    W, H = 900, 460
    img = Image.new("RGB", (W, H), DARK)
    d = ImageDraw.Draw(img)
    f_t = font(LATIN, 34)
    f_b = font(CJK_R, 22)
    f_s = font(CJK_R, 18)
    big = icon.resize((360, 360), Image.LANCZOS)
    img.paste(big, (40, 50), big)
    d.text((440, 70), "LOCKED ICON", font=f_t, fill=ACCENT)
    d.text((440, 130), "已锁定图标", font=f_b, fill=WHITE)
    d.text((440, 180), "橙红渐变底 + 白色圆角横条 + 右侧色块", font=f_s, fill=GREY)
    d.text((440, 214), "（上一轮编号 06）", font=f_s, fill=GREY)
    d.text((440, 270), "PNG 256×256  ·  ICO 16/32/48/64/128/256", font=f_s, fill=GREY)
    d.text((440, 306), "选定名字后用于：桌面快捷方式 / 窗口标题 / 卸载项", font=f_s, fill=GREY)
    path = os.path.join(OUT, "已锁定图标_20261109.png")
    img.save(path)
    return path


def build_md():
    lines = [
        "# Flux × 灵犀 合并版 · 全球化命名候选 20 个",
        "",
        "生成日期：2026-11-09。第二轮：去掉中国风意象，改为拉丁字母品牌名（全球可读、可注册）；中文仅作释义。",
        "",
        "**图标已锁定**：上一轮编号 06 那枚（橙红渐变 + 白色圆角横条 + 右侧色块），"
        "本轮 20 个名字共用这一枚，文件名 `AppIcon_Selected.png` / `AppIcon_Selected.ico`。",
        "",
        "| 编号 | 名称 | 中文定位 | 构词 / 意象 | 类型 |",
        "|---|---|---|---|---|",
    ]
    for idx, name, cn, cons, kind in NAMES:
        lines.append("| %s | **%s** | %s | %s | %s |" % (idx, name, cn, cons, kind))
    lines += [
        "",
        "## 类型分布",
        "",
        "- 品牌造词 5 个：Nexora / Vectra / Synova / Axora / Lumio（更像独立产品名，易注册）",
        "- 合成词 3 个：Desklink / Omniflow / Meshwork（语义直白，一看就懂）",
        "- 现成科技词 12 个：Synapse / Aether / Quanta / Orbit / Relay / Pulse / Span / "
        "Conduit / Portal / Flux / Helix / Continuum（国际通用，认知成本低）",
        "",
        "## 落地方式",
        "",
        "报编号后：把 `AppIcon_Selected.ico` 设为桌面快捷方式图标；快捷方式名、窗口标题、"
        "卸载注册表项 `DisplayName` 统一改成选定英文名（中文名可作副标题）。",
        "",
        "## 图标文件",
        "",
        "- `AppIcon_Selected.png` 256×256 RGBA",
        "- `AppIcon_Selected.ico` 含 16/32/48/64/128/256 六种尺寸",
        "- `命名总览_全球化_20261109.png` 5×4 联系表（名字 × 同一图标）",
        "- `已锁定图标_20261109.png` 图标单独预览",
    ]
    path = os.path.join(OUT, "命名清单_全球化_20261109.md")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return path


def build_zip():
    zpath = os.path.join(ROOT, "命名与图标_全球化_20261109.zip")
    n = 0
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(os.listdir(OUT)):
            p = os.path.join(OUT, name)
            if os.path.isfile(p):
                z.write(p, os.path.join("命名与图标_全球化_20261109", name))
                n += 1
    return zpath, n


def main():
    icon = Image.open(SEL_PNG).convert("RGBA")
    shutil.copyfile(SEL_PNG, os.path.join(OUT, "AppIcon_Selected.png"))
    shutil.copyfile(SEL_ICO, os.path.join(OUT, "AppIcon_Selected.ico"))
    sheet = build_sheet(icon)
    preview = build_icon_preview(icon)
    md = build_md()
    zpath, n = build_zip()
    print("ICON_LOCKED=06")
    print("ICON_SIZE=%s" % (icon.size,))
    print("NAMES=%d" % len(NAMES))
    print("SHEET=%s" % sheet)
    print("PREVIEW=%s" % preview)
    print("MD=%s" % md)
    print("ZIP=%s" % zpath)
    print("ZIP_BYTES=%d" % os.path.getsize(zpath))
    print("ZIP_ENTRIES=%d" % n)
    print("GEN_DONE=1")


if __name__ == "__main__":
    main()
