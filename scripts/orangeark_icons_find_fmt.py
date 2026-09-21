#!/usr/bin/env python3
"""OrangeArk: regenerate the search / format-painter icon family in orange.

The icons that are actually shipped inside the executable come from the ROOT
`icons/` directory: `ct_app.cc` registers the gresource under `/icons/`, and
`icons.gresource.xml` lists the root-level `*.svg` files only.  Earlier rounds
only touched `icons/Breeze_*_icons/` and `icons/48px/`, so the app kept showing
the original blue CherryTree artwork.

This script writes the same orange artwork to every icon location so that all
of them agree.

Run:  python scripts/orangeark_icons_find_fmt.py
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

HEAD = '<?xml version="1.0" encoding="UTF-8"?>\n'

# ---------------------------------------------------------------- magnifier
# shared geometry: lens at (6.9, 6.3) r4.3, handle down to (13.9, 13.3)
def _lens(stroke="#fb8c00", w=1.9, hw=2.2):
    return (
        '  <circle cx="6.9" cy="6.3" r="4.3" fill="none" stroke="%s" stroke-width="%s"/>\n'
        '  <line x1="10.2" y1="9.6" x2="13.9" y2="13.3" stroke="%s" stroke-width="%s" stroke-linecap="round"/>\n'
        % (stroke, w, stroke, hw)
    )


def svg(comment, body):
    return (
        HEAD
        + '<!-- OrangeArk: %s -->\n' % comment
        + '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n'
        + body
        + '</svg>\n'
    )


ICONS = {}

# ------------------------------------------------------------- search family
ICONS["ct_find"] = svg(
    "search / browse - orange magnifier with a handle",
    _lens(),
)

ICONS["ct_find_again"] = svg(
    "find next forward - orange magnifier plus a right arrow",
    _lens()
    + '  <line x1="1.2" y1="12.7" x2="5.2" y2="12.7" stroke="#e65100" stroke-width="1.7" stroke-linecap="round"/>\n'
    + '  <path d="M 4.2 11.1 L 6.6 12.7 L 4.2 14.3 Z" fill="#e65100"/>\n',
)

ICONS["ct_find_back"] = svg(
    "find next backward - orange magnifier plus a left arrow",
    _lens()
    + '  <line x1="6.8" y1="12.7" x2="2.8" y2="12.7" stroke="#e65100" stroke-width="1.7" stroke-linecap="round"/>\n'
    + '  <path d="M 3.8 11.1 L 1.4 12.7 L 3.8 14.3 Z" fill="#e65100"/>\n',
)

ICONS["ct_find_replace"] = svg(
    "find and replace - orange magnifier plus a pencil",
    _lens()
    + '  <path d="M 1.3 14.9 L 2.0 12.4 L 3.9 14.3 Z" fill="#e65100"/>\n'
    + '  <path d="M 2.4 11.6 L 3.7 10.3 L 6.0 12.6 L 4.7 13.9 Z" fill="#ffb74d"/>\n',
)

ICONS["ct_find_all"] = svg(
    "find in multiple nodes - two stacked orange magnifiers",
    '  <circle cx="5.1" cy="4.9" r="3.6" fill="none" stroke="#ffcc80" stroke-width="1.6"/>\n'
    + '  <line x1="7.7" y1="7.5" x2="9.6" y2="9.4" stroke="#ffcc80" stroke-width="1.8" stroke-linecap="round"/>\n'
    + '  <circle cx="7.7" cy="7.5" r="3.9" fill="none" stroke="#fb8c00" stroke-width="1.8"/>\n'
    + '  <line x1="10.5" y1="10.3" x2="14.0" y2="13.8" stroke="#fb8c00" stroke-width="2.1" stroke-linecap="round"/>\n',
)

ICONS["ct_find_sel"] = svg(
    "find in node content - orange magnifier plus a dashed selection box",
    _lens()
    + '  <rect x="1.1" y="11.3" width="5.5" height="3.5" rx="0.7" fill="none" stroke="#e65100" '
      'stroke-width="1.2" stroke-dasharray="1.6 1.1"/>\n',
)

ICONS["ct_find_selnsub"] = svg(
    "find in nodes names and tags - orange magnifier plus a filtered list",
    _lens()
    + '  <rect x="1.1" y="10.7" width="5.6" height="3.9" rx="0.7" fill="none" stroke="#e65100" stroke-width="1.2"/>\n'
    + '  <line x1="2.3" y1="12.0" x2="5.5" y2="12.0" stroke="#e65100" stroke-width="1.0" stroke-linecap="round"/>\n'
    + '  <line x1="2.3" y1="13.4" x2="4.4" y2="13.4" stroke="#e65100" stroke-width="1.0" stroke-linecap="round"/>\n',
)

# ------------------------------------------------------------ format painter
def _brush(wood):
    """A small paint brush pointing to the upper left (格式刷)."""
    return (
        '  <g transform="rotate(-45 8 8)">\n'
        '    <path d="M 8 1.1 C 9.7 2.4 10.2 3.9 10.2 5.2 L 5.8 5.2 C 5.8 3.9 6.3 2.4 8 1.1 Z" fill="#ffb74d"/>\n'
        '    <path d="M 5.8 5.2 L 10.2 5.2 L 9.9 6.9 L 6.1 6.9 Z" fill="#9e9e9e"/>\n'
        '    <path d="M 6.1 6.9 L 9.9 6.9 L 9.5 13.7 C 9.4 14.4 8.7 15.0 8.0 15.0 '
        'C 7.3 15.0 6.6 14.4 6.5 13.7 Z" fill="%s"/>\n'
        '  </g>\n' % wood
    )


ICONS["ct_fmt-txt-clone"] = svg("format painter (格式刷) - a small brush", _brush("#b0743c"))

ICONS["ct_fmt-txt-latest"] = svg(
    "repeat last format - a small brush with a repeat arrow",
    '  <path d="M 2.6 4.6 A 3.2 3.2 0 0 1 7.4 2.3" fill="none" stroke="#fb8c00" stroke-width="1.4" stroke-linecap="round"/>\n'
    + '  <path d="M 8.2 0.7 L 8.3 3.6 L 5.6 2.8 Z" fill="#fb8c00"/>\n'
    + '  <path d="M 9.4 5.0 L 14.1 9.7 C 14.7 10.3, 14.7 11.1, 14.1 11.7 '
      'C 13.5 12.3, 12.7 12.3, 12.1 11.7 L 7.4 7.0 Z" fill="#b0743c"/>\n'
    + '  <path d="M 7.4 7.0 L 9.4 9.0 L 8.0 10.4 L 6.0 8.4 Z" fill="#9e9e9e"/>\n'
    + '  <path d="M 6.0 8.4 L 8.0 10.4 L 5.2 13.2 C 4.4 14.0, 3.2 14.1, 2.5 13.4 '
      'C 1.8 12.7, 1.9 11.5, 2.7 10.7 Z" fill="#ffb74d"/>\n',
)

# dark-theme tweak: a lighter handle reads better on a dark background
DARK_OVERRIDES = {
    "ct_fmt-txt-clone": {"#b0743c": "#d6a877"},
    "ct_fmt-txt-latest": {"#b0743c": "#d6a877"},
}


def main():
    targets = [
        ROOT / "icons",
        ROOT / "icons" / "48px",
        ROOT / "icons" / "Breeze_Light_icons",
        ROOT / "icons" / "Breeze_Dark_icons",
    ]
    written = 0
    for tdir in targets:
        if not tdir.is_dir():
            print("skip (missing):", tdir)
            continue
        dark = tdir.name == "Breeze_Dark_icons"
        for name, content in ICONS.items():
            body = content
            if dark and name in DARK_OVERRIDES:
                for old, new in DARK_OVERRIDES[name].items():
                    body = body.replace(old, new)
            dest = tdir / (name + ".svg")
            dest.write_text(body, encoding="utf-8", newline="\n")
            written += 1
            print("wrote", dest.relative_to(ROOT))
    print("total:", written)


if __name__ == "__main__":
    main()
