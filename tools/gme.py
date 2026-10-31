#!/usr/bin/env python3
"""
gme.py - send a game (GME) request to the server as a player, from the command line.

The envelope is {"F4q6i9xe": {"Hhgi79M1": <request id>, "aV6cLn3v": <client id>},
"a3vSYuq2": {"Kn51uR4Y": <base64 of AES-128-ECB/PKCS#7 of the body JSON>}} posted to
/bf/gme/action.php; the AES key is the handler's (zero-padded to 16 bytes, the keys are read from
gimuserver/gme/GmeControllerHandlers.cpp). The reply body is decrypted the same way; an error
comes back in "b5PH6mZa". The body gets the login info of the player (IKqx1Cn9: user id, gumi id,
device id) and the signal key (6FrKacq7) like the client sends them.

    python tools/gme.py MissionStart '{"9Q1Lq5FS":[{"j28VNcUW":"1577","J3stQ7jd":"0","Z0Y4RoD7":"0","nA95Bdj6":"0","5Z1LNoyH":"0","u1iPEVUq":"-1"}]}'
    python tools/gme.py HomeInfo '{}' --user 8990764618982701 --gumi 00000001
"""
from __future__ import annotations

import argparse
import base64
import json
import re
import sys
import urllib.request
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parents[1]
HANDLERS = ROOT / "server" / "gimuserver" / "gme" / "GmeControllerHandlers.cpp"


def handlers() -> dict[str, tuple[str, str]]:
    """name -> (request id, key)"""
    out = {}
    for m in re.finditer(r'REGISTER\("(\w+)",\s*(\w+),\s*"(\w+)"\)', HANDLERS.read_text(encoding="utf-8")):
        out[m.group(2)] = (m.group(1), m.group(3))
    return out


def crypt(data: bytes, key: str, decrypt: bool) -> bytes:
    k = key.encode()[:16].ljust(16, b"\0")
    if decrypt:
        dec = Cipher(algorithms.AES(k), modes.ECB()).decryptor()
        out = dec.update(base64.b64decode(data)) + dec.finalize()
        return out[:-out[-1]]
    pad = 16 - len(data) % 16
    enc = Cipher(algorithms.AES(k), modes.ECB()).encryptor()
    return base64.b64encode(enc.update(data + bytes([pad]) * pad) + enc.finalize())


def send(name: str, body: dict, user: str, gumi: str, device: str, host: str) -> dict:
    rid, key = handlers()[name]
    body = {
        "IKqx1Cn9": [{"h7eY3sAK": user, "iN7buP2h": gumi, "iN7buP1i": device, "nrg19RGe": "0", "iN7buP0j": "gme.py", "DFY3k6qp": "2", "j2lk52Be": "2"}],
        "6FrKacq7": [{"Kn51uR4Y": "5EdKHavF"}],
        "KeC10fuL": [],
        **body,
    }
    env = {"F4q6i9xe": {"Hhgi79M1": rid, "aV6cLn3v": "gme.py"}, "a3vSYuq2": {"Kn51uR4Y": crypt(json.dumps(body).encode(), key, False).decode()}}
    rq = urllib.request.Request(f"http://{host}/bf/gme/action.php", data=json.dumps(env).encode(), headers={"Content-Type": "application/json"})
    reply = json.loads(urllib.request.urlopen(rq, timeout=30).read())
    if "b5PH6mZa" in reply and reply["b5PH6mZa"]:
        return {"error": reply["b5PH6mZa"]}
    return json.loads(crypt(reply["a3vSYuq2"]["Kn51uR4Y"].encode(), key, True))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("handler", help="handler name as in GmeControllerHandlers.cpp (MissionStart, HomeInfo...)")
    ap.add_argument("body", help="JSON object merged into the request body")
    ap.add_argument("--user", default="8990764618982701")
    ap.add_argument("--gumi", default="00000001")
    ap.add_argument("--device", default="KCSpan8nFrUeS5No")
    ap.add_argument("--host", default="127.0.0.1:9960")
    ap.add_argument("--full", action="store_true", help="print the whole reply, not the first 600 characters")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    reply = send(a.handler, json.loads(a.body), a.user, a.gumi, a.device, a.host)
    text = json.dumps(reply, ensure_ascii=False)
    print(text if a.full else text[:600])


if __name__ == "__main__":
    main()
