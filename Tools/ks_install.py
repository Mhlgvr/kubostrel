"""Helpers for install_iphone.command. Runs with any Python 3.8 or newer.

    ks_install.py teams [paid-team-file]         Apple teams this Mac can sign with
    ks_install.py devices <devicectl.json>       connected iPhones and iPads
    ks_install.py set-ini <file> <section> <key> <value>
    ks_install.py get-ini <file> <section> <key>
    ks_install.py find-app [--newer <file>] <folder>...
                                                 newest .app or .ipa inside the folders
    ks_install.py store-archive <app> <archive> <team> <privacy manifest>
                                                 Xcode archive of the app for App Store Connect

Every listing prints one tab-separated line per item.
"""

import datetime
import json
import mmap
import os
import plistlib
import re
import shutil
import subprocess
import sys

TEAM_ID = re.compile(r"[A-Z0-9]{10}")
PROFILE_FOLDERS = (
    "~/Library/Developer/Xcode/UserData/Provisioning Profiles",  # Xcode 16 and newer
    "~/Library/MobileDevice/Provisioning Profiles",
)

# What App Store Connect wants in the app's Info.plist. Without the first key it asks about
# encryption for every build; the game uses none beyond what iOS itself provides. The others are
# in DefaultEngine.ini too, and are added here only if Unreal left them out.
STORE_PLIST_KEYS = (
    ("ITSAppUsesNonExemptEncryption", False),
    ("UIRequiresFullScreen", True),
    ("NSLocalNetworkUsageDescription", "Нужно, чтобы играть с друзьями по Wi-Fi."),
)

# Used only when neither the app nor the engine has a privacy manifest. It names the APIs Apple
# asks about that Unreal uses on iOS: file dates, time since boot, free disk space, user defaults.
PRIVACY_MANIFEST = {
    "NSPrivacyTracking": False,
    "NSPrivacyTrackingDomains": [],
    "NSPrivacyCollectedDataTypes": [],
    "NSPrivacyAccessedAPITypes": [
        {"NSPrivacyAccessedAPIType": "NSPrivacyAccessedAPICategory" + category,
         "NSPrivacyAccessedAPITypeReasons": [reason]}
        for category, reason in (
            ("FileTimestamp", "C617.1"),
            ("SystemBootTime", "35F9.1"),
            ("DiskSpace", "E174.1"),
            ("UserDefaults", "CA92.1"),
        )
    ],
}

# App Store Connect refuses a build whose code names an API that reads private data, unless
# Info.plist says why the app needs it. This holds even for engine code the game never runs, so a
# key is added when the code names one of its APIs. The game reads none of this, and the texts say so.
PURPOSE_STRINGS = (
    ("NSMicrophoneUsageDescription", "Кубострел не пользуется микрофоном.",
     (b"recordPermission", b"RecordPermission", b"AVAudioSessionCategoryPlayAndRecord",
      b"AVAudioSessionCategoryRecord", b"AVAudioRecorder", b"AVCaptureDevice")),
    ("NSCameraUsageDescription", "Кубострел не пользуется камерой.",
     (b"AVCaptureDevice", b"UIImagePickerController")),
    ("NSPhotoLibraryUsageDescription", "Кубострел не открывает фотографии.",
     (b"PHPhotoLibrary", b"UIImagePickerController", b"ALAssetsLibrary")),
    ("NSPhotoLibraryAddUsageDescription", "Кубострел не сохраняет фотографии.",
     (b"PHPhotoLibrary", b"UIImageWriteToSavedPhotosAlbum", b"ALAssetsLibrary")),
    ("NSLocationWhenInUseUsageDescription", "Кубострел не узнаёт, где ты.", (b"CLLocationManager",)),
    ("NSLocationAlwaysAndWhenInUseUsageDescription", "Кубострел не узнаёт, где ты.",
     (b"requestAlwaysAuthorization",)),
    ("NSBluetoothAlwaysUsageDescription", "Кубострел не пользуется Bluetooth.",
     (b"CBCentralManager", b"CBPeripheralManager")),
    ("NSContactsUsageDescription", "Кубострел не читает контакты.", (b"CNContactStore", b"ABAddressBook")),
    ("NSCalendarsFullAccessUsageDescription", "Кубострел не читает календарь.", (b"EKEventStore",)),
    ("NSRemindersFullAccessUsageDescription", "Кубострел не читает напоминания.", (b"EKEventStore",)),
    ("NSMotionUsageDescription", "Кубострел не считает шаги.", (b"CMMotionActivityManager", b"CMPedometer")),
    ("NSSpeechRecognitionUsageDescription", "Кубострел не распознаёт речь.", (b"SFSpeechRecognizer",)),
    ("NSAppleMusicUsageDescription", "Кубострел не открывает медиатеку.",
     (b"MPMediaLibrary", b"MPMediaQuery", b"MPMediaPickerController")),
    ("NSUserTrackingUsageDescription", "Кубострел не следит за тобой.", (b"ATTrackingManager",)),
)


