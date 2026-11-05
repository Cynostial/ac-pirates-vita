#!/usr/bin/env python3
"""
trp_pack.py - build / inspect unencrypted PS Vita TROPHY.TRP files for homebrew
(used together with Rinnegatamante's NoTrpDrm plugin).

Container layout (big-endian), as implemented by RPCS3 rpcs3/Loader/TRP.h|.cpp and
observed byte-for-byte in working NoTrpDrm homebrew packs (Sonic Mania Vita,
smb2se-vita, raider-vita) produced by withLogic's Vita TRP Builder 0.1:

  0x00 u32  magic           0xDCA24D00
  0x04 u32  version         2   (homebrew packs use 2; RPCS3 checks SHA1 when >= 2)
  0x08 u64  file_size       total size of the .TRP in bytes
  0x10 u32  files_count     number of entries
  0x14 u32  element_size    0x40 (size of one entry record)
  0x18 u32  dev_flag        0
  0x1C u8[20] sha1          SHA1 of the WHOLE file with these 20 bytes zeroed
  0x30 u8[16] padding       zero
  0x40 entry[files_count]:
       char name[32]        NUL padded, e.g. "TROPCONF.SFM"
       u64  offset          absolute offset of the data
       u64  size            data size (unpadded)
       u32  flag            0 (unencrypted)
       u8   pad[12]         zero
  data: first blob right after the entry table, every blob starts on a 16-byte
        boundary, gaps and the file tail are zero-padded to 16 bytes.

Entry order used by the builder / working packs:
  TROPCONF.SFM, TROP.SFM, TROP_xx.SFM (ascending), ICON0.PNG, TROP000.PNG, TROP001.PNG, ...

Usage:
  python trp_pack.py pack   <src_dir> <out TROPHY.TRP> [--commid XXXX00001_00] [--strict]
  python trp_pack.py verify <TROPHY.TRP> [--extract DIR] [--strict]
  python trp_pack.py make-conf <TROP.SFM> <TROPCONF.SFM>
  python trp_pack.py selftest [--keep DIR]
"""

import argparse
import hashlib
import os
import re
import shutil
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib

TRP_MAGIC = 0xDCA24D00
TRP_VERSION = 2
HEADER_SIZE = 0x40
ENTRY_SIZE = 0x40
ALIGN = 16
SIG_PREFIX = "<!--Sce-Np-Trophy-Signature: "
SIG_LINE = SIG_PREFIX + "x" * 320 + "-->"      # exact placeholder used by Vita TRP Builder
MAX_TROPHIES = 128                              # SceNpTrophyFlagArray = uint32[4] (Vita3K MAX_TROPHIES)
NAME_MAX = 128                                  # SCE_NP_TROPHY_NAME_MAX_SIZE (Vita3K)
DETAIL_MAX = 1024                               # SCE_NP_TROPHY_DESCR_MAX_SIZE (Vita3K)
TITLE_MAX = 128                                 # SCE_NP_TROPHY_GAME_TITLE_MAX_SIZE (Vita3K)
ICON0_SIZE = (320, 176)
TROP_ICON_SIZE = (240, 240)
COMMID_RE = re.compile(r"^[A-Z0-9]{4}[0-9]{5}_[0-9]{2}$")
LANG_NAMES = {
    0: "Japanese", 1: "English (US)", 2: "French", 3: "Spanish", 4: "German", 5: "Italian",
    6: "Dutch", 7: "Portuguese (PT)", 8: "Russian", 9: "Korean", 10: "Chinese (Trad.)",
    11: "Chinese (Simp.)", 12: "Finnish", 13: "Swedish", 14: "Danish", 15: "Norwegian",
    16: "Polish", 17: "Portuguese (BR)", 18: "English (GB)", 19: "Turkish",
    20: "Spanish (LatAm)", 21: "French (CA)",
}


class Report:
    def __init__(self, strict=False):
        self.errors, self.warnings, self.notes = [], [], []
        self.strict = strict

    def err(self, m): self.errors.append(m)
    def warn(self, m): self.warnings.append(m)
    def note(self, m): self.notes.append(m)

    def dump(self):
        for m in self.notes: print("  note: " + m)
        for m in self.warnings: print("  WARNING: " + m)
        for m in self.errors: print("  ERROR: " + m)

    @property
    def failed(self):
        return bool(self.errors) or (self.strict and bool(self.warnings))


