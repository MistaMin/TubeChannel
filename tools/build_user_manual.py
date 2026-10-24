from pathlib import Path
import re
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Image, Table,
                                TableStyle, PageBreak, KeepTogether, Flowable)

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'docs/USER_MANUAL.md'
OUTPUT = ROOT / 'output/pdf/TubeChannel-User-Manual.pdf'
PAGE_W, PAGE_H = A4
LEFT = RIGHT = 17 * mm
TOP = 18 * mm
BOTTOM = 17 * mm
CONTENT_W = PAGE_W - LEFT - RIGHT

NAVY = colors.HexColor('#112235')
INK = colors.HexColor('#172536')
MUTED = colors.HexColor('#516276')
CYAN = colors.HexColor('#16A6C7')
PALE = colors.HexColor('#EAF3F7')
GRID = colors.HexColor('#D6E0E7')

styles = getSampleStyleSheet()
styles.add(ParagraphStyle(name='TitleCustom', parent=styles['Title'], fontName='Helvetica-Bold',
                          fontSize=29, leading=34, textColor=NAVY, alignment=TA_LEFT, spaceAfter=7*mm))
styles.add(ParagraphStyle(name='SubtitleCustom', parent=styles['Normal'], fontName='Helvetica',
                          fontSize=12, leading=17, textColor=MUTED, spaceAfter=5*mm))
styles.add(ParagraphStyle(name='H1Custom', parent=styles['Heading1'], fontName='Helvetica-Bold',
                          fontSize=18, leading=22, textColor=NAVY, spaceBefore=7*mm, spaceAfter=3*mm,
                          keepWithNext=True))
styles.add(ParagraphStyle(name='H2Custom', parent=styles['Heading2'], fontName='Helvetica-Bold',
                          fontSize=12.5, leading=16, textColor=CYAN, spaceBefore=4*mm, spaceAfter=2*mm,
                          keepWithNext=True))
styles.add(ParagraphStyle(name='BodyCustom', parent=styles['BodyText'], fontName='Helvetica',
                          fontSize=9.2, leading=13.2, textColor=INK, spaceAfter=2.4*mm))
styles.add(ParagraphStyle(name='SmallCustom', parent=styles['BodyText'], fontName='Helvetica',
                          fontSize=8, leading=10, textColor=MUTED))
styles.add(ParagraphStyle(name='TableHeadCustom', parent=styles['BodyText'], fontName='Helvetica-Bold',
                          fontSize=8.2, leading=10, textColor=colors.white))
styles.add(ParagraphStyle(name='TableCellCustom', parent=styles['BodyText'], fontName='Helvetica',
                          fontSize=7.8, leading=10.4, textColor=INK))
styles.add(ParagraphStyle(name='BulletCustom', parent=styles['BodyText'], fontName='Helvetica',
                          fontSize=9, leading=12.5, textColor=INK, leftIndent=12, firstLineIndent=-8,
                          spaceAfter=1.8*mm))


def normalize(text):
    return (text.replace('\u2010', '-').replace('\u2011', '-').replace('\u2012', '-')
            .replace('\u2013', '-').replace('\u2014', '-').replace('\u2212', '-')
            .replace('\u00d7', 'x').replace('\u00b7', '|').replace('\u00a0', ' '))


def inline_markup(text):
    text = normalize(text.strip())
    # Build safe XML markup while supporting the manual's bold, italic, code and links.
    token = re.compile(r'(\*\*.+?\*\*|\*.+?\*|`.+?`|\[[^\]]+\]\([^)]+\))')
    out = []
    pos = 0
    for match in token.finditer(text):
        out.append(escape(text[pos:match.start()]))
        value = match.group(0)
        if value.startswith('**'):
            out.append('<b>' + escape(value[2:-2]) + '</b>')
        elif value.startswith('*'):
            out.append('<i>' + escape(value[1:-1]) + '</i>')
        elif value.startswith('`'):
            out.append('<font name="Courier">' + escape(value[1:-1]) + '</font>')
        else:
            label, url = re.match(r'\[([^\]]+)\]\(([^)]+)\)', value).groups()
            out.append('<link href="' + escape(url, {'"': '&quot;'}) + '" color="#087F9F"><u>' + escape(label) + '</u></link>')
        pos = match.end()
    out.append(escape(text[pos:]))
    return ''.join(out)


