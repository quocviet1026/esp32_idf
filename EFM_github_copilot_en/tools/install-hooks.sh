#!/bin/sh
# Install git hooks + commit template for this repo. Run once after cloning:
#   sh tools/install-hooks.sh
set -eu

ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"

chmod +x tools/hooks/*
git config core.hooksPath tools/hooks
git config commit.template .gitmessage

echo "Installed: core.hooksPath=tools/hooks, commit.template=.gitmessage"
