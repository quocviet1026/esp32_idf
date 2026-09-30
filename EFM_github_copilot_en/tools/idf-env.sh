#!/usr/bin/env bash
# Init file for the "ESP-IDF" terminal profile (see .vscode/settings.json).
# Loads ~/.bashrc, then loads the ESP-IDF environment, so idf.py works in every new terminal -
# including a terminal opened by Copilot Agent.
#
# Every dev needs: IDF_PATH pointing to an ESP-IDF v5.5.x checkout, e.g. in ~/.bashrc:
#   export IDF_PATH="$HOME/esp/esp-idf"

if [ -f "$HOME/.bashrc" ]; then
    # shellcheck disable=SC1091
    . "$HOME/.bashrc"
fi

if [ -z "${IDF_PATH:-}" ]; then
    echo "[idf-env] IDF_PATH is not set. Add 'export IDF_PATH=\$HOME/esp/esp-idf' to ~/.bashrc" >&2
elif [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "[idf-env] $IDF_PATH/export.sh not found - check IDF_PATH" >&2
else
    # shellcheck disable=SC1091
    . "$IDF_PATH/export.sh" > /dev/null
    echo "[idf-env] $(idf.py --version 2>/dev/null) ready"
fi