# ----------------------------------------------------------------------------- helpers

def align_up(v, a=ALIGN):
    return (v + a - 1) & ~(a - 1)


def png_size(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return w, h


def entry_sort_key(name):
    u = name.upper()
    if u == "TROPCONF.SFM": return (0, 0, u)
    if u == "TROP.SFM": return (1, 0, u)
    m = re.match(r"^TROP_(\d+)\.SFM$", u)
    if m: return (2, int(m.group(1)), u)
    if u == "ICON0.PNG": return (3, 0, u)
    m = re.match(r"^ICON0_(\d+)\.PNG$", u)
    if m: return (4, int(m.group(1)), u)
    m = re.match(r"^GR(\d+)\.PNG$", u)
    if m: return (5, int(m.group(1)), u)
    m = re.match(r"^GR(\d+)_(\d+)\.PNG$", u)
    if m: return (6, int(m.group(1)) * 100 + int(m.group(2)), u)
    m = re.match(r"^TROP(\d+)\.PNG$", u)
    if m: return (7, int(m.group(1)), u)
    return (9, 0, u)


KNOWN_NAME_RE = re.compile(
    r"^(TROPCONF\.SFM|TROP\.SFM|TROP_\d{2}\.SFM|ICON0\.PNG|ICON0_\d{2}\.PNG|GR\d{3}\.PNG|GR\d{3}_\d{2}\.PNG|TROP\d{3}\.PNG)$")


def normalize_sfm(raw, name, rep, fix=True):
    """Strip UTF-8 BOM, CRLF->LF, ensure the signature placeholder comment is line 1."""
    data = raw
    if data.startswith(b"\xef\xbb\xbf"):
        if fix:
            data = data[3:]
            rep.note("%s: stripped UTF-8 BOM (working packs have none)" % name)
        else:
            rep.warn("%s: has a UTF-8 BOM (working packs have none)" % name)
    if b"\r\n" in data:
        if fix:
            data = data.replace(b"\r\n", b"\n")
            rep.note("%s: converted CRLF to LF" % name)
        else:
            rep.warn("%s: uses CRLF line endings (working packs use LF)" % name)
    text = data.decode("utf-8")
    first = text.split("\n", 1)[0]
    if not first.startswith(SIG_PREFIX):
        if fix:
            text = SIG_LINE + "\n" + text.lstrip()
            rep.note("%s: prepended the 320-'x' Sce-Np-Trophy-Signature placeholder line" % name)
        else:
            rep.warn("%s: first line is not the '<!--Sce-Np-Trophy-Signature: ...-->' placeholder" % name)
    elif first != SIG_LINE:
        rep.warn("%s: signature comment differs from the Vita TRP Builder placeholder (320 'x')" % name)
    if not text.endswith("\n"):
        if fix:
            text += "\n"
    return text.encode("utf-8")


def parse_sfm(data, name, rep):
    try:
        root = ET.fromstring(data.decode("utf-8"))
    except Exception as e:  # noqa
        rep.err("%s: XML parse error: %s" % (name, e))
        return None
    if root.tag != "trophyconf":
        rep.err("%s: root element is <%s>, expected <trophyconf>" % (name, root.tag))
        return None
    return root


def trophies_of(root):
    out = {}
    for t in root.findall("trophy"):
        out[t.get("id")] = t
    return out


def validate_set(files, rep, expect_commid=None):
    """files: dict NAME(upper) -> bytes. Validates SFM/PNG content consistency."""
    if "TROPCONF.SFM" not in files: rep.err("missing TROPCONF.SFM")
    if "TROP.SFM" not in files: rep.err("missing TROP.SFM (default-language names/details)")
    if "ICON0.PNG" not in files: rep.err("missing ICON0.PNG")
    for n in files:
        if not KNOWN_NAME_RE.match(n):
            rep.warn("unexpected entry name %r (not produced by Vita TRP Builder)" % n)
        if len(n.encode()) >= 32:
            rep.err("entry name %r is too long (max 31 chars)" % n)
    if rep.errors:
        return

    conf = parse_sfm(files["TROPCONF.SFM"], "TROPCONF.SFM", rep)
    if conf is None:
        return
    if conf.get("platform") != "psp2":
        rep.warn("TROPCONF.SFM: platform=%r (Vita packs use 'psp2')" % conf.get("platform"))
    for k, v in (("version", "1.1"), ("policy", "large")):
        if conf.get(k) != v:
            rep.warn("TROPCONF.SFM: %s=%r (working packs use %r)" % (k, conf.get(k), v))
    commid = (conf.findtext("npcommid") or "").strip()
    if not COMMID_RE.match(commid):
        rep.err("TROPCONF.SFM: <npcommid> %r must look like ABCD12345_00" % commid)
    elif commid.startswith("NPWR"):
        rep.warn("npcommid %s uses Sony's NPWR prefix - risk of colliding with a real game's "
                 "trophy set in ur0:user/xx/trophy; homebrew convention is a non-NPWR prefix" % commid)
    if expect_commid and commid != expect_commid:
        rep.err("npcommid %r != expected %r (folder must be sce_sys/trophy/<npcommid>/)" % (commid, expect_commid))
    tsv = (conf.findtext("trophyset-version") or "").strip()
    if not re.match(r"^\d\d\.\d\d$", tsv):
        rep.warn("TROPCONF.SFM: <trophyset-version> %r should look like 01.00" % tsv)
    pl = conf.find("parental-level")
    if pl is None:
        rep.warn("TROPCONF.SFM: no <parental-level license-area=\"default\">0</parental-level>")

    ctro = conf.findall("trophy")
    if not ctro:
        rep.err("TROPCONF.SFM: no <trophy> elements")
        return
    if len(ctro) > MAX_TROPHIES:
        rep.err("%d trophies > %d (SceNpTrophyFlagArray holds 128 bits)" % (len(ctro), MAX_TROPHIES))
    ids = []
    plat = []
    grades = {"B": 0, "S": 0, "G": 0, "P": 0}
    for t in ctro:
        tid = t.get("id", "")
        if not re.match(r"^\d{3}$", tid):
            rep.err("trophy id %r must be 3 digits (000..)" % tid)
            continue
        ids.append(int(tid))
        tt = t.get("ttype")
        if tt not in grades:
            rep.err("trophy %s: ttype=%r must be one of B,S,G,P" % (tid, tt))
        else:
            grades[tt] += 1
        if t.get("hidden") not in ("yes", "no"):
            rep.err("trophy %s: hidden=%r must be yes/no" % (tid, t.get("hidden")))
        if tt == "P":
            plat.append(tid)
    if ids != list(range(len(ids))):
        rep.err("trophy ids must be consecutive from 000 in order; got %s" % ids[:12])
    if len(plat) > 1:
        rep.err("more than one platinum trophy: %s" % plat)
    for t in ctro:
        tid, tt, pid = t.get("id"), t.get("ttype"), t.get("pid")
        if pid is None:
            rep.warn("trophy %s: no pid attribute (working packs always set it)" % tid)
        elif tt == "P" and pid != "-1":
            rep.err("platinum %s must have pid=\"-1\"" % tid)
        elif tt != "P" and plat and pid != plat[0]:
            rep.warn("trophy %s: pid=%r but platinum id is %s" % (tid, pid, plat[0]))
        elif tt != "P" and not plat and pid != "-1":
            rep.warn("trophy %s: pid=%r but set has no platinum (expected -1)" % (tid, pid))
    if plat and plat[0] != "000":
        rep.warn("platinum is %s; all known homebrew packs use 000 for the platinum" % plat[0])
    if not plat:
        rep.note("no platinum trophy in this set")
    pts = grades["B"] * 15 + grades["S"] * 30 + grades["G"] * 90
    rep.note("%d trophies: P=%d G=%d S=%d B=%d (%d points excluding platinum)"
             % (len(ctro), grades["P"], grades["G"], grades["S"], grades["B"], pts))

    # localized files must agree with TROPCONF
    conf_map = trophies_of(conf)
    for n in sorted(files, key=entry_sort_key):
        if not (n == "TROP.SFM" or re.match(r"^TROP_\d{2}\.SFM$", n)):
            continue
        root = parse_sfm(files[n], n, rep)
        if root is None:
            continue
        m = re.match(r"^TROP_(\d{2})\.SFM$", n)
        if m and int(m.group(1)) not in LANG_NAMES:
            rep.warn("%s: unknown language number" % n)
        if (root.findtext("npcommid") or "").strip() != commid:
            rep.err("%s: npcommid differs from TROPCONF.SFM" % n)
        if (root.findtext("trophyset-version") or "").strip() != tsv:
            rep.warn("%s: trophyset-version differs from TROPCONF.SFM" % n)
        title = root.findtext("title-name")
        detail = root.findtext("title-detail")
        if not title:
            rep.err("%s: missing <title-name>" % n)
        elif len(title.encode()) >= TITLE_MAX:
            rep.warn("%s: title-name is %d bytes (API buffer is %d)" % (n, len(title.encode()), TITLE_MAX))
        if not detail:
            rep.err("%s: missing <title-detail>" % n)
        elif len(detail.encode()) >= DETAIL_MAX:
            rep.warn("%s: title-detail is %d bytes (API buffer is %d)" % (n, len(detail.encode()), DETAIL_MAX))
        lmap = trophies_of(root)
        if set(lmap) != set(conf_map):
            rep.err("%s: trophy id set differs from TROPCONF.SFM" % n)
        for tid, t in lmap.items():
            c = conf_map.get(tid)
            if c is not None:
                for a in ("hidden", "ttype", "pid"):
                    if t.get(a) != c.get(a):
                        rep.err("%s trophy %s: %s=%r but TROPCONF has %r" % (n, tid, a, t.get(a), c.get(a)))
            nm, dt = t.findtext("name"), t.findtext("detail")
            if not nm:
                rep.err("%s trophy %s: missing <name>" % (n, tid))
            elif len(nm.encode()) >= NAME_MAX:
                rep.warn("%s trophy %s: name is %d bytes (API buffer is %d)" % (n, tid, len(nm.encode()), NAME_MAX))
            if dt is None:
                rep.err("%s trophy %s: missing <detail>" % (n, tid))
            elif len(dt.encode()) >= DETAIL_MAX:
                rep.warn("%s trophy %s: detail is %d bytes (API buffer is %d)" % (n, tid, len(dt.encode()), DETAIL_MAX))

    # images
    for n, blob in files.items():
        if n.endswith(".PNG"):
            sz = png_size(blob)
            if sz is None:
                rep.err("%s is not a PNG" % n)
            elif n.startswith("ICON0") and sz != ICON0_SIZE:
                rep.warn("%s is %dx%d, expected 320x176" % (n, sz[0], sz[1]))
            elif re.match(r"^TROP\d{3}\.PNG$", n) and sz != TROP_ICON_SIZE:
                rep.warn("%s is %dx%d, expected 240x240" % (n, sz[0], sz[1]))
    for i in ids:
        if "TROP%03d.PNG" % i not in files:
            rep.err("missing icon TROP%03d.PNG for trophy %03d" % (i, i))
    for n in files:
        m = re.match(r"^TROP(\d{3})\.PNG$", n)
        if m and int(m.group(1)) not in ids:
            rep.warn("%s has no matching trophy in TROPCONF.SFM" % n)


# ----------------------------------------------------------------------------- build / parse

def build_trp(files):
    """files: list of (name, bytes) in the desired order. Returns the TRP bytes."""
    count = len(files)
    table_end = HEADER_SIZE + count * ENTRY_SIZE
    offsets = []
    pos = align_up(table_end)
    for _, blob in files:
        offsets.append(pos)
        pos = align_up(pos + len(blob))
    total = pos
    out = bytearray(total)
    struct.pack_into(">IIQIII", out, 0, TRP_MAGIC, TRP_VERSION, total, count, ENTRY_SIZE, 0)
    for i, ((name, blob), off) in enumerate(zip(files, offsets)):
        nb = name.encode("ascii")
        if len(nb) >= 32:
            raise ValueError("entry name too long: " + name)
        e = HEADER_SIZE + i * ENTRY_SIZE
        out[e:e + 32] = nb.ljust(32, b"\0")
        struct.pack_into(">QQI", out, e + 32, off, len(blob), 0)
        out[off:off + len(blob)] = blob
    out[0x1C:0x30] = hashlib.sha1(bytes(out)).digest()   # sha1 field is still zero here
    return bytes(out)


def parse_trp(data, rep):
    """Returns list of (name, offset, size, flag, bytes). Records problems in rep."""
    if len(data) < HEADER_SIZE:
        rep.err("file too small")
        return []
    magic, ver, fsize, count, esize, dev = struct.unpack(">IIQIII", data[:0x1C])
    print("  magic=0x%08X version=%d file_size=%d (actual %d) entries=%d element_size=0x%X dev_flag=%d"
          % (magic, ver, fsize, len(data), count, esize, dev))
    if magic != TRP_MAGIC:
        rep.err("bad magic 0x%08X (expected 0xDCA24D00)" % magic)
        return []
    if ver != TRP_VERSION:
        rep.warn("version %d (homebrew packs use 2)" % ver)
    if fsize != len(data):
        rep.err("header file_size %d != actual %d" % (fsize, len(data)))
    if esize != ENTRY_SIZE:
        rep.err("element_size 0x%X != 0x40" % esize)
        return []
    if dev != 0:
        rep.warn("dev_flag=%d (homebrew packs use 0)" % dev)
    if any(data[0x30:0x40]):
        rep.warn("header padding 0x30..0x3F is not zero")
    z = bytearray(data[:fsize] if fsize <= len(data) else data)
    stored = bytes(z[0x1C:0x30])
    z[0x1C:0x30] = b"\0" * 20
    calc = hashlib.sha1(bytes(z)).digest()
    print("  sha1 stored=%s computed=%s %s" % (stored.hex(), calc.hex(), "OK" if stored == calc else "MISMATCH"))
    if stored != calc:
        rep.err("SHA1 mismatch")
    table_end = HEADER_SIZE + count * ENTRY_SIZE
    if table_end > len(data):
        rep.err("entry table runs past end of file")
        return []
    entries = []
    for i in range(count):
        e = data[HEADER_SIZE + i * ENTRY_SIZE: HEADER_SIZE + (i + 1) * ENTRY_SIZE]
        raw = e[:32]
        name = raw.split(b"\0", 1)[0].decode("ascii", "replace")
        off, size, flag = struct.unpack(">QQI", e[32:52])
        if any(e[52:]):
            rep.warn("entry %d (%s): trailing 12 pad bytes not zero" % (i, name))
        if flag != 0:
            rep.warn("entry %d (%s): flag=%d (non-zero = probably encrypted retail entry)" % (i, name, flag))
        if off % ALIGN:
            rep.warn("entry %d (%s): offset 0x%X not 16-byte aligned" % (i, name, off))
        if off < table_end or off + size > len(data):
            rep.err("entry %d (%s): range 0x%X+0x%X out of bounds" % (i, name, off, size))
            continue
        entries.append((name, off, size, flag, data[off:off + size]))
    spans = sorted((o, o + s, n) for n, o, s, _, _ in entries)
    for (a0, a1, an), (b0, b1, bn) in zip(spans, spans[1:]):
        if b0 < a1:
            rep.err("entries %s and %s overlap" % (an, bn))
    names = [e[0] for e in entries]
    if len(set(names)) != len(names):
        rep.err("duplicate entry names")
    if names != sorted(names, key=entry_sort_key):
        rep.note("entry order differs from Vita TRP Builder order (CONF, TROP, TROP_xx, ICON0, TROPnnn)")
    return entries


# ----------------------------------------------------------------------------- commands

def collect_dir(src, rep):
    files = {}
    for fn in sorted(os.listdir(src)):
        p = os.path.join(src, fn)
        if not os.path.isfile(p):
            continue
        u = fn.upper()
        if not (u.endswith(".SFM") or u.endswith(".PNG")):
            rep.note("ignoring %s" % fn)
            continue
        if u in files:
            rep.err("duplicate file (case-insensitive): %s" % fn)
        with open(p, "rb") as f:
            files[u] = f.read()
    return files


def cmd_pack(a):
    rep = Report(a.strict)
    files = collect_dir(a.src, rep)
    for n in list(files):
        if n.endswith(".SFM"):
            files[n] = normalize_sfm(files[n], n, rep, fix=not a.no_fix)
    validate_set(files, rep, a.commid)
    print("pack %s -> %s" % (a.src, a.out))
    rep.dump()
    if rep.failed:
        print("FAILED (nothing written)")
        return 1
    ordered = [(n, files[n]) for n in sorted(files, key=entry_sort_key)]
    trp = build_trp(ordered)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(trp)
    print("wrote %d bytes, %d entries" % (len(trp), len(ordered)))
    conf = ET.fromstring(files["TROPCONF.SFM"].decode("utf-8"))
    cid = conf.findtext("npcommid").strip()
    print("install it in the VPK as: sce_sys/trophy/%s/TROPHY.TRP  (comm id data='%s', num=%d)"
          % (cid, cid[:9], int(cid[10:])))
    return 0


def cmd_verify(a):
    rep = Report(a.strict)
    with open(a.trp, "rb") as f:
        data = f.read()
    print("verify %s" % a.trp)
    entries = parse_trp(data, rep)
    for i, (name, off, size, flag, blob) in enumerate(entries):
        extra = ""
        sz = png_size(blob)
        if sz:
            extra = " PNG %dx%d" % sz
        elif name.upper().endswith(".SFM"):
            extra = " XML" if blob.startswith(b"<") or blob.startswith(b"\xef\xbb\xbf<") else " (not plain XML - encrypted?)"
        print("  %2d %-14s off=0x%08X size=%8d flag=%d%s" % (i, name, off, size, flag, extra))
    files = {e[0].upper(): e[4] for e in entries}
    if entries and all(b.startswith(b"<") for n, b in files.items() if n.endswith(".SFM")):
        for n in files:
            if n.endswith(".SFM"):
                normalize_sfm(files[n], n, rep, fix=False)
        validate_set(files, rep)
    if a.extract:
        os.makedirs(a.extract, exist_ok=True)
        for name, _, _, _, blob in entries:
            safe = os.path.basename(name)
            if not safe or safe in (".", ".."):
                rep.err("refusing to extract entry name %r" % name)
                continue
            with open(os.path.join(a.extract, safe), "wb") as f:
                f.write(blob)
        print("  extracted %d entries to %s" % (len(entries), a.extract))
    rep.dump()
    print("RESULT: %s" % ("FAIL" if rep.failed else "OK"))
    return 1 if rep.failed else 0


def make_conf_bytes(trop_sfm_bytes):
    """Derive TROPCONF.SFM (no names/details/title) from a TROP.SFM, Vita TRP Builder style."""
    rep = Report()
    data = normalize_sfm(trop_sfm_bytes, "TROP.SFM", rep, fix=True)
    root = ET.fromstring(data.decode("utf-8"))
    lines = [SIG_LINE,
             '<trophyconf version="%s" platform="%s" policy="%s">'
             % (root.get("version", "1.1"), root.get("platform", "psp2"), root.get("policy", "large")),
             " <npcommid>%s</npcommid>" % root.findtext("npcommid").strip(),
             " <trophyset-version>%s</trophyset-version>" % root.findtext("trophyset-version").strip()]
    pl = root.find("parental-level")
    lines.append(' <parental-level license-area="%s">%s</parental-level>'
                 % ((pl.get("license-area", "default"), (pl.text or "0").strip()) if pl is not None else ("default", "0")))
    for t in root.findall("trophy"):
        lines.append(' <trophy id="%s" hidden="%s" ttype="%s" pid="%s"/>'
                     % (t.get("id"), t.get("hidden"), t.get("ttype"), t.get("pid")))
    lines.append("</trophyconf>")
    return ("\n".join(lines) + "\n").encode("utf-8")


def cmd_make_conf(a):
    with open(a.trop_sfm, "rb") as f:
        out = make_conf_bytes(f.read())
    with open(a.out, "wb") as f:
        f.write(out)
    print("wrote %s (%d bytes)" % (a.out, len(out)))
    return 0


# ----------------------------------------------------------------------------- self test

def make_png(w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb) * w for _ in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def make_trop_sfm(commid, title, detail, trophies):
    lines = [SIG_LINE, '<trophyconf version="1.1" platform="psp2" policy="large">',
             " <npcommid>%s</npcommid>" % commid, " <trophyset-version>01.00</trophyset-version>",
             ' <parental-level license-area="default">0</parental-level>',
             " <title-name>%s</title-name>" % esc(title), " <title-detail>%s</title-detail>" % esc(detail)]
    for tid, hidden, tt, pid, name, det in trophies:
        lines += [' <trophy id="%03d" hidden="%s" ttype="%s" pid="%s">' % (tid, hidden, tt, pid),
                  "  <name>%s</name>" % esc(name), "  <detail>%s</detail>" % esc(det), " </trophy>"]
    lines.append("</trophyconf>")
    return ("\n".join(lines) + "\n").encode("utf-8")


def cmd_selftest(a):
    base = a.keep or tempfile.mkdtemp(prefix="trp_selftest_")
    src = os.path.join(base, "src")
    if os.path.isdir(src):
        shutil.rmtree(src)
    os.makedirs(src)
    commid = "TEST00001_00"
    tro = [(0, "no", "P", "-1", "Platinum", "Unlock every other trophy")]
    grades = "BSGBSGBB"
    for i, g in enumerate(grades, 1):
        tro.append((i, "yes" if i == 3 else "no", g, "000", "Trophy %d <&>" % i, "Do thing number %d" % i))
    with open(os.path.join(src, "TROP.SFM"), "wb") as f:
        f.write(make_trop_sfm(commid, "Self Test Game", "A synthetic trophy set", tro))
    fr = [(t[0], t[1], t[2], t[3], "Trophée %d" % t[0], "Détail %d" % t[0]) for t in tro]
    with open(os.path.join(src, "TROP_02.SFM"), "wb") as f:
        f.write(make_trop_sfm(commid, "Jeu de test", "Un ensemble synthétique", fr))
    with open(os.path.join(src, "TROPCONF.SFM"), "wb") as f:
        f.write(make_conf_bytes(open(os.path.join(src, "TROP.SFM"), "rb").read()))
    with open(os.path.join(src, "ICON0.PNG"), "wb") as f:
        f.write(make_png(320, 176, (10, 20, 200)))
    for t in tro:
        with open(os.path.join(src, "TROP%03d.PNG" % t[0]), "wb") as f:
            f.write(make_png(240, 240, ((t[0] * 37) & 255, 128, 64)))
    out = os.path.join(base, "out", commid, "TROPHY.TRP")
    ok = True
    print("== selftest: pack")
    ok &= cmd_pack(argparse.Namespace(src=src, out=out, commid=commid, strict=True, no_fix=False)) == 0
    print("== selftest: verify + extract")
    ext = os.path.join(base, "extracted")
    if os.path.isdir(ext):
        shutil.rmtree(ext)
    ok &= cmd_verify(argparse.Namespace(trp=out, extract=ext, strict=True)) == 0
    print("== selftest: compare extracted files with sources")
    for fn in sorted(os.listdir(src)):
        same = open(os.path.join(src, fn), "rb").read() == open(os.path.join(ext, fn), "rb").read()
        ok &= same
        if not same:
            print("  MISMATCH " + fn)
    print("  %d files compared" % len(os.listdir(src)))
    print("== selftest: repack extracted -> must be byte-identical")
    out2 = os.path.join(base, "out2", "TROPHY.TRP")
    ok &= cmd_pack(argparse.Namespace(src=ext, out=out2, commid=commid, strict=True, no_fix=False)) == 0
    same = open(out, "rb").read() == open(out2, "rb").read()
    print("  byte-identical: %s" % same)
    ok &= same
    print("== selftest: corruption must be detected")
    bad = bytearray(open(out, "rb").read())
    bad[-20] ^= 0xFF
    badp = os.path.join(base, "bad.TRP")
    open(badp, "wb").write(bytes(bad))
    detected = cmd_verify(argparse.Namespace(trp=badp, extract=None, strict=False)) != 0
    print("  corruption detected: %s" % detected)
    ok &= detected
    print("SELFTEST %s (work dir: %s)" % ("PASSED" if ok else "FAILED", base))
    if not a.keep:
        shutil.rmtree(base, ignore_errors=True)
    return 0 if ok else 1


def main(argv=None):
    p = argparse.ArgumentParser(description="Build / verify unencrypted PS Vita TROPHY.TRP files")
    sp = p.add_subparsers(dest="cmd", required=True)
    pk = sp.add_parser("pack", help="pack a folder into TROPHY.TRP")
    pk.add_argument("src")
    pk.add_argument("out")
    pk.add_argument("--commid", help="expected npcommid, e.g. ABCD00001_00")
    pk.add_argument("--strict", action="store_true", help="treat warnings as errors")
    pk.add_argument("--no-fix", action="store_true",
                    help="do not auto-fix SFMs (BOM, CRLF, signature placeholder line)")
    vf = sp.add_parser("verify", help="parse/list/check a TROPHY.TRP")
    vf.add_argument("trp")
    vf.add_argument("--extract", metavar="DIR")
    vf.add_argument("--strict", action="store_true")
    mc = sp.add_parser("make-conf", help="derive TROPCONF.SFM from TROP.SFM")
    mc.add_argument("trop_sfm")
    mc.add_argument("out")
    st = sp.add_parser("selftest", help="synthetic round-trip test")
    st.add_argument("--keep", metavar="DIR", help="keep test files in DIR")
    a = p.parse_args(argv)
    return {"pack": cmd_pack, "verify": cmd_verify, "make-conf": cmd_make_conf, "selftest": cmd_selftest}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
