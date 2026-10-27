#!/bin/bash
# Builds release versions of Septima + Vista Rooms and packages them into
# dist/Rereverb-<version>.pkg.
#
#   ./make_installer.sh                      # plugins only (use this for releases)
#   ./make_installer.sh --with-irs <folder>  # PERSONAL USE: also installs your IRs
#
# The package installs:
#   /Library/Audio/Plug-Ins/VST3/{Septima,Vista Rooms}.vst3
#   /Library/Audio/Plug-Ins/Components/{Septima,Vista Rooms}.component
# and with --with-irs:
#   /Library/Application Support/Rereverb/Septima/IRs/*.wav   (found automatically by Septima)
#
# Never publish a package built with --with-irs: the IR library's licence does
# not allow redistributing its impulse responses.
set -eo pipefail
cd "$(dirname "$0")"
VERSION=$(sed -n 's/^project(Rereverb VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
IR_SRC=""
if [ "$1" = "--with-irs" ]; then
  IR_SRC="$2"
  if [ ! -d "$IR_SRC" ]; then echo "IR folder not found: $IR_SRC"; exit 1; fi
  echo "Including IRs from $IR_SRC (personal package: do not distribute)"
fi
command -v cmake >/dev/null || { echo "Install CMake first: brew install cmake"; exit 1; }
[ -f dependencies/JUCE/CMakeLists.txt ] || git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git dependencies/JUCE

ARCHS="${ARCHS:-arm64;x86_64}"   # universal by default so the package runs on any Mac
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DRR_COPY_AFTER_BUILD=OFF -DRR_LTO="${RR_LTO:-ON}" -DCMAKE_OSX_ARCHITECTURES="$ARCHS"
cmake --build build-release --config Release --parallel
./build-release/rr_tests | grep -E "FAIL|checks"

STAGE=build-release/pkgroot
SCRIPTS=build-release/pkgscripts
rm -rf "$STAGE" "$SCRIPTS"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" "$SCRIPTS"
for P in "Septima" "Vista Rooms"; do
  T=$( [ "$P" = "Septima" ] && echo Septima || echo VistaRooms )
  cp -R "build-release/${T}_artefacts/Release/VST3/$P.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/"
  cp -R "build-release/${T}_artefacts/Release/AU/$P.component" "$STAGE/Library/Audio/Plug-Ins/Components/"
done
if [ -n "$IR_SRC" ]; then
  IR_DST="$STAGE/Library/Application Support/Rereverb/Septima/IRs"
  mkdir -p "$IR_DST"
  # Accept either the quad 48 kHz folder or the whole unzipped download.
  Q=$(find "$IR_SRC" -type d -iname "*quad*48*" | head -1)
  cp "${Q:-$IR_SRC}"/*.wav "$IR_DST/"
  echo "$(ls "$IR_DST" | wc -l | tr -d ' ') impulse responses staged"
fi
PKG_NAME="Rereverb-$VERSION"; [ -n "$IR_SRC" ] && PKG_NAME="$PKG_NAME-personal"

cat > "$SCRIPTS/postinstall" <<'POST'
#!/bin/bash
# Remove per-user development copies so hosts don't see duplicates, then
# refresh the Audio Unit cache.
U=$(stat -f%Su /dev/console)
H=$(dscl . -read /Users/"$U" NFSHomeDirectory | awk '{print $2}')
for P in "Septima" "Vista Rooms"; do
  rm -rf "$H/Library/Audio/Plug-Ins/VST3/$P.vst3" "$H/Library/Audio/Plug-Ins/Components/$P.component"
done
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
POST
chmod +x "$SCRIPTS/postinstall"

mkdir -p dist
pkgbuild --root "$STAGE" --identifier uk.vengeance.rereverb --version "$VERSION" \
         --install-location / --scripts "$SCRIPTS" "dist/$PKG_NAME.pkg"
echo
echo "Package ready: $(pwd)/dist/$PKG_NAME.pkg"
echo "Double-click it to install (asks for your password)."
