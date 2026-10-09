#!/usr/bin/env bash
# CI-only Wine command interpreter. Never install this in the Flutter SDK.
set -euo pipefail
root="$GITHUB_WORKSPACE/_work/wine-cmd"
mkdir -p "$root"
curl --fail --location --connect-timeout 30 --max-time 300 --retry 3 \
  https://codeload.github.com/wine-mirror/wine/tar.gz/db11d0fe6a169c457e23d007e20404643d067aa8 \
  --output "$root/source.tar.gz"
echo "18aaee150ad540885b9706ae73ccf6febca904049de2792199a9dc18a2772e6a  $root/source.tar.gz" | sha256sum --check
python3 - "$root" <<'PY'
import sys,tarfile
from pathlib import Path
root=Path(sys.argv[1])
with tarfile.open(root/'source.tar.gz') as archive:
    archive.extractall(root,filter='data')
PY
source="$root/wine-db11d0fe6a169c457e23d007e20404643d067aa8"
cd "$source"
patch --batch --fuzz=0 -p1 < "$GITHUB_WORKSPACE/dev/windows_arm32/testing/wine-cmd-handles.patch"
# Keep the upstream icon resolutions and every available message translation.
for size in 16 24 32 48 64 128 256; do
  rsvg-convert -w "$size" -h "$size" programs/cmd/wcmd.svg \
    --output "$root/icon-$size.png"
done
icotool --create --output programs/cmd/wcmd.ico "$root"/icon-*.png
for translation in po/*.po; do
  msgfmt --output-file="${translation%.po}.mo" "$translation"
done
/opt/wine-stable/bin/wrc -Iinclude -Iprograms/cmd --po-dir=po \
  -o "$root/cmd.res" programs/cmd/cmd.rc
/opt/wine-stable/bin/winegcc -b x86_64-w64-mingw32 -municode \
  -Iinclude -I/opt/wine-stable/include -D__WINESRC__ \
  -o "$root/cmd.exe" programs/cmd/batch.c programs/cmd/builtins.c \
  programs/cmd/directory.c programs/cmd/wcmdmain.c "$root/cmd.res" \
  -lshell32 -luser32 -ladvapi32 -lkernelbase
/opt/wine-stable/bin/winegcc -b x86_64-w64-mingw32 -municode \
  -I/opt/wine-stable/include -D__WINESRC__ \
  -o "$root/check-lock.exe" \
  "$GITHUB_WORKSPACE/dev/windows_arm32/testing/check_windows_lock.c"
