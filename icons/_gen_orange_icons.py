#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Redraw the node level icons as unmistakable ORANGES instead of cherries.

Why this is a shape problem, not a colour problem: the node tree recolours the
same icon for each depth level (orange / blue / green / purple / red / ...), so
the "orange" cue cannot come from being orange. It has to come from silhouette.

What makes a citrus read as citrus at 16x16:
  * a slightly squat body (cherries are small spheres, oranges are broader)
  * a NAVEL - the little dimpled crown at the top. This is the strongest citrus
    signal and cherries never have it.
  * peel pores, a few small speckles on the skin
  * a stubby stem (cherries have a long thin one) with one leaf

The previous files drew a plain circle + leaf, which reads as a cherry.
"""
import colorsys
import io
import os

# this helper lives inside icons/ itself, so ICONS *is* the script directory
ICONS = os.path.dirname(os.path.abspath(__file__))

# base colour per level file (unchanged: each level keeps its own hue)
LEVELS = {
    "cherry_black":       "#37474f",
    "cherry_blue":        "#1e88e5",
    "cherry_cyan":        "#00897b",
    "cherry_green":       "#43a047",
    "cherry_grey":        "#78909c",
    "cherry_orange":      "#fb8c00",
    "cherry_orange_dark": "#ef6c00",
    "cherry_purple":      "#8e24aa",
    "cherry_red":         "#e53935",
    "cherry_sherbert":    "#ec407a",
    "cherry_yellow":      "#fdd835",
}

STEM = "#7a4a12"
LEAF = "#8bc34a"


def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def rgb_to_hex(r, g, b):
    return "#%02x%02x%02x" % (max(0, min(255, int(round(r)))),
                              max(0, min(255, int(round(g)))),
                              max(0, min(255, int(round(b)))))


def shift(hex_colour, lightness_delta, saturation_delta=0.0):
    """Darken/lighten in HLS space, keeping the hue recognisable."""
    r, g, b = [v / 255.0 for v in hex_to_rgb(hex_colour)]
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    l = max(0.0, min(1.0, l + lightness_delta))
    s = max(0.0, min(1.0, s + saturation_delta))
    return rgb_to_hex(*[v * 255 for v in colorsys.hls_to_rgb(h, l, s)])


TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<svg width="16" height="16" viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
  <!-- OrangeArk: node level icon - a citrus fruit, recoloured per depth level.
       Distinguishing marks of an orange rather than a cherry: squat body,
       navel crown at the top, peel pores, short stem. -->
  <path d="M 8.35 4.55 C 8.35 4.55 8.25 3.55 7.75 3.05" stroke="{stem}" stroke-width="0.95" fill="none" stroke-linecap="round"/>
  <path d="M 8.65 4.65 C 9.05 3.6 10.2 3.0 11.65 3.1 C 11.65 4.45 10.7 5.5 9.2 5.6 C 9.0 5.5 8.85 5.1 8.65 4.65 Z" fill="{leaf}"/>
  <ellipse cx="8" cy="9.95" rx="5.25" ry="4.95" fill="{base}" stroke="{dark}" stroke-width="0.9"/>
  <ellipse cx="8" cy="6.25" rx="1.55" ry="1.15" fill="{darker}" opacity="0.5"/>
  <ellipse cx="8" cy="6.25" rx="0.72" ry="0.52" fill="{darker}" opacity="0.75"/>
  <circle cx="5.45" cy="9.0" r="0.46" fill="{darker}" opacity="0.5"/>
  <circle cx="10.5" cy="9.2" r="0.46" fill="{darker}" opacity="0.5"/>
  <circle cx="6.1" cy="12.5" r="0.4" fill="{darker}" opacity="0.45"/>
  <circle cx="10.2" cy="12.2" r="0.4" fill="{darker}" opacity="0.45"/>
  <ellipse cx="6.05" cy="8.2" rx="1.85" ry="1.1" fill="#ffffff" opacity="0.34" transform="rotate(-25 6.05 8.2)"/>
</svg>
"""


def dark_for(base):
    """Pick the outline colour: dark enough to read, not so dark it muddies."""
    r, g, b = hex_to_rgb(base)
    lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
    if lum < 0.28:                      # already dark (black level)
        return shift(base, 0.16, 0.05)
    return shift(base, -0.20, 0.05)


def build(base):
    dark = dark_for(base)
    darker = dark_for(dark)
    return TEMPLATE.format(base=base, dark=dark, darker=darker,
                           stem=STEM, leaf=LEAF)


def main():
    written = []
    for name, base in sorted(LEVELS.items()):
        path = os.path.join(ICONS, name + ".svg")
        if not os.path.exists(path):
            print("!! 跳过不存在的文件:", path)
            continue
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(build(base))
        written.append(name)
    print("已重画 %d 个节点层级图标:" % len(written))
    for n in written:
        print("  ", n + ".svg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