def _run(args, text=False, stdin=None):
    try:
        return subprocess.run(args, input=stdin, capture_output=True, text=text).stdout
    except OSError:
        return "" if text else b""


def teams(paid_file=None):
    """Prints "team id, team name, kind" for each team. The kind is 1 for a free Personal Team,
    0 for a team of the paid Apple Developer Program and ? when nothing on this Mac tells."""
    found = {}  # team id -> [name, kind], in the order the teams were found

    def add(team_id, name, kind):
        if not TEAM_ID.fullmatch(team_id or ""):
            return
        entry = found.setdefault(team_id, ["", "?"])
        entry[0] = entry[0] or name
        if entry[1] == "?":
            entry[1] = kind

    # Older Xcode keeps the teams of the Apple IDs added in Settings > Accounts in its preferences.
    # Recent versions no longer do that for a newly added Apple ID, hence the other sources below.
    try:
        prefs = plistlib.loads(_run(["defaults", "export", "com.apple.dt.Xcode", "-"]))
    except Exception:
        prefs = {}

    def walk(node):
        if isinstance(node, dict):
            if isinstance(node.get("teamID"), str):
                free = bool(node.get("isFreeProvisioningTeam")) or "personal" in str(node.get("teamType", "")).lower()
                add(node["teamID"], str(node.get("teamName", "")), "1" if free else "0")
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    walk(prefs)

    # A signing certificate made by Xcode stores its team ID in the OU field. Only a paid team can
    # have a distribution certificate.
    for names, kind in ((("Apple Development", "iPhone Developer"), "?"), (("Apple Distribution", "iPhone Distribution"), "0")):
        pem = ""
        for name in names:
            pem += _run(["security", "find-certificate", "-a", "-c", name, "-p"], text=True)
        for cert in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S):
            subject = _run(["openssl", "x509", "-noout", "-subject"], text=True, stdin=cert)
            team = re.search(r"\bOU\s*=\s*([A-Z0-9]{10})\b", subject)
            org = re.search(r"(?:^|[,/])\s*O\s*=\s*([^,/\n]+)", subject)
            if team:
                add(team.group(1), org.group(1).strip() if org else "", kind)

    # The team last chosen in an Xcode project, and the teams of the signing profiles Xcode made.
    last = prefs.get("IDEProvisioningTeamManagerLastSelectedTeamID") if isinstance(prefs, dict) else None
    if isinstance(last, str):
        add(last, "", "?")
    for team_id, name, free in _profile_teams():
        add(team_id, name, "1" if free else "0")

    # The team a TestFlight upload was signed with, unless this Mac knows it to be a free one.
    try:
        with open(paid_file, encoding="utf-8") as f:
            add(f.read().strip(), "", "0")
    except (OSError, TypeError):
        pass

    for team_id, (name, kind) in found.items():
        print("%s\t%s\t%s" % (team_id, name.replace("\t", " ") or "-", kind))


def _profile_teams():
    """Yields (team id, team name, free) for each signing profile on this Mac that has not expired."""
    now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
    for folder in PROFILE_FOLDERS:
        folder = os.path.expanduser(folder)
        try:
            names = sorted(os.listdir(folder))
        except OSError:
            continue
        for name in names:
            if not name.endswith((".mobileprovision", ".provisionprofile")):
                continue
            try:
                profile = plistlib.loads(_run(["security", "cms", "-D", "-i", os.path.join(folder, name)]))
                created, expires = profile["CreationDate"], profile["ExpirationDate"]
                team_id = profile["TeamIdentifier"][0]
                if expires <= now:
                    continue
                # A free Personal Team gets profiles for 7 days, a paid team for a year.
                free = (expires - created).days < 30
            except Exception:
                continue
            yield team_id, str(profile.get("TeamName", "")), free


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


def _written(path):
    """When an app bundle (or .ipa) was last built: signing rewrites _CodeSignature at the end."""
    stamps = [os.path.getmtime(path)]
    for inner in ("Info.plist", os.path.join("_CodeSignature", "CodeResources")):
        try:
            stamps.append(os.path.getmtime(os.path.join(path, inner)))
        except OSError:
            pass
    return max(stamps)


def find_app(folders, newer=None):
    """Prints the newest .app bundle (or .ipa archive) found in the folders, or only one
    built after the file newer was written."""
    best = None
    since = os.path.getmtime(newer) if newer else None
    for root in folders:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            depth = dirpath[len(root):].count(os.sep)
            for name in dirnames + filenames:
                full = os.path.join(dirpath, name)
                if (name.endswith(".app") and name != "KSDeviceSetup.app") or name.endswith(".ipa"):
                    stamp = _written(full)
                    if (since is None or stamp >= since) and (best is None or stamp > best[0]):
                        best = (stamp, full)
            # Do not look inside app bundles, and stay near the top of each folder.
            dirnames[:] = [d for d in dirnames if not d.endswith(".app") and depth < 3]
    if best:
        print(best[1])


