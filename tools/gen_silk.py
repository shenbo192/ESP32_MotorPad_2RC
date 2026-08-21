import json

final = json.load(open('tools/silk_labels.json'))
arr = json.dumps(final, ensure_ascii=False)

js = (
    'const PCB_UUID = "31a230f467289f63";\n'
    'await eda.dmt_EditorControl.openDocument(PCB_UUID);\n'
    'const labels = ' + arr + ';\n'
    'let ok=0, fail=0; const fails=[];\n'
    'for (const L of labels) {\n'
    '  try {\n'
    '    const s = await eda.pcb_PrimitiveString.create(3, L.x, L.y, L.t);\n'
    '    if (!s) { fail++; fails.push(L.t+":null"); continue; }\n'
    '    if (s.setState_FontSize) s.setState_FontSize(26);\n'
    '    if (s.setState_LineWidth) s.setState_LineWidth(4);\n'
    '    if (s.setState_AlignMode) s.setState_AlignMode(L.a);\n'
    '    ok++;\n'
    '  } catch(e){ fail++; fails.push(L.t+":"+e.message); }\n'
    '}\n'
    'try { await eda.pcb_Document.save(); } catch(e){ fails.push("save:"+e.message); }\n'
    'return JSON.stringify({ok, fail, fails});\n'
)

open('tools/code_add_silkscreen.js', 'w').write(js)
print('wrote code_add_silkscreen.js, labels:', len(final))
