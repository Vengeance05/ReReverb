#!/bin/bash
# Builds Septima and Vista Rooms (AU + VST3), runs the DSP tests, and installs
# the plugins into ~/Library/Audio/Plug-Ins. Usage:  ./build.sh
set -e
cd "$(dirname "$0")"
if ! command -v cmake >/dev/null; then
  echo "CMake not found. Install it with:  brew install cmake"; exit 1
fi
if [ ! -f dependencies/JUCE/CMakeLists.txt ]; then
  echo "Fetching JUCE 8.0.4..."
  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git dependencies/JUCE
fi
cmake -S . -B build-rr -DCMAKE_BUILD_TYPE=Release
cmake --build build-rr --config Release --parallel
./build-rr/rr_tests | tail -1
echo
echo "Installed:"
ls -d ~/Library/Audio/Plug-Ins/VST3/Septima.vst3 ~/Library/Audio/Plug-Ins/VST3/"Vista Rooms.vst3" \
      ~/Library/Audio/Plug-Ins/Components/Septima.component ~/Library/Audio/Plug-Ins/Components/"Vista Rooms.component" 2>/dev/null
echo "Rescan plugins in your DAW (Logic: restart; Cubase: Plug-in Manager > Rescan)."
