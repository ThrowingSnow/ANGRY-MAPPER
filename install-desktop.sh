#!/usr/bin/env bash
# Installs the ANGRY-MAPPER .desktop entry into the user's application launcher.
set -e
DEST="$HOME/.local/share/applications/angry-mapper.desktop"
cp "$(dirname "$0")/angry-mapper.desktop" "$DEST"
update-desktop-database "$HOME/.local/share/applications/" 2>/dev/null || true
echo "Installed → $DEST"
echo "ANGRY-MAPPER should now appear in your application launcher."
