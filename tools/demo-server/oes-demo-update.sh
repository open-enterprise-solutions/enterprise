#!/usr/bin/env bash
# The demo server's update: when the nightly has moved, take its Linux programs (x64 or arm64, as the machine is)
# and its bases, publish every base, switch to them, and check that each one opened - or go back to the release that
# ran before.
#
# Usage: oes-demo-update.sh [--force]
#   --force  install the nightly again even when it is the one running (fresh copies of the bases), or one that
#            was refused before
#
# Run by oes-demo-update.timer every hour, as root (it restarts oes-web). Settings: /opt/oes-demo/demo.conf.
#
# ⭐ ONE RELEASE, BOTH ARCHIVES. The programs and the bases come from the same nightly, hence from the same commit
# (.github/nightly/package-bases.sh): a base is never opened by a build that does not know it.
# ⭐ A NIGHTLY THAT DOES NOT OPEN ITS BASES DOES NOT REPLACE ONE THAT DID. It is marked FAILED, the previous release
# runs again, and it is not tried again until the next nightly (or --force). Its folder stays for reading.
# ⚠ THE NIGHTLY IS REPLACED, NOT UPDATED: for a minute there is no tag, or no file. That is "not yet" - the next hour
# tries again - never an error.
set -euo pipefail

root=/opt/oes-demo
# shellcheck source=/dev/null
. "$root/demo.conf"   # HOST, REPO, TAG, KEEP
force=false
[ "${1:-}" = "--force" ] && force=true

say() { echo "oes-demo-update: $*"; }

case "$(uname -m)" in
  x86_64)  programs=oes-nightly-linux-x64.tar.gz ;;
  aarch64) programs=oes-nightly-linux-arm64.tar.gz ;;
  *)       say "no nightly is built for $(uname -m)"; exit 1 ;;
esac

sha=$(curl -fsSL "https://api.github.com/repos/$REPO/git/ref/tags/$TAG" 2>/dev/null \
      | sed -n 's/.*"sha": *"\([0-9a-f]\{40\}\)".*/\1/p' | head -n 1) || true
if [ -z "$sha" ]; then
  say "no tag $TAG on $REPO yet - next time"
  exit 0
fi

running=""
[ -e "$root/current/sha" ] && running=$(cat "$root/current/sha")
if ! $force; then
  [ "$sha" != "$running" ] || exit 0
  ! compgen -G "$root/releases/${sha:0:12}-*/FAILED" > /dev/null || exit 0
fi

dir="$root/releases/${sha:0:12}-$(date -u +%Y%m%d%H%M%S)"
mkdir -p "$dir"
url="https://github.com/$REPO/releases/download/$TAG"
for asset in "$programs" oes-nightly-bases.tar.gz; do
  if ! curl -fsSL --retry 3 -o "$dir/$asset" "$url/$asset"; then
    say "$asset is not in the release yet - next time"
    rm -rf "$dir"
    exit 0
  fi
done
tar -xzf "$dir/$programs" -C "$dir"
tar -xzf "$dir/oes-nightly-bases.tar.gz" -C "$dir"
rm -f "$dir"/*.tar.gz
echo "$sha" > "$dir/sha"

bases=()
for conf in "$dir"/bases/*/infobase.conf; do
  [ -e "$conf" ] && bases+=("$(basename "$(dirname "$conf")")")
done
if [ ${#bases[@]} -eq 0 ]; then
  say "the nightly ${sha:0:8} holds no base - kept the one running"
  touch "$dir/FAILED"
  exit 1
fi

# The web server opens every base inside itself, on the loopback; Caddy is the door to the world. Behind it a
# browser's Origin is https://HOST while the server sees plain HTTP - so the origin is named.
{
  echo "; Written by oes-demo-update.sh for ${sha:0:8} - lost at the next update."
  echo "Host = 127.0.0.1"
  echo "HttpPort = 8080"
  echo "Origins = https://$HOST"
  for base in "${bases[@]}"; do
    printf '\n[%s]\nKind = firebird\nDirectory = %s\n' "$base" "$dir/bases/$base"
  done
} > "$dir/oes/web/web.conf"
chown -R oes:oes "$dir"

previous=""
[ -e "$root/current" ] && previous=$(readlink -f "$root/current")
ln -sfn "$dir" "$root/current"
systemctl restart oes-web.service

# Every base answers at /<name> with its redirect: the web server mounts only the bases it opened.
opened() {
  local base code
  for base in "${bases[@]}"; do
    code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:8080/$base") || code=000
    [ "$code" = "301" ] || return 1
  done
}
ok=false
for _ in $(seq 1 30); do
  if opened; then ok=true; break; fi
  sleep 2
done

if ! $ok; then
  touch "$dir/FAILED"
  say "the nightly ${sha:0:8} did not open its bases (${bases[*]}) - journalctl -u oes-web; its folder: $dir"
  if [ -n "$previous" ] && [ "$previous" != "$dir" ]; then
    ln -sfn "$previous" "$root/current"
    systemctl restart oes-web.service
    say "back to ${previous##*/}"
  fi
  exit 1
fi

# The list at / - what Caddy shows before any base is chosen.
mkdir -p "$root/site"
{
  echo '<!doctype html><html lang="en"><head><meta charset="utf-8">'
  echo '<meta name="viewport" content="width=device-width, initial-scale=1"><title>OES Enterprise demo</title></head>'
  echo "<body><h1>OES Enterprise demo</h1><p>Nightly ${sha:0:8}. The bases start afresh with every nightly.</p><ul>"
  for base in "${bases[@]}"; do
    echo "<li><a href=\"/$base/\">$base</a></li>"
  done
  echo '</ul></body></html>'
} > "$root/site/index.html.new"
mv -f "$root/site/index.html.new" "$root/site/index.html"

# Keep the one running and KEEP-1 before it; a refused one only while it is still the nightly (it says why).
current=$(readlink -f "$root/current")
kept=0
for old in $(ls -1dt "$root"/releases/*/ 2>/dev/null); do
  old=${old%/}
  [ "$old" != "$current" ] || continue
  if [ -e "$old/FAILED" ]; then
    [ "$(cat "$old/sha" 2>/dev/null)" = "$sha" ] || rm -rf "$old"
    continue
  fi
  kept=$((kept + 1))
  [ "$kept" -lt "$KEEP" ] || rm -rf "$old"
done

say "running ${sha:0:8}: ${bases[*]}"
