#!/bin/bash
# Builds ICOFormat.plugin (universal arm64+x86_64) from src/ using the Adobe Photoshop SDK sample project.
# Usage: ./build.sh [path/to/pluginsdk]   (default: ~/Dev/Photoshop/photoshopsdk/pluginsdk)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="${1:-$HOME/Dev/Photoshop/photoshopsdk/pluginsdk}"
SAMPLE="$SDK/samplecode/format/simpleformat"
[ -d "$SAMPLE" ] || { echo "SDK sample not found: $SAMPLE" >&2; exit 1; }

# Build tree mirrors the SDK layout so the sample project's relative paths keep working.
T="$HERE/build/tree"
rm -rf "$HERE/build"
mkdir -p "$T/samplecode/format/icoformat/common" "$T/samplecode/format/icoformat/mac"
ln -s "$SDK/samplecode/common" "$T/samplecode/common"
ln -s "$SDK/photoshopapi" "$T/photoshopapi"

cp "$HERE/src/ICOFormat.cpp" "$HERE/src/PiPLs.json" "$T/samplecode/format/icoformat/common/"
P="$T/samplecode/format/icoformat/mac/icoformat.xcodeproj"
cp -R "$SAMPLE/mac/simpleformat.xcodeproj" "$P"

# Drop sources we don't use, rename everything, link zlib.
grep -v -E "SimpleFormat(UI|Scripting|Terminology)|SimpleFormat\.h" "$P/project.pbxproj" \
  | sed -e 's/SimpleFormat/ICOFormat/g' -e 's/simpleformat/icoformat/g' \
        -e 's/ZERO_LINK = NO;/ZERO_LINK = NO; OTHER_LDFLAGS = "-lz";/' > "$P/project.pbxproj.new"
mv "$P/project.pbxproj.new" "$P/project.pbxproj"

xcodebuild -project "$P" -target ICOFormat -configuration Release \
  ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO CODE_SIGN_IDENTITY="-" \
  GCC_TREAT_WARNINGS_AS_ERRORS=NO build 2>&1 | grep -E "error:|BUILD (SUCCEEDED|FAILED)"

OUT="$T/samplecode/Output/Mac/Release/Release/ICOFormat.plugin"
[ -d "$OUT" ] || { echo "Build failed: $OUT missing" >&2; exit 1; }
rm -rf "$HERE/ICOFormat.plugin"
cp -R "$OUT" "$HERE/ICOFormat.plugin"
codesign --force --sign - "$HERE/ICOFormat.plugin"
echo "Built: $HERE/ICOFormat.plugin"
