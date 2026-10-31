#!/usr/bin/env python3
"""
mst.py - decode / encode the client's downloaded MST tables (the .dat files under /mst/).

Format (2.19.6, Android, BraveFrontierJNI.decodeCStringForBase64 on the Java side): the .dat is
base64 text of AES-128-ECB / PKCS#7 over a plain JSON array of rows; the AES key is the table's
8-character "encode key" right-padded with zero bytes to 16 (the same scheme as the GME bodies).
File names are Ver<version>_<file key>[_<part>].dat; <version>, the part count and the row count
(checked by DataMstManager::checkCorruptionsOfMsts) come from deploy/system/version_info.json
(moWQ30GH name, d2RFtP8T version, H6k1LIxC parts, 5kbnkTp0 rows). Both keys per table live in a
string table of libgame.so (DataMstManager::getMstFileNameFromKey); tools/mst_keys.json is the
map recovered from it by trying every candidate key on the served files (`keys` command).

    python tools/mst.py decode F_RESOURCE_MST [-o out.json]      decode the served table (all parts)
    python tools/mst.py decode path/to/Ver24_a4M4dh1i.dat -k 7z5RKwhU
    python tools/mst.py encode F_RESOURCE_MST rows.json --version 3276 --out deploy/overrides/mst
    python tools/mst.py keys --lib path/to/libgame.so            rebuild tools/mst_keys.json
"""
from __future__ import annotations

import argparse
import base64
import json
import math
import re
import sys
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parents[1]
DEPLOY = ROOT / "server" / "deploy"
MST_DIR = DEPLOY / "game_content" / "mst"
KEYS_FILE = Path(__file__).resolve().parent / "mst_keys.json"
VERSION_INFO = DEPLOY / "system" / "version_info.json"


def aes_key(key: str) -> bytes:
    return key.encode()[:16].ljust(16, b"\0")


def decrypt(data: bytes, key: str) -> bytes:
    raw = base64.b64decode(data.strip())
    dec = Cipher(algorithms.AES(aes_key(key)), modes.ECB()).decryptor()
    out = dec.update(raw) + dec.finalize()
    pad = out[-1] if out else 0
    if not (1 <= pad <= 16) or out[-pad:] != bytes([pad]) * pad:
        raise ValueError("bad PKCS#7 padding: wrong key?")
    return out[:-pad]


def encrypt(plain: bytes, key: str) -> bytes:
    pad = 16 - len(plain) % 16
    enc = Cipher(algorithms.AES(aes_key(key)), modes.ECB()).encryptor()
    out = enc.update(plain + bytes([pad]) * pad) + enc.finalize()
    return base64.b64encode(out)


def load_keys() -> dict:
    return json.loads(KEYS_FILE.read_text(encoding="utf-8")) if KEYS_FILE.exists() else {}


def version_rows() -> list[dict]:
    doc = json.loads(VERSION_INFO.read_text(encoding="utf-8"))
    return doc[next(iter(doc))]


def version_of(table: str) -> dict | None:
    return next((r for r in version_rows() if r.get("moWQ30GH") == table), None)


def table_files(table: str, version: int | None = None, mst_dir: Path = MST_DIR) -> list[Path]:
    info = load_keys().get(table)
    if not info:
        sys.exit(f"{table}: not in {KEYS_FILE.name} (run `mst.py keys`)")
    if version is None:
        row = version_of(table)
        if not row:
            sys.exit(f"{table}: not in version_info.json")
        version = int(row["d2RFtP8T"])
    parts = info["parts"]
    if parts <= 1:
        return [mst_dir / f"Ver{version}_{info['file_key']}.dat"]
    return [mst_dir / f"Ver{version}_{info['file_key']}_{i}.dat" for i in range(1, parts + 1)]


def decode_table(table: str, version: int | None = None, mst_dir: Path = MST_DIR) -> list:
    key = load_keys()[table]["enc_key"]
    rows: list = []
    for f in table_files(table, version, mst_dir):
        rows.extend(json.loads(decrypt(f.read_bytes(), key)))
    return rows


def dump_rows(rows: list) -> bytes:
    # the original files are PHP json_encode output: compact, "/" escaped, non-ASCII as \uXXXX
    return json.dumps(rows, separators=(",", ":"), ensure_ascii=True).replace("/", "\\/").encode()


def encode_table(table: str, rows: list, version: int, out_dir: Path) -> list[Path]:
    info = load_keys()[table]
    parts = max(1, info["parts"])
    per = math.ceil(len(rows) / parts)
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for i, f in enumerate(table_files(table, version, out_dir)):
        chunk = rows[i * per:(i + 1) * per]
        f.write_bytes(encrypt(dump_rows(chunk), info["enc_key"]))
        written.append(f)
    return written


