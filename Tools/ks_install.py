"""Helpers for install_iphone.command. Runs with any Python 3.8 or newer.

    ks_install.py teams                          Apple teams this Mac can sign with
    ks_install.py devices <devicectl.json>       connected iPhones and iPads
    ks_install.py set-ini <file> <section> <key> <value>
    ks_install.py get-ini <file> <section> <key>
    ks_install.py find-app <folder>...           newest .app or .ipa inside the folders

Every listing prints one tab-separated line per item.
"""

import json
import os
import plistlib
import re
import subprocess
import sys

TEAM_ID = re.compile(r"[A-Z0-9]{10}")


def _run(args, text=False, stdin=None):
    try:
        return subprocess.run(args, input=stdin, capture_output=True, text=text).stdout
    except OSError:
        return "" if text else b""


def teams():
    """Prints "team id, team name, 1 if it is a free Personal Team" for each team."""
    found = []

    def add(team_id, name, free):
        if TEAM_ID.fullmatch(team_id or "") and team_id not in [t[0] for t in found]:
            found.append((team_id, name, free))

    # Xcode keeps the teams of the Apple IDs added in Settings > Accounts in its preferences.
    try:
        prefs = plistlib.loads(_run(["defaults", "export", "com.apple.dt.Xcode", "-"]))
    except Exception:
        prefs = {}

    def walk(node):
        if isinstance(node, dict):
            if isinstance(node.get("teamID"), str):
                free = bool(node.get("isFreeProvisioningTeam")) or "personal" in str(node.get("teamType", "")).lower()
                add(node["teamID"], str(node.get("teamName", "")), free)
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    walk(prefs)

    # A signing certificate made by Xcode stores its team ID in the OU field.
    pem = ""
    for name in ("Apple Development", "iPhone Developer"):
        pem += _run(["security", "find-certificate", "-a", "-c", name, "-p"], text=True)
    for cert in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S):
        subject = _run(["openssl", "x509", "-noout", "-subject"], text=True, stdin=cert)
        team = re.search(r"\bOU\s*=\s*([A-Z0-9]{10})\b", subject)
        org = re.search(r"(?:^|[,/])\s*O\s*=\s*([^,/\n]+)", subject)
        if team:
            add(team.group(1), org.group(1).strip() if org else "", True)

    for team_id, name, free in found:
        print("%s\t%s\t%d" % (team_id, name.replace("\t", " ") or "-", 1 if free else 0))


def devices(path):
    """Prints "identifier, udid, name, iOS version, developer mode, pairing, transport" per device.

    Devices on a cable come first. Devices that are only reachable over Wi-Fi are listed only
    when nothing is plugged in.
    """
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return
    wired, wireless = [], []
    for dev in data.get("result", {}).get("devices", []):
        hw = dev.get("hardwareProperties") or {}
        props = dev.get("deviceProperties") or {}
        conn = dev.get("connectionProperties") or {}
        if hw.get("platform") != "iOS":
            continue
        transport = conn.get("transportType") or ""
        if transport not in ("wired", "localNetwork"):
            continue  # known to this Mac, but not connected right now
        row = [
            dev.get("identifier", ""),
            hw.get("udid", ""),
            props.get("name") or hw.get("marketingName") or "iPhone",
            props.get("osVersionNumber", ""),
            props.get("developerModeStatus", ""),
            conn.get("pairingState", ""),
            transport,
        ]
        if row[0] and row[1]:
            (wired if transport == "wired" else wireless).append(row)
    # Empty values become "-": the shell reads these lines with IFS=tab, which merges empty fields.
    for row in wired or wireless:
        print("\t".join(str(value).replace("\t", " ") or "-" for value in row))


def _read_lines(path):
    with open(path, encoding="utf-8") as f:
        return f.read().split("\n")


def get_ini(path, section, key):
    current = None
    for line in _read_lines(path):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            current = stripped[1:-1]
        elif current == section and stripped.startswith(key + "="):
            print(stripped[len(key) + 1:])
            return


def set_ini(path, section, key, value):
    """Sets key=value in [section], keeping comments and everything else in the file."""
    lines = _read_lines(path)
    current, section_end, done = None, None, False
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            current = stripped[1:-1]
            if current == section:
                section_end = i + 1
        elif current == section:
            if stripped.startswith(key + "="):
                lines[i] = "%s=%s" % (key, value)
                done = True
            elif stripped:
                section_end = i + 1
    if not done:
        if section_end is None:
            if lines and lines[-1] == "":
                lines.pop()
            lines += ["", "[%s]" % section, "%s=%s" % (key, value), ""]
        else:
            lines.insert(section_end, "%s=%s" % (key, value))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def find_app(folders):
    """Prints the newest .app bundle (or .ipa archive) found in the folders."""
    best = None
    for root in folders:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            depth = dirpath[len(root):].count(os.sep)
            for name in dirnames + filenames:
                full = os.path.join(dirpath, name)
                if (name.endswith(".app") and name != "KSDeviceSetup.app") or name.endswith(".ipa"):
                    stamp = os.path.getmtime(full)
                    if best is None or stamp > best[0]:
                        best = (stamp, full)
            # Do not look inside app bundles, and stay near the top of each folder.
            dirnames[:] = [d for d in dirnames if not d.endswith(".app") and depth < 3]
    if best:
        print(best[1])


def main(argv):
    if len(argv) >= 2 and argv[1] == "teams":
        teams()
    elif len(argv) == 3 and argv[1] == "devices":
        devices(argv[2])
    elif len(argv) == 6 and argv[1] == "set-ini":
        set_ini(argv[2], argv[3], argv[4], argv[5])
    elif len(argv) == 5 and argv[1] == "get-ini":
        get_ini(argv[2], argv[3], argv[4])
    elif len(argv) >= 3 and argv[1] == "find-app":
        find_app(argv[2:])
    else:
        sys.stderr.write(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
