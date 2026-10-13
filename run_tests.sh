#!/usr/bin/env bash
# Windows/MSVC/Qt reproducible test entrypoint (Git Bash/WSL with Windows pwsh).
set -euo pipefail
cd "$(dirname "$0")"
if command -v pwsh.exe >/dev/null 2>&1; then
  pwsh.exe -NoProfile -ExecutionPolicy Bypass -File ./run_tests.ps1 "$@"
else
  echo 'Run run_tests.ps1 using Windows PowerShell 7 with MSVC and Qt installed.' >&2
  exit 1
fi
