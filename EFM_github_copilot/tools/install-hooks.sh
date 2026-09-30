#!/bin/sh
# Cai git hooks + commit template cho repo nay. Chay 1 lan sau khi clone:
#   sh tools/install-hooks.sh
set -eu

ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"

chmod +x tools/hooks/*
git config core.hooksPath tools/hooks
git config commit.template .gitmessage

echo "Da cai: core.hooksPath=tools/hooks, commit.template=.gitmessage"
