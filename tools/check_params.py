#!/usr/bin/env python3
"""Session-compatibility guard for parameters.

Saved sessions and presets store each control by its parameter ID, and choice
controls by index. So between releases:
  * a parameter ID may never be removed or renamed;
  * a choice list may only grow at the end (no reordering or removal).
New parameters are fine.

  tools/check_params.py           check against tests/golden/parameters.json
  tools/check_params.py --update  record the current parameters (after adding some)
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SNAPSHOT = ROOT / "tests/golden/parameters.json"
SOURCES = {"septima": ROOT / "plugins/septima/SeptimaProcessor.cpp",
           "vista_rooms": ROOT / "plugins/vista_rooms/VistaProcessor.cpp",
           "common": ROOT / "plugins/common/Widgets.cpp"}   # tempo-sync note values

def scan(path):
    src = path.read_text()
    ids = re.findall(r'rrui::add(?:Float|Choice|Bool|Int)\(l,\s*"([^"]+)"', src)
    arrays = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
              for m in re.finditer(r"(?:static\s+)?const juce::StringArray (\w+)\s*\{([^}]*)\}", src)}
    return {"ids": ids, "choices": arrays}

def main():
    current = {name: scan(path) for name, path in SOURCES.items()}
    if "--update" in sys.argv:
        SNAPSHOT.write_text(json.dumps(current, indent=1) + "\n")
        print(f"recorded parameters to {SNAPSHOT.relative_to(ROOT)}")
        return 0
    old = json.loads(SNAPSHOT.read_text())
    errors = []
    for plugin, snap in old.items():
        cur = current.get(plugin, {"ids": [], "choices": {}})
        for pid in snap["ids"]:
            if pid not in cur["ids"]:
                errors.append(f"{plugin}: parameter '{pid}' was removed or renamed")
        for name, items in snap["choices"].items():
            now = cur["choices"].get(name)
            if now is None:
                errors.append(f"{plugin}: choice list {name} disappeared")
            elif now[:len(items)] != items:
                errors.append(f"{plugin}: choice list {name} changed order or lost entries ({items} -> {now})")
    for e in errors:
        print("ERROR", e)
    print(f"parameter check: {sum(len(v['ids']) for v in old.values())} IDs checked, {len(errors)} problems")
    return 1 if errors else 0

if __name__ == "__main__":
    sys.exit(main())
