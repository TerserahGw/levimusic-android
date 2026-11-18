#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
if ! command -v git >/dev/null || ! command -v gh >/dev/null; then
  echo 'Install: pkg install git gh'; exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
  echo 'Run gh auth login first'; exit 1
fi
if [ ! -d .git ]; then git init -b main; fi
git add .
if ! git config user.name >/dev/null; then git config user.name 'LeviMusic Developer'; fi
if ! git config user.email >/dev/null; then git config user.email 'levimusic@users.noreply.github.com'; fi
if ! git diff --cached --quiet; then git commit -m 'Initial LeviMusic online music mod'; fi
if git remote get-url origin >/dev/null 2>&1; then
  git push -u origin main
else
  gh repo create levimusic-android --private --source=. --remote=origin --push
fi
echo 'Open your GitHub repository > Actions to see the ARM64 build.'
