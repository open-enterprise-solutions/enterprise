#!/usr/bin/env bash
# Make a fresh Debian 12 / Ubuntu 22.04+ machine the demo server - once, as root, from this folder.
#
# Usage: sudo bash setup.sh <name>     e.g. sudo bash setup.sh 34.82.10.7.sslip.io
#
# <name> is where browsers reach the demo; Caddy obtains its certificate for it, so it must already point at
# this machine (an <ip>.sslip.io name does by itself) and ports 80 and 443 must be open to the world.
#
# What it leaves: the user oes; /opt/oes-demo (bin/, releases/, site/, demo.conf); a 1 GB swap file when the
# machine has none (a 1 GB machine opens a base on it); Caddy; oes-web.service; oes-demo-update.timer - and the
# nightly installed by a first run of the update. Running it again refreshes the scripts, units and Caddyfile.
set -euo pipefail

name="${1:?usage: setup.sh <name the demo is reached by, e.g. 34.82.10.7.sslip.io>}"
# An empty variable in front of the domain gives ".sslip.io" - refused here, not by Caddy after the install.
case "$name" in
  .* | *..* | *' '*) echo "setup.sh: '$name' is not a name - was the address in front of it empty?"; exit 1 ;;
esac
here="$(cd "$(dirname "$0")" && pwd)"
root=/opt/oes-demo

if [ -z "$(swapon --show --noheadings)" ]; then
  fallocate -l 1G /swapfile
  chmod 600 /swapfile
  mkswap /swapfile
  swapon /swapfile
  grep -q '^/swapfile ' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

# Caddy from its own repository: the distributions' are old or absent (Ubuntu 22.04 has none).
if ! command -v caddy > /dev/null; then
  apt-get update
  apt-get install -y curl ca-certificates gnupg debian-keyring debian-archive-keyring apt-transport-https
  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' \
    | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' > /etc/apt/sources.list.d/caddy-stable.list
  chmod o+r /usr/share/keyrings/caddy-stable-archive-keyring.gpg /etc/apt/sources.list.d/caddy-stable.list
  apt-get update
  apt-get install -y caddy
fi

# What the server's programs take from the system besides the C and C++ runtimes: GLib, for wx's base library.
# The nightly is checked against exactly this on a clean Debian 12 (.github/nightly/package-linux.sh).
if ! dpkg -s libglib2.0-0 > /dev/null 2>&1; then
  apt-get update
  apt-get install -y --no-install-recommends libglib2.0-0
fi

id oes > /dev/null 2>&1 || useradd --system --home-dir "$root" --shell /usr/sbin/nologin oes
mkdir -p "$root/bin" "$root/releases" "$root/site"
install -m 755 "$here/oes-demo-update.sh" "$root/bin/oes-demo-update.sh"
cat > "$root/demo.conf" <<CONF
# The demo server's settings, read by oes-demo-update.sh. Written by setup.sh.
HOST=$name
REPO=open-enterprise-solutions/enterprise
TAG=nightly
KEEP=2
CONF
[ -e "$root/site/index.html" ] || echo '<!doctype html><title>OES Enterprise demo</title><p>Starting.</p>' > "$root/site/index.html"
# Root's, all but the releases' own folders (the update gives each to oes): root runs the script and reads the settings
# every hour, so the web server, which strangers log in to, must not be able to rewrite them or their folder.
chown -R root:root "$root/bin" "$root/site"
chown root:root "$root" "$root/releases" "$root/demo.conf"

install -m 644 "$here/oes-web.service" "$here/oes-demo-update.service" "$here/oes-demo-update.timer" /etc/systemd/system/
sed "s/DEMO_HOST/$name/" "$here/Caddyfile" > /etc/caddy/Caddyfile
systemctl daemon-reload
systemctl enable oes-web.service
systemctl restart caddy

"$root/bin/oes-demo-update.sh" || echo "The first update did not take - see above; the timer tries again every hour."
systemctl enable --now oes-demo-update.timer

echo "The demo: https://$name/   journal: journalctl -u oes-web -u oes-demo-update"
