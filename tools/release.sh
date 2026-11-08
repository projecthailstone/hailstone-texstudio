#!/usr/bin/env bash
# Makes a release of Hailstone Texture Studio as a Hailstone tool add-on (the client's docs/addons.md "Tools"):
# builds it for each platform, packs each build's dist/ (the client's tools/make_addon.py), signs
# hailstone-addon.txt, and with --publish uploads everything as a GitHub release of this repository, the one players
# paste on the Add-ons screen.
#
#   tools/release.sh 1.0.0-beta              build and pack into release/v1.0.0-beta/, nothing uploaded
#   tools/release.sh 1.0.0-beta --publish    the same, then gh release create v1.0.0-beta
#
# Needs:
#   a hailstone-client checkout ($HAILSTONE_CLIENT, default ../hailstone-client) at the commit to build against, with
#     tools/make_addon.py and a built hailstone_sign ($HAILSTONE_SIGN, default $HAILSTONE_CLIENT/build/hailstone_sign);
#   the add-on key's secret half ($ADDON_KEY, default ~/.config/hailstone-release/addon-key.secret: keep it off
#     GitHub, and keep using the same one, since a player's client refuses an update signed with another key);
#   the converted models ($TEXSTUDIO_MODELS, default ~/.local/share/hailstone-texpack/models/ncnn; README "Models");
#   Docker with the client's images: hailstone-linux-build:24.04 (Ubuntu 24.04, so it runs on its glibc and
#     libstdc++) and hailstone-msvc-build:24.04 with msvc-wine in ~/opt/msvc (the client's docs/ci.md "MSVC under
#     Wine"). $JOBS caps the parallel jobs (default 20).
set -euo pipefail

NAME=texstudio
RELEASE_REPO=projecthailstone/hailstone-texstudio
DESCRIPTION="Builds HD texture packs from your own game files on your graphics card: the PBRify upscaler, with normal and roughness maps."
AUTHOR="Project Hailstone"

version=${1:?usage: tools/release.sh <version> [--publish]}
version=${version#v}
publish=${2:-}
here=$(cd "$(dirname "$0")/.." && pwd)
client=$(cd "${HAILSTONE_CLIENT:-$here/../hailstone-client}" && pwd)
sign=${HAILSTONE_SIGN:-$client/build/hailstone_sign}
key=${ADDON_KEY:-$HOME/.config/hailstone-release/addon-key.secret}
models=$(cd "${TEXSTUDIO_MODELS:-$HOME/.local/share/hailstone-texpack/models/ncnn}" && pwd)
jobs=${JOBS:-20}
out=$here/release/v$version
tag=v$version

[ -f "$client/src/texpack/texpack.h" ] || { echo "no hailstone-client at $client (set HAILSTONE_CLIENT)" >&2; exit 1; }
[ -x "$sign" ] || { echo "no hailstone_sign at $sign: build the client, or set HAILSTONE_SIGN" >&2; exit 1; }
[ -f "$key" ] || { echo "no add-on key at $key: '$sign keygen $key' once, or set ADDON_KEY" >&2; exit 1; }
ls "$models"/*.param >/dev/null 2>&1 || { echo "no models (.param/.bin) in $models (set TEXSTUDIO_MODELS)" >&2; exit 1; }
grep -q "project(hailstone-texstudio VERSION ${version%%-*} " "$here/CMakeLists.txt" ||
  { echo "CMakeLists.txt's project version is not ${version%%-*}" >&2; exit 1; }
if [ -n "$(git -C "$here" status --porcelain)" ]; then
  echo "the working tree has changes: commit them first, so the release matches a commit" >&2
  exit 1
fi
client_commit=$(git -C "$client" rev-parse --short HEAD)

rm -rf "$here/build-linux" "$here/build-windows" "$out"

# linux-x64, in the Ubuntu 24.04 image; the build folder belongs to the caller, not root. Every folder is mounted at
# its own path, so the build's paths are the host's.
docker run --rm --cpus "$jobs" -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "${here}:${here}" -v "${client}:${client}:ro" -v "${models}:${models}:ro" -w "$here" \
  hailstone-linux-build:24.04 sh -c "
  git config --global --add safe.directory '*' &&
  cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DHAILSTONE_CLIENT_DIR='$client' -DTEXSTUDIO_MODELS='$models' &&
  cmake --build build-linux -j $jobs --target texstudio &&
  strip --strip-unneeded build-linux/dist/$NAME &&
  ctest --test-dir build-linux --output-on-failure"

# windows-x64, MSVC under Wine (the client's tools/docker/msvc_build.sh). It runs inside a timeout: a Wine program
# that cannot start (a command line over 32,767 characters) hangs the build rather than failing it.
docker run --rm --cpus "$jobs" -u "$(id -u):$(id -g)" -e HOME="$HOME/opt/msvc-home" -e MSVC_DIR="$HOME/opt/msvc" \
  -e JOBS="$jobs" -v "${HOME}/opt:${HOME}/opt" -v "${here}:${here}" -v "${client}:${client}:ro" \
  -v "${models}:${models}:ro" -w "$here" hailstone-msvc-build:24.04 \
  timeout 3h "$client/tools/docker/msvc_build.sh" . build-windows -DHAILSTONE_CLIENT_DIR="$client" \
  -DTEXSTUDIO_MODELS="$models"
for f in build-windows/dist/$NAME.exe build-windows/dist/SDL3.dll; do
  [ -f "$here/$f" ] || { echo "the Windows build has no $f" >&2; exit 1; }
done
# (the tests of the Windows build: the host's Wine, as the client's docs/ci.md says, not the image's older one)
if command -v wine >/dev/null; then
  WINEDEBUG=-all wine "$here/build-windows/dist/$NAME.exe" --version | grep -q "Hailstone Texture Studio $version" ||
    { echo "the Windows build does not say it is $version" >&2; exit 1; }
fi

python3 "$client/tools/make_addon.py" --name "$NAME" --kind tool --version "$version" \
    --platform "linux-x64=$here/build-linux/dist" --platform "windows-x64=$here/build-windows/dist" \
    --author "$AUTHOR" --description "$DESCRIPTION" \
    --notes "https://github.com/$RELEASE_REPO/releases/tag/$tag" --out "$out"
"$sign" sign "$key" "$out/hailstone-addon.txt"
"$sign" verify "$("$sign" public "$key")" "$out/hailstone-addon.txt"
ls -l "$out"

if [ "$publish" = "--publish" ]; then
  # The tag goes on the commit built, so push it first.
  gh release create "$tag" -R "$RELEASE_REPO" --target "$(git -C "$here" rev-parse HEAD)" \
     --title "Texture Studio $version" \
     --notes "Install from Hailstone's Add-ons screen: paste https://github.com/$RELEASE_REPO (a Hailstone that installs tool add-ons is needed). Built against hailstone-client $client_commit." \
     "$out"/*
fi
