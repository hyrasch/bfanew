#!/usr/bin/env python3
"""
patch_apk.py - point the Brave Frontier Global 2.19.6.0 Android client at a
private server, then let the Android SDK tools align and sign it.

    python tools/patch_apk.py --apk ../brave-frontier-2-19-6-0.apk --host 192.168.1.14 \
        --sdk D:/.Code/android-sdk --out ../build

The original APK is only read. Output: <out>/bf-<host>-<port>.apk (aligned,
signed v1+v2 with <out>/bfanew-debug.keystore, created on first run; keep it,
a different key needs an uninstall first).

Patches (offsets valid for the APK with SHA-256 ORIG_SHA256 only; they come
from the disassembly notes in bfanew/CLAUDE.md, "Android client: findings"):
  * DLS host    SERVICE_PHP_URL()  "api-sl.bfww.gumi.sg"          -> host:port
  * player API  SERVICE_PHP_URL2() "api-prod-player.bfww.gumi.sg" -> host:port
  * game host fallback PHP_URL()   "ios.bfww.gumi.sg"             -> host:port (if it fits)
  * gumi live default "https://api-sl.gl.gumi.sg/" -> "http://host:port/" (+ its length immediates)
  * every "https://" the client prepends -> "http://" (string literal on both ABIs,
    plus the inline movk copies on arm64)
  * the ":80" suffix added to the HOST 1 endpoint -> disabled
The manifest is left alone: libcurl ignores the cleartext policy, so the game
traffic works; only the news/notice WebViews would need usesCleartextTraffic.

Requires Python 3.10+, a JDK, Android build-tools (zipalign, apksigner).
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path

ORIG_SHA256 = "346d29478b3a2a8246c79267abf7c5d9e1e785b10d63677dbdf93fb14d139bf9"
SIG_FILES = {"META-INF/MANIFEST.MF", "META-INF/CERT.SF", "META-INF/CERT.RSA"}


def cstr(s: str, room: int) -> bytes:
    """s as a NUL-padded C string of exactly `room` bytes (NUL included)."""
    b = s.encode("ascii")
    if len(b) >= room:
        raise SystemExit(f"{s!r} is {len(b)} chars, at most {room - 1} fit here")
    return b.ljust(room, b"\0")


def a64_imm16(word: int, imm: int) -> int:
    """AArch64 MOVZ/MOVK with its imm16 field replaced."""
    return (word & ~(0xFFFF << 5)) | (imm << 5)


def arm_imm8(word: int, imm: int) -> int:
    """ARM-mode MOV rd, #imm8 (rotation 0) with the immediate replaced."""
    assert (word & 0x0FF00000) == 0x03A00000 and imm < 256
    return (word & ~0xFF) | imm


class P:
    def __init__(self, off: int, old: bytes, new: bytes, why: str):
        assert len(old) == len(new), why
        self.off, self.old, self.new, self.why = off, old, new, why


def str_patch(off: int, old: str, new: str, why: str) -> P:
    return P(off, cstr(old, len(old) + 1), cstr(new, len(old) + 1), why)


def word_patch(off: int, old: int, new: int, why: str) -> P:
    return P(off, struct.pack("<I", old), struct.pack("<I", new), why)


