#!/bin/sh
# Builds Gullay as CLAP, VST3 and AU on this Mac (Apple Silicon and Intel) and installs them:
#   cd path/to/Gullay && sh build-all-mac.sh
# Needs CMake (brew install cmake) and an internet connection the first time (it downloads clap,
# clap-wrapper, the VST3 SDK and Apple's AudioUnitSDK into the build folder).
# Works with full Xcode or only the command line tools (it then builds with Ninja or Make).
set -e
cd "$(dirname "$0")"
HERE="$(pwd)"
command -v cmake >/dev/null 2>&1 || { echo "CMake is missing. Install it with: brew install cmake"; exit 1; }

# Build on the internal disk: signing a bundle on an external drive can fail.
WORK="$HOME/Library/Caches/GullayBuild"
mkdir -p "$WORK"

# The generator: Xcode if it is really there, else Ninja, else plain Makefiles.
GEN=""
if xcodebuild -version >/dev/null 2>&1; then GEN="Xcode"
elif command -v ninja >/dev/null 2>&1; then GEN="Ninja"
else GEN="Unix Makefiles"; fi
# a cache made with another generator cannot be reused
if [ -f "$WORK/CMakeCache.txt" ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=$GEN" "$WORK/CMakeCache.txt"; then rm -rf "$WORK"; mkdir -p "$WORK"; fi
echo "Configuring with $GEN (the first time downloads the SDKs)..."
if [ "$GEN" = "Xcode" ]; then
  cmake -S "$HERE" -B "$WORK" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" >"$WORK/configure.log" 2>&1 || { tail -30 "$WORK/configure.log"; exit 1; }
else
  cmake -S "$HERE" -B "$WORK" -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" >"$WORK/configure.log" 2>&1 || { tail -30 "$WORK/configure.log"; exit 1; }
fi
echo "Building CLAP, VST3 and AU (a few minutes)..."
cmake --build "$WORK" --config Release --parallel >"$WORK/build.log" 2>&1 || { grep -E "error" "$WORK/build.log" | head -30; echo "(the whole log: $WORK/build.log)"; exit 1; }

OUT="$HERE/build/mac-all"
rm -rf "$OUT"; mkdir -p "$OUT"
for ext in clap vst3 component; do
  find "$WORK" -maxdepth 4 -type d -name "Gullay.$ext" -not -path "*/_deps/*" | head -1 | while read -r b; do cp -R "$b" "$OUT/"; done
done
for b in "$OUT"/*; do codesign --force --deep --sign - "$b" >/dev/null 2>&1 || true; done
[ -d "$OUT/Gullay.clap" ] || { echo "No Gullay.clap was made: see $WORK/build.log"; exit 1; }

install_one() { # $1 bundle, $2 folder
  [ -d "$1" ] || return 0
  mkdir -p "$2"; rm -rf "$2/$(basename "$1")"; cp -R "$1" "$2/"
}
install_one "$OUT/Gullay.clap" "$HOME/Library/Audio/Plug-Ins/CLAP"
install_one "$OUT/Gullay.vst3" "$HOME/Library/Audio/Plug-Ins/VST3"
install_one "$OUT/Gullay.component" "$HOME/Library/Audio/Plug-Ins/Components"
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

if [ -d "$OUT/Gullay.component" ]; then
  echo ""
  echo "Checking the AU with Apple's validator..."
  auval -v aumf Gula OtKa | tail -5 || true
fi
echo ""
echo "Installed: $(ls "$OUT" | tr '\n' ' ')"
echo "A copy of each is in build/mac-all. In Bitwig: rescan plug-ins (Settings > Locations), then add Gullay (CLAP)."
