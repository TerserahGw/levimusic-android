#!/usr/bin/env bash
# One-time signing setup for LeviMusic Android in Termux.
# Never commit the keystore or password to a public repository.
set -euo pipefail
umask 077

REPO="${LEVIMUSIC_REPOSITORY:-TerserahGw/levimusic-android}"
if ! command -v gh >/dev/null 2>&1; then
  echo "Install GitHub CLI: pkg install gh"
  exit 1
fi
if ! command -v keytool >/dev/null 2>&1; then
  echo "Install Java: pkg install openjdk-17"
  exit 1
fi
if ! command -v openssl >/dev/null 2>&1; then
  echo "Install OpenSSL: pkg install openssl"
  exit 1
fi
gh auth status >/dev/null || { echo "Run gh auth login first."; exit 1; }

STORE="${HOME}/.local/share/levimusic/signing"
KEYSTORE="${STORE}/levimusic-release.jks"
PASSWORD="${STORE}/signing-password"
mkdir -p "$STORE"
chmod 700 "$STORE"
if [[ ! -f "$KEYSTORE" ]]; then
  if [[ -f "$PASSWORD" ]]; then
    echo "Password exists but keystore does not. Recover your backup."
    exit 1
  fi
  openssl rand -hex 32 > "$PASSWORD"
  chmod 600 "$PASSWORD"
  PASS="$(cat "$PASSWORD")"
  keytool -genkeypair -noprompt -keystore "$KEYSTORE" \
      -storetype PKCS12 -alias levimusic \
      -keyalg RSA -keysize 3072 -validity 10000 \
      -storepass "$PASS" -keypass "$PASS" \
      -dname "CN=ReaAurelis, OU=LeviMusic, O=LeviMusic, C=ID"
  chmod 600 "$KEYSTORE"
  echo "New signing key created."
else
  [[ -f "$PASSWORD" ]] || { echo "Missing existing keystore password."; exit 1; }
  echo "Reusing existing release signing key."
fi

echo "Saving secrets to GitHub Actions..."
base64 "$KEYSTORE" | tr -d '\n\r' | gh secret set LEVIMUSIC_KEYSTORE_B64 -R "$REPO"
gh secret set LEVIMUSIC_SIGNING_PASSWORD -R "$REPO" < "$PASSWORD"

echo "Signing configured. Back up both files privately:"
echo "  ${KEYSTORE}"
echo "  ${PASSWORD}"
echo "Never commit, upload publicly or lose these signing files."
echo "You might need one final reinstall to switch away from old debug APK keys."
gh workflow run build.yml -R "$REPO"
echo "Build status: https://github.com/${REPO}/actions"
