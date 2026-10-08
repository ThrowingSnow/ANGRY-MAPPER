#!/usr/bin/env bash
# Builds ANGRY-MAPPER.app on macOS (Apple Silicon or Intel).
#
#   ./scripts/build-macos.sh
#
# Needs Xcode command line tools + Homebrew (for cmake/ninja). GLFW is built from source
# by CMake, Syphon.framework is built from source if it isn't in ~/Library/Frameworks yet.
# Produces dist/ANGRY-MAPPER.app (self-contained, ad-hoc signed) and a zip of it.
# The result runs on macOS 10.13+ (MACOS_MIN to override).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
DIST="$ROOT/dist"
APP="$DIST/ANGRY-MAPPER.app"
ARCH="$(uname -m)"
SYPHON_DIR="$HOME/Library/Frameworks/Syphon.framework"
MACOS_MIN="${MACOS_MIN:-10.13}"
export MACOSX_DEPLOYMENT_TARGET="$MACOS_MIN"

xcode-select -p >/dev/null 2>&1 || { echo "→ Installing Xcode Command Line Tools, re-run afterwards"; xcode-select --install; exit 1; }
command -v brew >/dev/null || { echo "Homebrew missing: https://brew.sh"; exit 1; }

brew install cmake ninja

# --- Syphon.framework ---
if [[ ! -d "$SYPHON_DIR" ]]; then
    TMP="$(mktemp -d)"
    git clone --depth 1 https://github.com/Syphon/Syphon-Framework.git "$TMP/Syphon-Framework"
    xcodebuild -project "$TMP/Syphon-Framework/Syphon.xcodeproj" \
        -target Syphon -configuration Release \
        ARCHS="$ARCH" ONLY_ACTIVE_ARCH=NO \
        MACOSX_DEPLOYMENT_TARGET="$MACOS_MIN" \
        CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" \
        SYMROOT="$TMP/build"
    mkdir -p "$HOME/Library/Frameworks"
    cp -R "$TMP/build/Release/Syphon.framework" "$SYPHON_DIR"
    rm -rf "$TMP"
fi

# --- Compile ---
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN"
cmake --build "$BUILD"

# --- Bundle ---
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BUILD/angry-mapper" "$APP/Contents/MacOS/angry-mapper"
# Syphon's install name is @loader_path/../Frameworks → Contents/Frameworks
cp -R "$SYPHON_DIR" "$APP/Contents/Frameworks/"
[[ -f "$ROOT/assets/icon.png" ]] && cp "$ROOT/assets/icon.png" "$APP/Contents/Resources/"

VERSION="$(sed -n 's/^project(ANGRY-MAPPER VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>ANGRY-MAPPER</string>
    <key>CFBundleDisplayName</key>        <string>ANGRY-MAPPER</string>
    <key>CFBundleIdentifier</key>         <string>ch.throwingsnow.angry-mapper</string>
    <key>CFBundleExecutable</key>         <string>angry-mapper</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleVersion</key>            <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key> <string>${VERSION}</string>
    <key>LSMinimumSystemVersion</key>     <string>${MACOS_MIN}</string>
    <key>NSHighResolutionCapable</key>    <true/>
</dict>
</plist>
PLIST

# Ad-hoc sign (required on Apple Silicon)
codesign --force --deep --sign - "$APP"

# --- Zip ---
ZIP="$DIST/ANGRY-MAPPER-macos-$ARCH.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
echo "Built $APP"
echo "Zipped → $ZIP"
