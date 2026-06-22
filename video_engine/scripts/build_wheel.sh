#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$REPO_ROOT"

python -m pip install --upgrade pip setuptools wheel build
python -m build --wheel

echo
echo "Wheel build complete:"
ls -lah "$REPO_ROOT"/dist
