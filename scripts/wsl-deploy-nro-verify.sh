#!/usr/bin/env bash
# Deploy an NRO with full-file verification.
#
# Header-only checks are not enough: a half-written file can match the first
# 64 KB and still kill hbloader with an instruction abort. Upload to a temp
# name, download the whole thing again, compare MD5, and only then swap it in.
set -uo pipefail

# Paths (override with environment variables if yours differ)
KODI_ROOT="${KODI_ROOT:-$HOME/kodi-switch}"      # build tree root (kodi/, build-kodi/, package/)
DEV_DIR="${DEV_DIR:-$HOME/kodi-switch}"         # where these scripts live
SRC_DIR="${SRC_DIR:-$KODI_ROOT}"                # project source root (overlay/, docs/)
IP="${1:-}"
if [ -z "$IP" ]; then
  echo "usage: $0 <switch-ip>" >&2
  exit 2
fi
BASE="ftp://$IP:5000"
LOCAL=${2:-${KODI_ROOT}/package/kodi.nro}
TMP=kodi-upload.nro
OUT=${DEV_DIR}/from-switch
mkdir -p "$OUT"

want=$(md5sum "$LOCAL" | cut -d' ' -f1)
echo "== upload $(stat -c %s "$LOCAL") bytes (md5 $want) =="
curl -sS --fail --connect-timeout 8 --max-time 1800 -T "$LOCAL" "$BASE/switch/$TMP" || {
  echo "  upload FAILED"; exit 1; }
echo "  uploaded"

echo "== download back and verify whole file =="
curl -sS --fail --connect-timeout 8 --max-time 1800 -o "$OUT/verify-full.nro" "$BASE/switch/$TMP" || {
  echo "  read-back FAILED"; exit 1; }
got=$(md5sum "$OUT/verify-full.nro" | cut -d' ' -f1)
echo "  card md5:  $got"
echo "  local md5: $want"
if [ "$got" != "$want" ]; then
  echo "  MISMATCH - not replacing kodi.nro"
  curl -s -Q "DELE /switch/$TMP" "$BASE/" >/dev/null 2>&1
  exit 1
fi

echo "== swap in =="
curl -s -Q "DELE /switch/kodi.nro" "$BASE/" >/dev/null 2>&1
curl -s -Q "RNFR /switch/$TMP" -Q "RNTO /switch/kodi.nro" "$BASE/" >/dev/null 2>&1
len=$(curl -sS -I "$BASE/switch/kodi.nro" 2>/dev/null | tr -d '\r' | awk '/Content-Length/ {print $2}')
echo "  kodi.nro now $len bytes"

echo "== clear boot.log =="
curl -s -Q "DELE /switch/kodi/boot.log" "$BASE/switch/kodi/" >/dev/null 2>&1 && echo "  deleted"
