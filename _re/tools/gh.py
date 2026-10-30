"""Small client for the GhidraMCP plugin HTTP server (127.0.0.1:8089), bound to IJoy.dll.

Every request carries `program=IJoy.dll` (several programs are open in the Ghidra project).
"""
import json
import os
import urllib.parse
import urllib.request

PROGRAM = os.environ.get("GHIDRA_PROGRAM", "IJoy.dll")
BASE = "http://127.0.0.1:8089/"


def get(endpoint, timeout=300, **params):
    params.setdefault("program", PROGRAM)
    url = BASE + endpoint + "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=timeout) as r:
        data = r.read().decode("utf-8", "replace")
    try:
        return json.loads(data)
    except json.JSONDecodeError:
        return data


def post(endpoint, body, timeout=300):
    url = BASE + endpoint + "?" + urllib.parse.urlencode({"program": PROGRAM})
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        data = r.read().decode("utf-8", "replace")
    try:
        return json.loads(data)
    except json.JSONDecodeError:
        return data


def items(kind, page=1000):
    """All entries of /list_program_items?kind=..."""
    out, off = [], 0
    while True:
        r = get("list_program_items", kind=kind, limit=page, offset=off)
        out += r["items"]
        off += len(r["items"])
        if not r["items"] or off >= r.get("total", 0):
            return out
