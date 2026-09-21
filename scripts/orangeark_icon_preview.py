#!/usr/bin/env python3
"""OrangeArk: render a preview sheet of the search / format-painter icons.

Writes `icon-preview.html` (self-contained, no external assets) so the new
orange artwork can be eyeballed at several sizes on light and dark backgrounds.

Run:  python scripts/orangeark_icon_preview.py
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ICON_DIR = ROOT / "icons"

GROUPS = [
    ("搜索系列（橙色放大镜）", [
        ("ct_find", "查找 / 浏览"),
        ("ct_find_again", "查找下一个"),
        ("ct_find_back", "查找上一个"),
        ("ct_find_replace", "查找替换"),
        ("ct_find_all", "多节点查找"),
        ("ct_find_sel", "在节点内容中查找"),
        ("ct_find_selnsub", "在节点名与标签中查找"),
    ]),
    ("格式与符号", [
        ("ct_fmt-txt-clone", "格式刷 Format Clone"),
        ("ct_fmt-txt-latest", "重复上次格式 Repeat Last Format"),
        ("ct_special_char", "特殊符号选择器"),
    ]),
]


def read_svg(name: str) -> str:
    p = ICON_DIR / (name + ".svg")
    if not p.is_file():
        return '<span style="color:#c62828">missing %s</span>' % name
    txt = p.read_text(encoding="utf-8")
    # drop the xml declaration, keep the <svg> element inline
    return txt.split("?>", 1)[-1].strip()


def card(name, label, size, bg, fg):
    svg = read_svg(name)
    svg = svg.replace("<svg ", '<svg style="width:%dpx;height:%dpx" ' % (size, size), 1)
    return (
        '<div style="background:%s;color:%s;border:1px solid #d8d8d8;border-radius:10px;'
        'padding:12px 10px;text-align:center;min-width:132px">'
        '<div style="display:flex;gap:14px;align-items:flex-end;justify-content:center;height:%dpx">%s</div>'
        '<div style="font-size:11px;margin-top:9px;line-height:1.45">'
        '<b>%s</b><br><span style="opacity:.65;font-family:monospace;font-size:10px">%s</span></div>'
        "</div>" % (bg, fg, size + 6, svg, label, name)
    )


def main():
    out = []
    out.append(
        "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>OrangeArk 图标预览</title></head>"
        "<body style=\"margin:0;padding:26px;background:#fafafa;color:#1f1f1f;"
        "font-family:'Microsoft YaHei',system-ui,sans-serif\">"
        "<h1 style=\"margin:0 0 4px;font-size:21px\">OrangeArk 图标预览</h1>"
        "<p style=\"margin:0 0 22px;color:#666;font-size:13px\">"
        "根级 <code>icons/</code> 下的图标会被编译进 gresource 并随 exe 发布 —— 这一批已全部改为橙色系。"
        "</p>"
    )
    for title, items in GROUPS:
        out.append('<h2 style="font-size:15px;margin:24px 0 12px">%s</h2>' % title)
        out.append('<div style="display:flex;flex-wrap:wrap;gap:12px">')
        for name, label in items:
            out.append(card(name, label, 32, "#ffffff", "#1f1f1f"))
        out.append("</div>")
        out.append('<h3 style="font-size:12px;margin:16px 0 8px;color:#777">深色主题下的表现（16px 实际尺寸）</h3>')
        out.append('<div style="display:flex;flex-wrap:wrap;gap:12px">')
        for name, label in items:
            out.append(card(name, label, 16, "#2b2b2b", "#e8e8e8"))
        out.append("</div>")
    out.append("</body></html>")

    dest = ROOT / "icon-preview.html"
    dest.write_text("\n".join(out), encoding="utf-8")
    print("wrote", dest)


if __name__ == "__main__":
    main()
