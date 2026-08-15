import re
from pathlib import Path

cfg = Path('src/Config.h').resolve()
out = Path('wiring.pdf').resolve()
text_lines = []
if not cfg.exists():
    print('Config.h not found:', cfg)
    raise SystemExit(1)

s = cfg.read_text(encoding='utf-8')
# capture defines
defines = dict(re.findall(r"#define\s+(\w+)\s+(\d+)", s))
# keys we want
keys = ['FRONT_IN1','FRONT_IN2','FRONT_ENA','MIDDLE_IN1','MIDDLE_IN2','MIDDLE_ENA','REAR_IN1','REAR_IN2','REAR_ENA','STEER_IN1','STEER_IN2','STEER_ENA','STATUS_LED','LED_STRIP_PIN']
for k in keys:
    v = defines.get(k,'-')
    text_lines.append(f"{k}: {v}")

# create PDF bytes
lines = ['Wiring Table', ''] + text_lines
# build content stream with simple PDF text
content = 'BT\n/F1 12 Tf\n50 760 Td\n'
for i, line in enumerate(lines):
    safe = line.replace('(','\\(').replace(')','\\)')
    if i == 0:
        content += f'({safe}) Tj\n'
    else:
        content += f'0 -14 Td ({safe}) Tj\n'
content += 'ET\n'
content_bytes = content.encode('latin1')

# assemble PDF objects
objs = []
# obj1: catalog
objs.append(b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n")
# obj2: pages
objs.append(b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n")
# obj3: page
objs.append(b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>\nendobj\n")
# obj4: font
objs.append(b"4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n")
# obj5: contents
stream = content_bytes
objs.append(b"5 0 obj\n<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream\nendobj\n")

# write file with xref
with open(out, 'wb') as f:
    f.write(b"%PDF-1.1\n")
    offsets = []
    for o in objs:
        offsets.append(f.tell())
        f.write(o)
    xref_pos = f.tell()
    # xref
    f.write(b"xref\n0 %d\n" % (len(objs)+1))
    f.write(b"0000000000 65535 f \n")
    for off in offsets:
        f.write(b"%010d 00000 n \n" % off)
    # trailer
    f.write(b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs)+1, xref_pos))
print('Wrote', out)
