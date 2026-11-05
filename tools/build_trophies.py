"""Build the PS Vita trophy pack from support/trophies (trophies.tsv and the
icons from make_trophy_icons.py). The VPK carries it as
sce_sys/trophy/ACPR00001_00/TROPHY.TRP; native/trophies.c opens that comm
id. Unsigned, as all homebrew packs: it needs the NoTrpDrm plugin.
  py -3.13 tools/build_trophies.py OUT.TRP"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import trp_pack

COMMID = 'ACPR00001_00'
TITLE = 'AC Pirates'
DETAIL = 'Trophies for the PS Vita port of AC Pirates, from the game\'s own achievements.'

def rows(root):
    out = []
    for line in (root / 'support/trophies/trophies.tsv').read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('#'):
            tid, key, grade, kind, name, detail = line.split('\t')
            out.append((int(tid), key, grade, kind, name, detail))
    assert [r[0] for r in out] == list(range(len(out))), 'trophy ids must run 0..n'
    return out

def build(root, target):
    trophies = [(tid, 'no', grade, '-1' if grade == 'P' else '000', name, detail)
                for tid, _key, grade, _kind, name, detail in rows(root)]
    rep = trp_pack.Report(strict=True)
    trop = trp_pack.normalize_sfm(trp_pack.make_trop_sfm(COMMID, TITLE, DETAIL, trophies), 'TROP.SFM', rep)
    files = {'TROP.SFM': trop, 'TROPCONF.SFM': trp_pack.make_conf_bytes(trop)}
    icons = root / 'support/trophies/icons'
    files['ICON0.PNG'] = (icons / 'ICON0.PNG').read_bytes()
    for tid, *_ in trophies:
        files[f'TROP{tid:03d}.PNG'] = (icons / f'TROP{tid:03d}.PNG').read_bytes()
    trp_pack.validate_set(files, rep, COMMID)
    if rep.failed:
        rep.dump()
        raise SystemExit('trophy pack is invalid')
    data = trp_pack.build_trp([(n, files[n]) for n in sorted(files, key=trp_pack.entry_sort_key)])
    target = Path(target)
    if not target.exists() or target.read_bytes() != data:
        target.write_bytes(data)
    return target

if __name__ == '__main__':
    print('wrote', build(Path(__file__).resolve().parents[1], sys.argv[1]))
