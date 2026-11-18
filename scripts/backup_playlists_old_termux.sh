#!/usr/bin/env bash
# Backup the playlists of an ALREADY-INSTALLED old LeviMusic Bridge (v1.1.x).
# Run BEFORE uninstalling the old, debug-signed APK for signing-key migration.
set -euo pipefail
if ! command -v python3 >/dev/null 2>&1; then
  echo "Install python3 using: pkg install python"
  exit 1
fi
echo "Keep the OLD LeviMusic Bridge running before executing this script."
printf "Paste old Bridge Key (characters hidden): "
IFS= read -rs LEVIMUSIC_OLD_KEY
printf '\n'
export LEVIMUSIC_OLD_KEY
python3 - <<'PY'
import json, os, pathlib, urllib.parse, urllib.request

key=os.environ.get("LEVIMUSIC_OLD_KEY","").strip()
if len(key)!=48 or any(c not in "0123456789abcdef" for c in key):
    raise SystemExit("Invalid Bridge Key. Copy it from the old LeviMusic APK.")
base="http://127.0.0.1:38771"
def load(path):
    request=urllib.request.Request(base+path,headers={"X-LeviMusic-Token":key})
    try:
        with urllib.request.urlopen(request,timeout=8) as response:
            raw=response.read(3_000_000)
    except Exception as exc:
        raise SystemExit("Old Bridge is offline or key rejected: "+str(exc))
    return json.loads(raw)

listings=load("/v1/playlists").get("playlists",[])
data=[]
for item in listings[:40]:
    name=item.get("name","")
    if not name:continue
    payload=load("/v1/playlist/tracks?name="+urllib.parse.quote(name,safe=""))
    songs=payload.get("tracks",[])[:300]
    data.append({"name":name,"tracks":songs})
payload={"format":"levimusic-playlists-1","playlists":data}
downloads=pathlib.Path("/storage/emulated/0/Download")
out=(downloads if downloads.is_dir() and os.access(downloads,os.W_OK) else pathlib.Path.home())/"LeviMusic-playlists-backup.json"
out.write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding="utf-8")
print("Saved",len(data),"playlist(s) to:",out)
print("Back this file up, then use My Playlists -> Import backup on the NEW Bridge APK.")
PY
unset LEVIMUSIC_OLD_KEY
