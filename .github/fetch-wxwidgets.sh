#!/usr/bin/env bash
# The build's one submodule, fetched with patience — the only step every job of every workflow starts with.
#
# GitHub answers a clone with HTTP 503 now and then, and git retries a failed submodule only once: on
# 2026-09-17 the macOS test job lost its whole run to `libwebp` (a submodule of wxWidgets) answering 503
# twice within a minute — nothing in the code, and nothing a person could do but press Re-run. So the fetch
# is tried again, with a longer wait each time, before the job gives up.
#
# docs/private/ is deliberately NOT fetched: it is a private submodule the workflow token cannot read.
set -u

attempts=4
for attempt in $(seq 1 "$attempts"); do
  if git submodule update --init --recursive src/3rdparty/wxWidgets; then
    exit 0
  fi
  if [ "$attempt" -lt "$attempts" ]; then
    wait=$((attempt * 30))
    echo "::warning::wxWidgets could not be fetched (attempt $attempt of $attempts) - trying again in $wait s"
    sleep "$wait"
  fi
done

echo "::error::wxWidgets could not be fetched after $attempts attempts"
exit 1
