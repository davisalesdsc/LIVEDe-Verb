#!/usr/bin/env bash
# Builds a universal macOS installer (.pkg) and a .dmg that contains it.
#
# Usage: packaging/make_installer.sh <artefacts-dir> <version> <output-dir>
#   <artefacts-dir>  e.g. build/AuroraDeVerb_artefacts/Release
#
# Optional signing (Developer ID). Leave unset for ad-hoc signed plugins and an
# unsigned installer:
#   APP_SIGN_ID        "Developer ID Application: Name (TEAMID)"
#   INSTALLER_SIGN_ID  "Developer ID Installer: Name (TEAMID)"
set -euo pipefail

ARTEFACTS="$1"
VERSION="$2"
OUT="$3"
NAME="Aurora DeVerb"
ID_BASE="com.auroraaudio.deverb"
FILE_BASE="AuroraDeVerb-${VERSION}-macOS-universal"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

sign_bundle() {
    local bundle="$1"
    if [[ -n "${APP_SIGN_ID:-}" ]]; then
        codesign --force --deep --timestamp --options runtime --sign "$APP_SIGN_ID" "$bundle"
    else
        codesign --force --deep --sign - "$bundle"
    fi
    codesign --verify --deep --strict "$bundle"
}

# component <format-dir> <bundle-ext> <install-location> <id-suffix>
component() {
    local fmt="$1" ext="$2" location="$3" suffix="$4"
    local bundle="$ARTEFACTS/$fmt/$NAME.$ext"
    [[ -e "$bundle" ]] || { echo "skip $fmt (not built)"; return 0; }

    local root="$WORK/root-$suffix"
    mkdir -p "$root"
    cp -R "$bundle" "$root/"
    sign_bundle "$root/$NAME.$ext"
    if [[ "$fmt" != Standalone ]]; then
        mkdir -p "$WORK/zip/$fmt"
        cp -R "$root/$NAME.$ext" "$WORK/zip/$fmt/"
    fi

    # Keep the installer from "relocating" the bundle to an older copy elsewhere.
    pkgbuild --analyze --root "$root" "$WORK/$suffix.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$suffix.plist"

    pkgbuild --root "$root" \
             --component-plist "$WORK/$suffix.plist" \
             --install-location "$location" \
             --identifier "$ID_BASE.$suffix" \
             --version "$VERSION" \
             "$WORK/pkgs/$suffix.pkg"

    CHOICES+="    <choice id=\"$suffix\" visible=\"true\" title=\"$NAME $fmt\"><pkg-ref id=\"$ID_BASE.$suffix\"/></choice>\n"
    OUTLINE+="        <line choice=\"$suffix\"/>\n"
    REFS+="    <pkg-ref id=\"$ID_BASE.$suffix\" version=\"$VERSION\">$suffix.pkg</pkg-ref>\n"
}

mkdir -p "$WORK/pkgs"
CHOICES="" OUTLINE="" REFS=""
component AU         component "/Library/Audio/Plug-Ins/Components" au
component VST3       vst3      "/Library/Audio/Plug-Ins/VST3"       vst3
component VST        vst       "/Library/Audio/Plug-Ins/VST"        vst
component Standalone app       "/Applications"                      app

# hostArchitectures makes the installer run natively on Apple Silicon (no Rosetta prompt).
cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <options customize="allow" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
$(printf "%b" "$OUTLINE")    </choices-outline>
$(printf "%b" "$CHOICES")$(printf "%b" "$REFS")</installer-gui-script>
EOF

SIGN_ARGS=()
[[ -n "${INSTALLER_SIGN_ID:-}" ]] && SIGN_ARGS=(--sign "$INSTALLER_SIGN_ID" --timestamp)

productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK/pkgs" \
             "${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}" \
             "$OUT/$FILE_BASE.pkg"

# DMG with the installer and a short read-me.
DMGROOT="$WORK/dmg"
mkdir -p "$DMGROOT"
cp "$OUT/$FILE_BASE.pkg" "$DMGROOT/Install $NAME.pkg"
cat > "$DMGROOT/LEIA-ME.txt" <<EOF
$NAME $VERSION - universal (Apple Silicon + Intel)

1. Abra "Install $NAME.pkg".
   Se o macOS bloquear (instalador nao assinado): clique com o botao direito > Abrir,
   ou Ajustes do Sistema > Privacidade e Seguranca > "Abrir Mesmo Assim".
2. Os plugins vao para:
   AU   /Library/Audio/Plug-Ins/Components
   VST3 /Library/Audio/Plug-Ins/VST3
3. Reabra a DAW (no Logic, o AU e validado automaticamente).
EOF
hdiutil create -volname "$NAME $VERSION" -srcfolder "$DMGROOT" -ov -format UDZO "$OUT/$FILE_BASE.dmg"

# Plain zip of the bundles for manual installs.
( cd "$WORK/zip" && zip -qry "$OUT/$FILE_BASE-bundles.zip" . )

echo "Created:"
ls -lh "$OUT"
