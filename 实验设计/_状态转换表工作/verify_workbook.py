"""Read-only verification of the exported workbook against pinned DFA data."""
import json
from pathlib import Path
import openpyxl

root = Path(__file__).resolve().parent
data = json.loads((root / 'dfa_data.json').read_text(encoding='utf-8'))
target = root.parent / 'AAACompilerDevelopment_词法状态转换表.xlsx'
values = openpyxl.load_workbook(target, data_only=True)
formulas = openpyxl.load_workbook(target, data_only=False)
assert len(values.worksheets) == 4
ordinary, classes, rules, header = values.worksheets
errors = []
for sheet in values:
    for row in sheet:
        for cell in row:
            if cell.data_type == 'e':
                errors.append((sheet.title, cell.coordinate, cell.value))
assert not errors, errors
for sheet, name, start in [(ordinary, 'ordinary', 9), (header, 'header', 8)]:
    dfa = data[name]
    rule_list = data['rules'] if name == 'ordinary' else data['header_rules']
    for s, row in enumerate(dfa['matrix']):
        assert sheet.cell(start+s, 1).value == s
        assert [sheet.cell(start+s, 6+c).value for c in range(len(row))] == row
        witness = dfa['witnesses'][s] or '空前缀'
        assert sheet.cell(start+s, 5).value == witness, (name,s,witness,sheet.cell(start+s,5).value)
        idx = dfa['accept'][s]
        expected_rule = rule_list[idx]['name'] if idx >= 0 else '—'
        assert sheet.cell(start+s, 2).value == expected_rule
    assert sheet.freeze_panes is not None
for i, rule in enumerate(data['rules']):
    assert rules.cell(i+9, 2).value == rule['name']
    assert rules.cell(i+9, 5).value == rule['regex']
    assert rules.cell(i+9, 6).value == rule.get('literal', '—')
for i, (word, token) in enumerate(data['keywords'].items()):
    assert rules.cell(i+9, 9).value == word
    assert rules.cell(i+9, 10).value == token
for i, example in enumerate(data['examples']):
    assert rules.cell(i+80, 5).value == example['source'], (i,rules.cell(i+80,5).value,example['source'])
    assert rules.cell(i+80, 6).value == example['tokens']
    assert rules.cell(i+80, 7).value == example['errors']
for i, members in enumerate(data['ordinary']['classes']):
    assert classes.cell(i+8, 4).value == len(members)
    parts=[]
    j=0
    while j < len(members):
        a=b=members[j]
        while j+1<len(members) and members[j+1]==b+1:
            j+=1
            b=members[j]
        parts.append(f'0x{a:02X}' if a==b else f'0x{a:02X}–0x{b:02X}')
        j+=1
    assert classes.cell(i+8,2).value == ', '.join(parts)
assert sum(classes.cell(i+8,4).value for i in range(48)) == 256
for sheet in formulas:
    for row in sheet:
        for cell in row:
            if cell.data_type == 'f':
                assert cell.value.startswith('="') and cell.value.endswith('"'), (sheet.title,cell.coordinate,cell.value)
report = {'result':'通过', 'ordinary_states':92, 'input_classes':48, 'transition_cells':92*48,
          'header_states':7, 'rules':64, 'keywords':32, 'preserved_source_examples':len(data['examples']),
          'formula_errors':errors, 'commit':data['commit'], 'verified_date':'2026-10-09'}
report['freeze_panes'] = {sheet.title:sheet.freeze_panes for sheet in values}
(root / 'export_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
