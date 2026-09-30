#!/usr/bin/env bash
# Init file cho terminal profile "ESP-IDF" (xem .vscode/settings.json).
# Nap ~/.bashrc roi nap moi truong ESP-IDF, de idf.py chay duoc trong moi terminal moi -
# ke ca terminal do Copilot Agent mo.
#
# Moi dev can: IDF_PATH tro toi ESP-IDF v5.5.x, vd trong ~/.bashrc:
#   export IDF_PATH="$HOME/esp/esp-idf"

if [ -f "$HOME/.bashrc" ]; then
    # shellcheck disable=SC1091
    . "$HOME/.bashrc"
fi

if [ -z "${IDF_PATH:-}" ]; then
    echo "[idf-env] IDF_PATH chua dat. Them 'export IDF_PATH=\$HOME/esp/esp-idf' vao ~/.bashrc" >&2
elif [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "[idf-env] Khong thay $IDF_PATH/export.sh - kiem tra IDF_PATH" >&2
else
    # shellcheck disable=SC1091
    . "$IDF_PATH/export.sh" > /dev/null
    echo "[idf-env] $(idf.py --version 2>/dev/null) ready"
fi
