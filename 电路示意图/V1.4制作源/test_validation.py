"""Verify that a missing jumper, solder short and hidden connector are rejected."""
from pathlib import Path
import contextlib, copy, io, json, sys

root=Path(__file__).parent
source=(root/'make_artifacts.py').read_text(encoding='utf-8')
data=json.loads((root/'layout_V1.4.json').read_text(encoding='utf-8'))
sys.path.insert(0,str(root))
load_line="D=json.loads((ROOT/'layout_V1.4.json').read_text(encoding='utf-8'))"

def check(model,should_pass,code=source):
    code=code.replace(load_line,'D='+repr(model))
    sys.argv=[str(root/'make_artifacts.py'),'--check-only']
    result=False
    # stdout.reconfigure belongs to the production CLI, not captured test output.
    code=code.replace('sys.stdout.reconfigure(encoding="utf-8")','')
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            exec(compile(code,str(root/'make_artifacts.py'),'exec'),
                 {'__file__':str(root/'make_artifacts.py'),'__name__':'__main__'})
    except SystemExit as exc:
        result=exc.code==0
    except AssertionError:
        result=False
    assert result==should_pass,'validator accepted invalid construction or rejected baseline'

check(data,True)
missing=copy.deepcopy(data)
missing['routes']=[r for r in missing['routes'] if not(r['side']=='top' and r['points'][0]==[10,6])]
check(missing,False)
short=copy.deepcopy(data)
short['routes'].append(dict(net='TP2',stage=2,side='bottom',points=[[8,3],[9,3]]))
check(short,False)
hidden=copy.deepcopy(data)
next(p for p in hidden['ports'] if p['ref']=='S-')['pad']=[20,7]
check(hidden,False)
# Inverting the short-check condition must fail the valid baseline.
mutation=source.replace("assert bottom.get(p,r['net'])==r['net']", "assert bottom.get(p,r['net'])!=r['net']",1)
assert mutation!=source
check(data,False,mutation)
print('PASS: baseline, missing jumper, solder short, hidden port, validator mutation')
