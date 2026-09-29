#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ct_fmt-txt-latest.svg — repeat last format.

The format-painter button (ct_fmt-txt-clone.svg) embeds the brush artwork the
user supplied as a PNG. This button kept being drawn from scratch (arrow +
shrunken brush) and looked nothing like its sibling. Reuse the SAME brush
image, slightly smaller and shifted, and add a small crisp repeat arrow so the
pair reads as "the same brush, applied again".
"""
import io
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
CLONE = os.path.join(HERE, "ct_fmt-txt-clone.svg")
TARGET = os.path.join(HERE, "ct_fmt-txt-latest.svg")


def brush_data_uri():
    text = io.open(CLONE, encoding="utf-8").read()
    m = re.search(r'xlink:href="data:image/png;base64,([^"]+)"', text)
    if not m:
        raise SystemExit("clone.svg 里没找到内嵌的刷子 PNG")
    return m.group(1)


TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<!-- OrangeArk: repeat last format - the SAME brush artwork the format painter
     uses (user-supplied original), smaller and shifted, plus a small repeat
     arrow. Previously the brush was redrawn by hand at a different angle and
     the fat orange arrow read as scribble next to the painter button. -->
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 16 16" width="16" height="16">
  <!-- repeat arrow: thin arc, small solid head -->
  <path d="M 1.55 4.35 A 2.75 2.75 0 0 1 6.05 2.5" fill="none" stroke="#fb8c00" stroke-width="1.25" stroke-linecap="round"/>
  <path d="M 6.95 1.15 L 7.0 3.85 L 4.65 2.95 Z" fill="#fb8c00"/>
  <!-- the brush, 85% size, pushed down-right so the arrow owns the top-left -->
  <image x="2.6" y="3.55" width="13.4" height="11.7" preserveAspectRatio="xMidYMid meet" xlink:href="data:image/png;base64,{brush}"/>
</svg>
"""


def main():
    svg = TEMPLATE.format(brush=brush_data_uri())
    with io.open(TARGET, "w", encoding="utf-8", newline="\n") as f:
        f.write(svg)
    print("已重画:", TARGET)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
