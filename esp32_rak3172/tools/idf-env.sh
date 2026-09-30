#!/usr/bin/env bash
# Init file for the VS Code "ESP-IDF" terminal profile (see .vscode/settings.json).
# Loads the user's normal bash config, then the ESP-IDF environment, so `idf.py`
# works in every new terminal — including terminals used by Copilot Agent.
#
# Requirement per developer: IDF_PATH points to the ESP-IDF v5.5.1 checkout,
# e.g. in ~/.bashrc:  export IDF_PATH="$HOME/esp/esp-idf"

# 1. Normal interactive shell setup (prompt, aliases, PATH, IDF_PATH...)
if [ -f "$HOME/.bashrc" ]; then
    # shellcheck disable=SC1091
    . "$HOME/.bashrc"
fi

# 2. ESP-IDF environment
if [ -z "${IDF_PATH:-}" ]; then
    echo "[idf-env] IDF_PATH is not set. Add 'export IDF_PATH=\$HOME/esp/esp-idf' to ~/.bashrc" >&2
elif [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "[idf-env] $IDF_PATH/export.sh not found — check IDF_PATH" >&2
else
    # export.sh is chatty; keep only errors
    # shellcheck disable=SC1091
    . "$IDF_PATH/export.sh" > /dev/null
    echo "[idf-env] ESP-IDF $(idf.py --version 2>/dev/null) ready"
fi
