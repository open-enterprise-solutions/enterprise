#!/usr/bin/env bash
# The packages a Linux job builds with, installed with patience - every job's "Install build dependencies".
# The packages are the caller's: `bash .github/install-packages.sh build-essential cmake ...`.
#
# The runner image adds apt sources of Microsoft's (packages.microsoft.com) beside Ubuntu's, and nothing a
# job here installs comes from them. On 2026-11-27 that host answered `apt-get update` with HTTP 403 and
# the TSan job ended in its second minute, while the same step passed in every other job of the run. So
# those sources are taken out first: the update reads Ubuntu's archive alone. Ubuntu's own mirrors fail
# now and then as well, so the update and the install are tried again, with a longer wait each time, as
# the submodules are (fetch-submodules.sh).
set -u

grep -l 'packages.microsoft.com' /etc/apt/sources.list.d/* 2>/dev/null | xargs -r sudo rm -f --

attempts=4
for attempt in $(seq 1 "$attempts"); do
  if sudo apt-get update && sudo apt-get install -y "$@"; then
    exit 0
  fi
  if [ "$attempt" -lt "$attempts" ]; then
    wait=$((attempt * 30))
    echo "::warning::the packages could not be installed (attempt $attempt of $attempts) - trying again in $wait s"
    sleep "$wait"
  fi
done

echo "::error::the packages could not be installed after $attempts attempts"
exit 1