def cmd_decode(a):
    p = Path(a.what)
    if p.exists():
        if not a.key:
            sys.exit("-k KEY is needed for a path")
        rows = json.loads(decrypt(p.read_bytes(), a.key))
    else:
        rows = decode_table(a.what, a.version)
    text = json.dumps(rows, indent=1, ensure_ascii=False)
    if a.out:
        Path(a.out).write_text(text + "\n", encoding="utf-8")
        print(f"{len(rows)} rows -> {a.out}")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)


def cmd_encode(a):
    rows = json.loads(Path(a.rows).read_text(encoding="utf-8"))
    files = encode_table(a.table, rows, a.version, Path(a.out))
    row = version_of(a.table)
    if row and int(row["5kbnkTp0"]) != len(rows):
        print(f"note: version_info.json says {row['5kbnkTp0']} rows, encoded {len(rows)}; the client checks that count")
    for f in files:
        print(f"{f} ({f.stat().st_size} bytes)")


def cmd_keys(a):
    """Recover {table: file_key, enc_key} by trying every 8-char string near the known key table of
    libgame.so on the served files: the file key comes from the file whose version matches
    version_info.json, the encode key from the first candidate whose decryption is the table JSON."""
    so = Path(a.lib).read_bytes()
    if so.find(b"hdt2N6mr") < 0:  # F_RESOURCE_MST's file key, inside the key table
        sys.exit("key table not found in that library")
    # every NUL-terminated 8-character alphanumeric string of the library is a candidate (the key
    # table is spread over a wide stretch of .rodata)
    cands = sorted(set(s.decode() for s in re.findall(rb"(?<=\x00)[A-Za-z0-9]{8}(?=\x00)", so)))
    files: dict[tuple[int, str], list[Path]] = {}
    for d in (MST_DIR, DEPLOY / "overrides" / "mst"):
        for f in d.glob("*.dat") if d.is_dir() else []:
            m = re.match(r"Ver(\d+)_(\w{8})(?:_(\d+))?\.dat$", f.name)
            if m:
                files.setdefault((int(m.group(1)), m.group(2)), []).append(f)

    def find_key(paths: list[Path], want_rows: int, allow_mismatch: bool) -> tuple[str, int] | None:
        head = base64.b64decode(paths[0].read_bytes()[:88])[:32]
        fallback = None
        for k in cands:
            dec = Cipher(algorithms.AES(aes_key(k)), modes.ECB()).decryptor()
            first = dec.update(head)
            if not first.startswith(b'[{"') or any(c < 32 for c in first):  # UTF-8 text is fine
                continue
            try:
                n = sum(len(json.loads(decrypt(p.read_bytes(), k))) for p in paths)
            except Exception:
                continue
            if n == want_rows:
                return k, n
            fallback = fallback or (k, n)  # decodes, but the row count differs from version_info.json
        return fallback if allow_mismatch else None

    out, skipped = {}, []
    for r in version_rows():
        name, version = r["moWQ30GH"], int(r["d2RFtP8T"])
        want = int(r["5kbnkTp0"])
        hits = [(fk, sorted(fs)) for (fv, fk), fs in files.items() if fv == version]
        strict_only = False
        if not hits and want > 0:
            # version_info.json was bumped past the served files: match by row count alone
            hits, strict_only = [(fk, sorted(fs)) for (fv, fk), fs in files.items()], True
        found = None
        for fk, fs in hits:
            # a row-count mismatch is accepted only when the version number is unique among the
            # served files (then the file is certain, only version_info.json's count is stale)
            hit = find_key(fs, want, allow_mismatch=not strict_only and len(hits) == 1 and want > 0)
            if hit:
                ek, n = hit
                found = {"file_key": fk, "enc_key": ek, "parts": int(r["H6k1LIxC"]), "rows": want}
                if n != want:
                    found["rows_in_files"] = n
                break
        if found:
            out[name] = found
        else:
            skipped.append(name)
    KEYS_FILE.write_text(json.dumps(out, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"{len(out)} tables -> {KEYS_FILE}; not served as files / unmatched: {' '.join(skipped)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("decode"); d.add_argument("what", help="table name (F_RESOURCE_MST) or a .dat path")
    d.add_argument("-k", "--key"); d.add_argument("--version", type=int); d.add_argument("-o", "--out"); d.set_defaults(f=cmd_decode)
    e = sub.add_parser("encode"); e.add_argument("table"); e.add_argument("rows", help="JSON array of rows")
    e.add_argument("--version", type=int, required=True); e.add_argument("--out", required=True); e.set_defaults(f=cmd_encode)
    k = sub.add_parser("keys"); k.add_argument("--lib", required=True, help="libgame.so (arm64) from the APK"); k.set_defaults(f=cmd_keys)
    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
