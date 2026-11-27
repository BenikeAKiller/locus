#!/bin/bash
# Builds dist/Locus-<version>-macOS.pkg from a Release build made with the Xcode generator
# (cmake -S . -B build -G Xcode && cmake --build build --config Release).
# The package installs:
#   /Applications/Locus.app
#   /Library/Audio/Plug-Ins/VST3/Locus Send.vst3
#   /Library/Audio/Plug-Ins/Components/Locus Send.component
# Nothing is signed with a Developer ID; bundles are ad-hoc signed so they run on Apple Silicon.
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build}"
VERSION=$(sed -n 's/^project(Locus VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)

APP="$BUILD_DIR/app/Locus_artefacts/Release/Locus.app"
VST3="$BUILD_DIR/send/LocusSend_artefacts/Release/VST3/Locus Send.vst3"
AU="$BUILD_DIR/send/LocusSend_artefacts/Release/AU/Locus Send.component"

for b in "$APP" "$VST3" "$AU"; do
    [ -d "$b" ] || { echo "missing: $b" >&2; exit 1; }
done

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ROOT="$STAGE/root"
mkdir -p "$ROOT/Applications" "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components"
ditto "$APP"  "$ROOT/Applications/Locus.app"
ditto "$VST3" "$ROOT/Library/Audio/Plug-Ins/VST3/Locus Send.vst3"
ditto "$AU"   "$ROOT/Library/Audio/Plug-Ins/Components/Locus Send.component"

for b in "$ROOT/Applications/Locus.app" \
         "$ROOT/Library/Audio/Plug-Ins/VST3/Locus Send.vst3" \
         "$ROOT/Library/Audio/Plug-Ins/Components/Locus Send.component"; do
    codesign --force --deep --sign - "$b"
done

# Install the bundles exactly at these paths: by default the installer "relocates" a bundle
# to wherever a copy with the same identifier already is (e.g. a build folder).
pkgbuild --analyze --root "$ROOT" "$STAGE/components.plist"
python3 - "$STAGE/components.plist" <<'EOF'
import plistlib, sys
path = sys.argv[1]
with open(path, "rb") as f:
    bundles = plistlib.load(f)
def pin(entries):
    for b in entries:
        b["BundleIsRelocatable"] = False
        pin(b.get("ChildBundles", []))
pin(bundles)
with open(path, "wb") as f:
    plistlib.dump(bundles, f)
print("bundles:", [b["RootRelativeBundlePath"] for b in bundles])
EOF

# After installing, make the Audio Unit visible without logging out.
mkdir -p "$STAGE/scripts"
cat > "$STAGE/scripts/postinstall" <<'EOF'
#!/bin/bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x "$STAGE/scripts/postinstall"

mkdir -p dist
pkgbuild --root "$ROOT" \
         --component-plist "$STAGE/components.plist" \
         --scripts "$STAGE/scripts" \
         --identifier com.locus.Locus.pkg \
         --version "$VERSION" \
         --install-location / \
         "$STAGE/Locus-component.pkg"

productbuild --package "$STAGE/Locus-component.pkg" "dist/Locus-$VERSION-macOS.pkg"
echo "built dist/Locus-$VERSION-macOS.pkg"
pkgutil --payload-files "dist/Locus-$VERSION-macOS.pkg" | grep -E '\.(app|vst3|component)$'