def _signing_identity(app):
    try:
        out = subprocess.run(["codesign", "-dvv", app], capture_output=True, text=True).stderr
    except OSError:
        return ""
    match = re.search(r"^Authority=(.+)$", out, re.M)
    return match.group(1).strip() if match else ""


def _code_files(app, info):
    """The app's executable and the code of the frameworks inside it."""
    files = [os.path.join(app, info.get("CFBundleExecutable", ""))]
    for dirpath, dirnames, filenames in os.walk(os.path.join(app, "Frameworks")):
        files += [os.path.join(dirpath, name) for name in filenames
                  if name.endswith(".dylib") or os.path.basename(dirpath) == name + ".framework"]
    return [path for path in files if os.path.isfile(path)]


def _mentioned(paths, apis):
    """Which of the API names appear anywhere in the files."""
    found = set()
    for path in paths:
        if os.path.getsize(path) == 0:
            continue
        with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            found.update(api for api in apis if api not in found and data.find(api) != -1)
    return found


def _store_version(value, default):
    """App Store Connect takes versions of one to three numbers, such as 1.0.0."""
    numbers = re.findall(r"\d+", str(value))[:3]
    return ".".join(numbers) if numbers else default


def store_archive(app, archive, team, privacy):
    """Makes an Xcode archive of the app, which xcodebuild -exportArchive signs for the App Store
    and uploads. Unreal 5.8 cannot archive the game itself: its "xcodebuild archive" leaves the
    game data out of the app. Prints what the app was missing, one item per line."""
    name = os.path.basename(os.path.normpath(app))
    target = os.path.join(archive, "Products", "Applications", name)
    if os.path.isdir(archive):
        shutil.rmtree(archive)
    shutil.copytree(app, target, symlinks=True)

    plist = os.path.join(target, "Info.plist")
    with open(plist, "rb") as f:
        data = f.read()
    info = plistlib.loads(data)
    added = [key for key, value in STORE_PLIST_KEYS if key not in info]
    info.update((key, value) for key, value in STORE_PLIST_KEYS if key in added)
    wanted = {api for _, _, apis in PURPOSE_STRINGS for api in apis}
    mentioned = _mentioned(_code_files(target, info), wanted)
    for key, text, apis in PURPOSE_STRINGS:
        if key not in info and mentioned.intersection(apis):
            info[key] = text
            added.append(key)
    for key, default in (("CFBundleShortVersionString", "1.0.0"), ("CFBundleVersion", "1")):
        version = _store_version(info.get(key, ""), default)
        if info.get(key) != version:
            info[key] = version
            added.append("%s %s" % (key, version))
    if added:
        with open(plist, "wb") as f:
            plistlib.dump(info, f, fmt=plistlib.FMT_BINARY if data.startswith(b"bplist") else plistlib.FMT_XML)
    # Apple refuses builds that use certain system APIs without saying why in this file.
    manifest = os.path.join(target, "PrivacyInfo.xcprivacy")
    if not os.path.exists(manifest):
        if os.path.isfile(privacy):
            shutil.copyfile(privacy, manifest)
        else:
            with open(manifest, "wb") as f:
                plistlib.dump(PRIVACY_MANIFEST, f)
        added.append("PrivacyInfo.xcprivacy")
    for item in added:
        print("added\t" + item)
    if not info.get("CFBundleIconName") and not info.get("CFBundleIcons"):
        print("missing\ticon")

    properties = {
        "ApplicationPath": "Applications/" + name,
        "Architectures": ["arm64"],
        "CFBundleIdentifier": str(info.get("CFBundleIdentifier", "")),
        "CFBundleShortVersionString": info["CFBundleShortVersionString"],
        "CFBundleVersion": info["CFBundleVersion"],
        "Team": team,
    }
    identity = _signing_identity(target)
    if identity:
        properties["SigningIdentity"] = identity
    now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None, microsecond=0)
    with open(os.path.join(archive, "Info.plist"), "wb") as f:
        plistlib.dump({
            "ApplicationProperties": properties,
            "ArchiveVersion": 2,
            "CreationDate": now,
            "Name": "Kubostrel",
            "SchemeName": "Kubostrel",
        }, f)


def main(argv):
    if len(argv) in (2, 3) and argv[1] == "teams":
        teams(argv[2] if len(argv) == 3 else None)
    elif len(argv) == 3 and argv[1] == "devices":
        devices(argv[2])
    elif len(argv) == 6 and argv[1] == "set-ini":
        set_ini(argv[2], argv[3], argv[4], argv[5])
    elif len(argv) == 5 and argv[1] == "get-ini":
        get_ini(argv[2], argv[3], argv[4])
    elif len(argv) >= 5 and argv[1:3] == ["find-app", "--newer"]:
        find_app(argv[4:], argv[3])
    elif len(argv) >= 3 and argv[1] == "find-app":
        find_app(argv[2:])
    elif len(argv) == 6 and argv[1] == "store-archive":
        store_archive(argv[2], argv[3], argv[4], argv[5])
    else:
        sys.stderr.write(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
