#!/usr/bin/env bash
# Install the current DB-IP country database. The router picks up a new file by
# itself within a minute. Run monthly, for example from a systemd timer.
#
# Usage: update-geoip.sh [destination.csv]
#
# DB-IP Lite is CC BY 4.0: anything showing its results publicly must credit
# DB-IP with a link to https://db-ip.com.
set -euo pipefail

DEST="${1:-/etc/npquicrouter/dbip-country-lite.csv}"
TMP="$(mktemp "$DEST.XXXXXX")"
trap 'rm -f "$TMP"' EXIT

# A new month's file can appear some days into the month.
for month in "$(date -u +%Y-%m)" "$(date -u -d "$(date -u +%Y-%m-15) -1 month" +%Y-%m)"; do
  if curl -fsSL "https://download.db-ip.com/free/dbip-country-lite-$month.csv.gz" | gunzip > "$TMP"; then
    lines="$(wc -l < "$TMP")"
    # The real file has several hundred thousand; a short one is a bad download.
    if [[ "$lines" -lt 100000 ]]; then
      echo "Only $lines lines in the $month file, keeping the current database" >&2
      exit 1
    fi
    chmod 644 "$TMP"
    mv "$TMP" "$DEST"
    echo "Installed $month ($lines ranges) at $DEST"
    exit 0
  fi
done
echo "No database could be downloaded, keeping the current one" >&2
exit 1