def arm64_patches(hp: str) -> list[P]:
    gl = f"http://{hp}/"
    ps = [
        str_patch(0x232711F, "api-sl.bfww.gumi.sg", hp, "SERVICE_PHP_URL (DLS host)"),
        str_patch(0x2327133, "api-prod-player.bfww.gumi.sg", hp, "SERVICE_PHP_URL2 (player API)"),
        str_patch(0x2369CE3, "https://", "http://", "https:// literal (CommonUtils::getPhpUrl ...)"),
        str_patch(0x2319E64, "https://api-sl.gl.gumi.sg/", gl, "gumi live default URL"),
        word_patch(0xF5C0D4, 0x52800342, a64_imm16(0x52800342, len(gl)), "NetworkManager ctor: assign length"),
        word_patch(0xF5C278, 0x52800346, a64_imm16(0x52800346, len(gl)), "NetworkManager::sharedInstance: assign length"),
        word_patch(0x1C7A918, 0x52800062, a64_imm16(0x52800062, 0), "reinitEndPoints: append ':80' -> 0 bytes"),
    ]
    if len(hp) <= 15:  # 16-byte SSO copy + size byte (len << 1)
        ps.append(str_patch(0x23270B8, "ios.bfww.gumi.sg", hp, "PHP_URL default game host"))
        for off in (0x1162CB0, 0x1162E10):
            ps.append(word_patch(off, 0x52800408, a64_imm16(0x52800408, len(hp) << 1), "PHP_URL default: SSO size"))
    else:
        print(f"  note: {hp} is longer than 15 chars, PHP_URL default left as is (DLS 'game' wins anyway)")
    # inline "https://": mov xN,#0x7468 ; movk #0x7074,lsl16 ; movk #0x3a73,lsl32 ; movk #0x2f2f,lsl48
    # and the SSO size byte mov w8,#0x10.  Rewrite to "http://\0" (size 7).
    for where, (o_size, o_32, o_48, rd) in {
        "GumiLiveManager::init": (0xF5FBF0, 0xF5FBEC, 0xF5FBF4, 9),
        "ServiceRequestManager::reinitEndPoints": (0x1C7A5C8, 0x1C7A5DC, 0x1C7A5E0, 8),
    }.items():
        m32 = 0xF2C00000 | (0x3A73 << 5) | rd
        m48 = 0xF2E00000 | (0x2F2F << 5) | rd
        ps += [
            word_patch(o_32, m32, a64_imm16(m32, 0x2F3A), f"{where}: inline https:// -> http:// (lsl32)"),
            word_patch(o_48, m48, a64_imm16(m48, 0x002F), f"{where}: inline https:// -> http:// (lsl48)"),
            word_patch(o_size, 0x52800208, a64_imm16(0x52800208, 7 << 1), f"{where}: inline scheme SSO size"),
        ]
    return ps


def armv7_patches(hp: str) -> list[P]:
    gl = f"http://{hp}/"
    ps = [
        str_patch(0x1861ED2, "api-sl.bfww.gumi.sg", hp, "SERVICE_PHP_URL (DLS host)"),
        str_patch(0x1861EE6, "api-prod-player.bfww.gumi.sg", hp, "SERVICE_PHP_URL2 (player API)"),
        str_patch(0x1892D2D, "https://", "http://", "https:// literal (reinitEndPoints, getPhpUrl ...)"),
        str_patch(0x185B6C3, "https://api-sl.gl.gumi.sg/", gl, "gumi live default URL"),
        word_patch(0xCCB920, 0xE3A0201A, arm_imm8(0xE3A0201A, len(gl)), "NetworkManager ctor: assign length"),
        word_patch(0xCCBAAC, 0xE3A0201A, arm_imm8(0xE3A0201A, len(gl)), "NetworkManager::sharedInstance: assign length"),
        # `cbz r4, no_append` -> `b no_append`: never append ":80"
        P(0x1383F90, bytes.fromhex("acb1"), bytes.fromhex("15e0"), "reinitEndPoints: never append ':80'"),
    ]
    if len(hp) <= 16:
        ps.append(str_patch(0x1861E66, "ios.bfww.gumi.sg", hp, "PHP_URL default game host"))
    return ps


def apply(data: bytes, patches: list[P], label: str) -> bytes:
    buf = bytearray(data)
    for p in patches:
        have = bytes(buf[p.off : p.off + len(p.old)])
        if have != p.old:
            raise SystemExit(f"{label} @0x{p.off:x} ({p.why}): found {have.hex()}, expected {p.old.hex()}")
        buf[p.off : p.off + len(p.new)] = p.new
        print(f"  {label} 0x{p.off:08x}  {p.why}")
    return bytes(buf)


def find_tool(sdk: Path, name: str) -> Path:
    cands = sorted((sdk / "build-tools").glob(f"*/{name}"), reverse=True)
    if not cands:
        raise SystemExit(f"{name} not found under {sdk / 'build-tools'}; install build-tools with sdkmanager")
    return cands[0]


