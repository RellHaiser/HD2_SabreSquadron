"""Makes every request to the Ghidra plugin (127.0.0.1:8089) name the client program.

With more than one program open (e.g. the dedicated server HD2DS_SabreSquadron.exe next to
the client) the plugin refuses requests without `program=`. Importing this module wraps
urllib.request.urlopen so that GET and POST requests to the plugin get the parameter.
Override with the environment variable GHIDRA_PROGRAM.
"""
import os
import urllib.parse
import urllib.request

PROGRAM = os.environ.get("GHIDRA_PROGRAM", "HD2_SabreSquadron_unpacked.exe")
_HOST = "127.0.0.1:8089"
_urlopen = urllib.request.urlopen


def with_program(url):
    if _HOST not in url or "program=" in url:
        return url
    sep = "&" if "?" in url else "?"
    return url + sep + "program=" + urllib.parse.quote(PROGRAM)


def _patched_urlopen(req, *args, **kwargs):
    if isinstance(req, str):
        req = with_program(req)
    elif isinstance(req, urllib.request.Request):
        req.full_url = with_program(req.full_url)
    return _urlopen(req, *args, **kwargs)


if getattr(urllib.request.urlopen, "__name__", "") != "_patched_urlopen":
    urllib.request.urlopen = _patched_urlopen