class SignalDiagram(Flowable):
    def __init__(self, width=CONTENT_W, height=143):
        super().__init__()
        self.width, self.height = width, height

    def draw(self):
        c = self.canv
        w, h = self.width, self.height
        c.setFillColor(NAVY); c.roundRect(0, 0, w, h, 9, stroke=0, fill=1)
        c.setFillColor(colors.HexColor('#72D9F0')); c.setFont('Helvetica-Bold', 7.2)
        c.drawString(12, h-17, 'AUDIO PATH')
        labels = [('Input', 'Mic / Inst'), ('Preamp', 'gain / cut / load'),
                  ('Tube', 'drive / warmth'), ('Compressor', 'audio path'),
                  ('Equalizer', 'four bands'), ('Output', 'level / meter'), ('Host', 'out')]
        gap = 5
        bx = 12
        bw = (w - 24 - gap*6)/7
        by = h-61
        bh = 32
        for i, (title, sub) in enumerate(labels):
            x = bx + i*(bw+gap)
            c.setFillColor(colors.HexColor('#1B3B54'))
            c.setStrokeColor(colors.HexColor('#3D6983'))
            c.roundRect(x, by, bw, bh, 5, stroke=1, fill=1)
            c.setFillColor(colors.HexColor('#EAF4FF'))
            c.setFont('Helvetica-Bold', 7.2); c.drawCentredString(x+bw/2, by+19, title)
            c.setFillColor(colors.HexColor('#B7CADC'))
            c.setFont('Helvetica', 5.3); c.drawCentredString(x+bw/2, by+8, sub)
            if i < len(labels)-1:
                c.setStrokeColor(colors.HexColor('#50C8ED')); c.setLineWidth(1.4)
                c.line(x+bw+0.5, by+bh/2, x+bw+gap-1, by+bh/2)
                c.line(x+bw+gap-3, by+bh/2+2, x+bw+gap-1, by+bh/2)
                c.line(x+bw+gap-3, by+bh/2-2, x+bw+gap-1, by+bh/2)
        c.setFillColor(colors.HexColor('#72D9F0')); c.setFont('Helvetica-Bold', 7.2)
        c.drawString(12, h-80, 'CONTROL AND OPTIONAL SUPPLY PATH')
        # Detector control block.
        dx, dy, dw, dh = w*.30, 35, w*.42, 23
        c.setFillColor(colors.HexColor('#352B22')); c.setStrokeColor(colors.HexColor('#AB7D43'))
        c.roundRect(dx, dy, dw, dh, 5, stroke=1, fill=1)
        c.setFillColor(colors.HexColor('#FFF1D8')); c.setFont('Helvetica-Bold', 7)
        c.drawCentredString(dx+dw/2, dy+14, 'Compressor detector controls gain reduction')
        c.setFont('Helvetica', 5.6); c.drawCentredString(dx+dw/2, dy+5, 'Internal or external sidechain')
        # Short visual link from compressor to detector and back.
        comp_x = bx + 3*(bw+gap) + bw/2
        c.setStrokeColor(colors.HexColor('#F1A94F')); c.setLineWidth(1)
        c.line(comp_x, by-1, comp_x, dy+dh+1)
        c.line(comp_x, dy+dh+1, dx+dw/2, dy+dh+1)
        # Supply feedback annotation.
        c.setFillColor(colors.HexColor('#B7CADC')); c.setFont('Helvetica', 5.8)
        c.drawString(12, 12, 'PSU SIM (off by default) models changing rails that can affect circuit stages.')


def image_flow(path, alt):
    full = ROOT / 'docs' / path
    if not full.exists():
        raise FileNotFoundError(full)
    img = Image(str(full))
    max_w, max_h = CONTENT_W, 5.5*inch
    scale = min(max_w/img.imageWidth, max_h/img.imageHeight, 1.0)
    img.drawWidth = img.imageWidth*scale
    img.drawHeight = img.imageHeight*scale
    img.hAlign = 'CENTER'
    caption = Paragraph(inline_markup(alt), styles['SmallCustom'])
    caption.style.alignment = TA_CENTER
    return KeepTogether([Spacer(1, 1.2*mm), img, Spacer(1, 1*mm), caption, Spacer(1, 2.5*mm)])


from reportlab.lib.units import inch

text = SOURCE.read_text(encoding='utf-8')
lines = text.splitlines()
story = []
story.append(Paragraph('Tube Channel User Guide', styles['TitleCustom']))
story.append(Paragraph('Quick reference for the channel strip, its controls, operating modes, and signal path.', styles['SubtitleCustom']))
story.append(Paragraph('<b>Release 1.2.3</b>  |  VST3, Audio Unit, CLAP, LV2, AAX, and standalone formats where supported.', styles['BodyCustom']))
cover_path = ROOT/'docs/ui-digital.png'
from PIL import Image as PILImage
with PILImage.open(cover_path) as cover_source:
    cover_ratio = cover_source.height / cover_source.width
cover = Image(str(cover_path), width=CONTENT_W, height=CONTENT_W*cover_ratio)
cover.hAlign = 'CENTER'
story.append(Spacer(1, 5*mm)); story.append(cover)
story.append(Spacer(1, 5*mm))
story.append(Paragraph('Tube Channel combines an input and preamp, compressor, four-band equalizer, and output section. The digital and analogue-style layouts share the same controls and audio processing. Use the System page for quality, oversampling, stereo, level alignment, and circuit settings.', styles['BodyCustom']))
story.append(PageBreak())