def run(cmd: list, **kw) -> None:
    print("  $", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apk", required=True, type=Path, help="original brave-frontier-2-19-6-0.apk (read only)")
    ap.add_argument("--host", required=True, help="server IP or hostname reachable from the device")
    ap.add_argument("--port", type=int, default=9960)
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[2] / "build")
    ap.add_argument("--sdk", type=Path, default=Path(os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or ""))
    ap.add_argument("--force", action="store_true", help="patch even if the APK hash is not the known one")
    args = ap.parse_args()

    hp = f"{args.host}:{args.port}" if args.port != 80 else args.host
    if not str(args.sdk) or not args.sdk.exists():
        raise SystemExit("--sdk (or ANDROID_HOME) must point at an Android SDK with build-tools")
    zipalign, apksigner = find_tool(args.sdk, "zipalign.exe"), find_tool(args.sdk, "apksigner.bat")
    java = shutil.which("java")
    if not java and os.environ.get("JAVA_HOME"):
        java = str(Path(os.environ["JAVA_HOME"]) / "bin" / "java.exe")
    if not java or not Path(java).exists():
        raise SystemExit("java not found on PATH / JAVA_HOME (apksigner needs a JDK)")
    java_home = Path(java).resolve().parents[1]

    print(f"input  {args.apk}")
    digest = hashlib.sha256(args.apk.read_bytes()).hexdigest()
    if digest != ORIG_SHA256:
        msg = f"APK SHA-256 {digest} is not the known original {ORIG_SHA256}; offsets may be wrong"
        if not args.force:
            raise SystemExit(msg + " (use --force to try anyway)")
        print("WARNING:", msg)

    args.out.mkdir(parents=True, exist_ok=True)
    unsigned = args.out / f"bf-{args.host}-{args.port}.unsigned.apk"
    aligned = args.out / f"bf-{args.host}-{args.port}.aligned.apk"
    final = args.out / f"bf-{args.host}-{args.port}.apk"
    keystore = args.out / "bfanew-debug.keystore"

    print(f"patching for {hp}")
    with zipfile.ZipFile(args.apk) as zin, zipfile.ZipFile(unsigned, "w") as zout:
        for info in zin.infolist():
            if info.filename in SIG_FILES:
                continue  # apksigner writes new ones
            data = zin.read(info)
            if info.filename == "lib/arm64-v8a/libgame.so":
                data = apply(data, arm64_patches(hp), "arm64")
            elif info.filename == "lib/armeabi-v7a/libgame.so":
                data = apply(data, armv7_patches(hp), "armv7")
            out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            out.compress_type = info.compress_type  # keep stored/deflated as in the original
            out.external_attr = info.external_attr
            zout.writestr(out, data)

    env = {**os.environ, "JAVA_HOME": str(java_home), "PATH": str(java_home / "bin") + os.pathsep + os.environ.get("PATH", "")}
    print("aligning")
    run([zipalign, "-f", "4", unsigned, aligned])
    run([zipalign, "-c", "4", aligned])

    if not keystore.exists():
        print("creating signing key", keystore)
        run([java_home / "bin" / "keytool.exe", "-genkeypair", "-keystore", keystore, "-storepass", "bfanew-dev",
             "-keypass", "bfanew-dev", "-alias", "bfanew", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=bfanew dev, O=bfanew"], env=env)
    print("signing")
    run([apksigner, "sign", "--ks", keystore, "--ks-pass", "pass:bfanew-dev", "--ks-key-alias", "bfanew",
         "--v1-signing-enabled", "true", "--v2-signing-enabled", "true", "--v3-signing-enabled", "false",
         "--out", final, aligned], env=env)
    run([apksigner, "verify", "--verbose", "--print-certs", final], env=env)
    unsigned.unlink()
    aligned.unlink()
    print(f"\nready: {final}\n  adb install -r \"{final}\"")


if __name__ == "__main__":
    main()
