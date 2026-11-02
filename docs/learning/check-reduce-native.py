"""把同一批固定片段的模型 AST 与当前 C++ parse() 的 AST 逐节点对照。"""
from pathlib import Path
import importlib.util
import json
import subprocess

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('reduce_model', here / 'build-reduce-actions.py')
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)


def tree(graph, nid):
    n = graph[nid]
    return dict(kind=n['kind'], name=n['name'], children=[tree(graph, c) for c in n['children']])


for index, (title, source) in enumerate(model.sources):
    source_file = here / f'reduce-native-{index}.c'
    source_file.write_text(source, 'utf-8')
    actual = json.loads(subprocess.check_output([str(here / 'verify-reduce-native.exe'), str(source_file)], encoding='utf-8'))
    scene = model.Simulator(source, title).run()[-1]
    expected = tree(scene['steps'][-1]['graph'], scene['output']['nodes'][0])
    assert actual == expected, title
    print(f'AST match: source {index + 1}')
print('All 4 parser ASTs match the teaching model, including child order and names.')
