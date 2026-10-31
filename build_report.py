#!/usr/bin/env python3
"""Dung bao_cao/SE203732_Asgn02_BaoCao.pdf tu BaoCao_Asgn02.md (chen log that qua @@include)."""
import os, re, sys
sys.path[:0] = [os.path.expanduser('~/embedded_lab3/tools/pylib'), os.path.expanduser('~/embedded_lab3/tools/py/usr/lib/python3/dist-packages')]
import markdown, weasyprint
B = os.path.expanduser('~/embedded_asgn2'); D = f'{B}/bao_cao'
src = open(f'{D}/BaoCao_Asgn02.md', encoding='utf-8').read()
def inc(m):
    a = m.group(1).split()
    lines = open(f'{B}/{a[0]}', errors='replace').read().replace('\r', '').rstrip('\n').split('\n')
    if len(a) == 3: lines = lines[int(a[1]):int(a[2])]
    return '```\n' + '\n'.join(lines).replace('```', "'''") + '\n```'
src = re.sub(r'^@@include (.+)$', inc, src, flags=re.M)
html = markdown.markdown(src, extensions=['tables', 'fenced_code'])
css = '''@page{size:A4;margin:16mm 14mm;@bottom-center{content:"SE203732 - ASGN-02 - trang " counter(page) "/" counter(pages);font-size:8pt;color:#666}}
body{font-family:"DejaVu Sans",sans-serif;font-size:9.3pt;line-height:1.5}
h1{font-size:19pt;border-bottom:2px solid #2E75B6;padding-bottom:4px}
h2{font-size:14pt;color:#1F4E79;margin-top:20px;border-bottom:1px solid #9bb7d4;page-break-after:avoid}
h3{font-size:11pt;color:#1F4E79;margin-top:14px;page-break-after:avoid}
table{border-collapse:collapse;width:100%;margin:8px 0;font-size:8.3pt;page-break-inside:auto}
tr{page-break-inside:avoid}
th,td{border:1px solid #999;padding:3px 5px;vertical-align:top}th{background:#DCE9F5}
code{font-family:"DejaVu Sans Mono",monospace;font-size:7.8pt;background:#f1f1f1;word-break:break-all}
pre{background:#f4f4f4;padding:6px;white-space:pre-wrap;word-break:break-all;border:1px solid #ddd;font-size:7.6pt}pre code{background:none}
img{max-width:100%;display:block;margin:8px auto}em{color:#444}'''
out = f'{D}/SE203732_Asgn02_BaoCao.pdf'
weasyprint.HTML(string=f'<html><head><meta charset="utf-8"><style>{css}</style></head><body>{html}</body></html>', base_url=f'{D}/x.md').write_pdf(out)
print(out, os.path.getsize(out))
