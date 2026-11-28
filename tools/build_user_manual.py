#!/usr/bin/env python3
"""Builds output/pdf/Tube-Channel-User-Manual.pdf from docs/USER_MANUAL.md and the pictures beside it.

Needs reportlab and Pillow. The manual's Markdown is the single source: headings,
paragraphs, lists, tables, images and inline code are laid out as they are written.
"""
import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape

from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import Image, KeepTogether, ListFlowable, ListItem, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text().strip()
DOC = ROOT / "docs"
SRC = DOC / "USER_MANUAL.md"
OUT = ROOT / "output/pdf/Tube-Channel-User-Manual.pdf"
WIDTH = A4[0] - 36 * mm
NAVY = colors.HexColor("#101a2c")
ACCENT = colors.HexColor("#1aa7c4")
PANEL = colors.HexColor("#eef4f7")
GREY = colors.HexColor("#5a6876")

base = getSampleStyleSheet()
BODY = ParagraphStyle("body", parent=base["BodyText"], fontName="Helvetica", fontSize=9.5, leading=13.5, spaceAfter=6)
CELL = ParagraphStyle("cell", parent=BODY, fontSize=8.5, leading=11, spaceAfter=0)
HEAD = ParagraphStyle("head", parent=CELL, fontName="Helvetica-Bold", textColor=colors.white)
H1 = ParagraphStyle("h1", parent=BODY, fontName="Helvetica-Bold", fontSize=22, leading=26, textColor=NAVY, spaceAfter=10)
H2 = ParagraphStyle("h2", parent=BODY, fontName="Helvetica-Bold", fontSize=14, leading=18, textColor=NAVY, spaceBefore=12, spaceAfter=6, keepWithNext=1)
H3 = ParagraphStyle("h3", parent=BODY, fontName="Helvetica-Bold", fontSize=11, leading=14, textColor=ACCENT, spaceBefore=8, spaceAfter=4, keepWithNext=1)


def inline(text):
    """Markdown inline markup to reportlab's mini-HTML."""
    out = escape(text)
    out = re.sub(r"`([^`]+)`", r'<font face="Courier" size="8.5">\1</font>', out)
    out = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", out)
    out = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\1", out)
    return out


def picture(path, width):
    w, h = PILImage.open(path).size
    return Image(str(path), width=width, height=width * h / w)


def cell(text, style, width):
    m = re.fullmatch(r"!\[[^\]]*\]\(([^)]+)\)", text.strip())
    if m:
        return picture(DOC / m.group(1), width - 4 * mm)
    return Paragraph(inline(text), style)


def table(rows):
    cols = len(rows[0])
    images = any("![" in c for r in rows for c in r)
    if images:
        widths = [WIDTH / cols] * cols
    else:
        longest = [max(len(r[i]) for r in rows) for i in range(cols)]
        share = [min(max(n, 8), 60) for n in longest]
        widths = [WIDTH * s / sum(share) for s in share]
    data = [[cell(c, HEAD if i == 0 else CELL, widths[j]) for j, c in enumerate(r)] for i, r in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), NAVY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PANEL]),
        ("GRID", (0, 0), (-1, -1), 0.25, colors.HexColor("#c5d0d8")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ]))
    return t


def build():
    lines = SRC.read_text().split("\n")
    story, i = [], 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
        elif line.startswith("#"):
            level = len(line) - len(line.lstrip("#"))
            story.append(Paragraph(inline(line.lstrip("# ")), {1: H1, 2: H2}.get(level, H3)))
            i += 1
        elif line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-+:?", c) for c in cells):
                    rows.append(cells)
                i += 1
            story += [table(rows), Spacer(1, 6)]
        elif line.startswith("!["):
            path = DOC / re.match(r"!\[[^\]]*\]\(([^)]+)\)", line).group(1)
            if path.suffix != ".svg":  # the layout library reads bitmaps only; the diagram is in the online manual
                story += [picture(path, WIDTH), Spacer(1, 6)]
            i += 1
        elif line.startswith("- "):
            items = []
            while i < len(lines) and (lines[i].startswith("- ") or lines[i].startswith("  ")):
                if lines[i].startswith("- "):
                    items.append(lines[i][2:])
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            story.append(ListFlowable([ListItem(Paragraph(inline(t), BODY), leftIndent=12) for t in items],
                                      bulletType="bullet", start="•", leftIndent=12))
        else:
            para = []
            while i < len(lines) and lines[i].strip() and not re.match(r"[#|!-] ?", lines[i][:2]):
                para.append(lines[i].strip())
                i += 1
            if not para:  # a line that starts like markup but is plain text
                para, i = [line.strip()], i + 1
            story.append(Paragraph(inline(" ".join(para)), BODY))

    def footer(canvas, doc):
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(GREY)
        canvas.drawString(18 * mm, 10 * mm, f"Tube Channel {VERSION} User Manual")
        canvas.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    SimpleDocTemplate(str(OUT), pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm,
                      bottomMargin=18 * mm, title="Tube Channel User Manual", author="OpenGrid").build(
        story, onFirstPage=footer, onLaterPages=footer)
    print(OUT.relative_to(ROOT))


if __name__ == "__main__":
    sys.exit(build())