# Start at the first section after the guide title and introduction.
i = 0
while i < len(lines) and not lines[i].startswith('## Main page'):
    i += 1
while i < len(lines):
    raw = lines[i].strip()
    if not raw:
        i += 1; continue
    if raw.startswith('# '):
        i += 1; continue
    if raw.startswith('## '):
        story.append(Paragraph(inline_markup(raw[3:]), styles['H1Custom']))
        i += 1; continue
    if raw.startswith('### '):
        story.append(Paragraph(inline_markup(raw[4:]), styles['H2Custom']))
        i += 1; continue
    image_match = re.match(r'!\[([^]]*)\]\(([^)]+)\)', raw)
    if image_match:
        alt, filename = image_match.groups()
        if filename.lower().endswith('.svg'):
            story.append(Spacer(1, 2*mm)); story.append(SignalDiagram()); story.append(Spacer(1, 2*mm))
            story.append(Paragraph(inline_markup(alt), styles['SmallCustom']))
        else:
            story.append(image_flow(filename, alt))
        i += 1; continue
    if raw.startswith('|'):
        rows = []
        while i < len(lines) and lines[i].strip().startswith('|'):
            row = lines[i].strip().strip('|')
            cells = [cell.strip() for cell in row.split('|')]
            if not all(re.fullmatch(r':?-{3,}:?', cell.replace(' ', '')) for cell in cells):
                rows.append(cells)
            i += 1
        if rows:
            ncols = max(len(r) for r in rows)
            data = []
            for ridx, row in enumerate(rows):
                row += ['']*(ncols-len(row))
                style = styles['TableHeadCustom'] if ridx == 0 else styles['TableCellCustom']
                data.append([Paragraph(inline_markup(cell), style) for cell in row])
            if ncols == 2:
                widths = [CONTENT_W*0.30, CONTENT_W*0.70]
            else:
                widths = [CONTENT_W/ncols]*ncols
            table = Table(data, colWidths=widths, repeatRows=1, hAlign='LEFT')
            table.setStyle(TableStyle([
                ('BACKGROUND',(0,0),(-1,0),NAVY),
                ('BACKGROUND',(0,1),(-1,-1),colors.white),
                ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,PALE]),
                ('GRID',(0,0),(-1,-1),0.35,GRID),
                ('VALIGN',(0,0),(-1,-1),'TOP'),
                ('LEFTPADDING',(0,0),(-1,-1),5),('RIGHTPADDING',(0,0),(-1,-1),5),
                ('TOPPADDING',(0,0),(-1,-1),4),('BOTTOMPADDING',(0,0),(-1,-1),4),
            ]))
            story.append(table); story.append(Spacer(1, 3*mm))
        continue
    if raw.startswith('- '):
        story.append(Paragraph('&bull; ' + inline_markup(raw[2:]), styles['BulletCustom']))
        i += 1; continue
    # Merge consecutive ordinary lines into a paragraph.
    para = [raw]
    i += 1
    while i < len(lines):
        nxt = lines[i].strip()
        if not nxt or nxt.startswith(('#', '|', '![', '- ')):
            break
        para.append(nxt); i += 1
    story.append(Paragraph(inline_markup(' '.join(para)), styles['BodyCustom']))


def page_decor(canvas, doc):
    canvas.saveState()
    if doc.page > 1:
        y = PAGE_H - 10*mm
        canvas.setStrokeColor(GRID); canvas.setLineWidth(0.5)
        canvas.line(LEFT, y-2*mm, PAGE_W-RIGHT, y-2*mm)
        canvas.setFont('Helvetica-Bold', 7.2); canvas.setFillColor(NAVY)
        canvas.drawString(LEFT, y, 'TUBE CHANNEL  |  USER GUIDE')
        canvas.setFont('Helvetica', 7.2); canvas.setFillColor(MUTED)
        canvas.drawRightString(PAGE_W-RIGHT, y, 'Release 1.2.3')
    canvas.setStrokeColor(GRID); canvas.setLineWidth(0.4)
    canvas.line(LEFT, 10*mm, PAGE_W-RIGHT, 10*mm)
    canvas.setFont('Helvetica', 7); canvas.setFillColor(MUTED)
    canvas.drawString(LEFT, 6.5*mm, 'Tube Channel  |  User Guide')
    canvas.drawRightString(PAGE_W-RIGHT, 6.5*mm, f'{doc.page}')
    canvas.restoreState()


doc = SimpleDocTemplate(str(OUTPUT), pagesize=A4, rightMargin=RIGHT, leftMargin=LEFT,
                        topMargin=TOP, bottomMargin=BOTTOM,
                        title='Tube Channel User Guide', author='MistaMin',
                        subject='Tube Channel 1.2.3 controls, modes, and signal flow')
doc.build(story, onFirstPage=page_decor, onLaterPages=page_decor)
print(OUTPUT)
print(f'pages={doc.page}')
