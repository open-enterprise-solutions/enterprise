# The demo server

A Linux machine that runs the latest nightly with the bases of `examples/`, in the browser, and takes the next
nightly by itself.

## What runs

- **`webserver`** of the nightly, on `127.0.0.1:8080`: every base a `Kind=firebird` publication, opened inside it —
  one process, no cluster, no application server. Sized for a 1 GB machine, with a swap file for the peaks.
- **Caddy** on 80 and 443: HTTPS with a certificate it obtains and renews; `/` — the list of bases; the rest — the
  web server.
- **`oes-demo-update.timer`**, every hour: when the tag `nightly` points at a new commit, takes
  `oes-nightly-linux-x64.tar.gz` (or `-arm64`, as the machine is) and `oes-nightly-bases.tar.gz` from that release,
  publishes every base, switches, and checks that each base opened. A nightly that does not open its bases is
  marked `FAILED`; the previous one runs again.

The programs and the bases come from one release, hence one commit: a base is never opened by a build that does not
know it. The bases start afresh with every nightly — what visitors typed is gone the next day.

Not reachable from outside: the cluster's port (7373), MCP (7374, 7600–7699), the designer.

⚠ A visitor's rights are what the base gives: a base without users lets anyone in with all of them, and a client can
hand the server a file to open. The web server runs as `oes`, on the loopback alone, writing only in the releases
(`oes-web.service`), and owns nothing root runs.

## A base in the demo

A folder in `examples/` with an `infobase.conf` and its database. The nightly packs every such folder
(`.github/nightly/package-bases.sh`); its name is the address: `examples/fb_test_buh` → `https://<name>/fb_test_buh/`.
The database is named `sys.fdb`, in lower case: Linux tells the cases apart. A base saved before OES-RPC 1.0 (2026-10-26)
does not open and fails the whole nightly, so it does not carry an `infobase.conf`: `examples/ui-gallery` waits for that.

## Setting it up — Google Cloud, e2-micro

1. **The machine:** Compute Engine → e2-micro in `us-west1`, `us-central1` or `us-east1` (the free tier's
   regions), Debian 12, a 30 GB *standard* persistent disk, *Allow HTTP traffic* and *Allow HTTPS traffic* ticked.
2. **Its address:** reserve the external IP as static (VPC network → IP addresses), or the name changes with it.
   An external IPv4 may be billed — look at the billing a day later, and set a budget alert.
3. **The name:** `<ip>.sslip.io` (e.g. `34.82.10.7.sslip.io`) points at the IP with no registration. A domain of
   your own: an A record, then the same command with that name.
4. **Setup**, on the machine:

   ```sh
   sudo apt-get install -y git
   git clone --depth 1 --branch develop https://github.com/open-enterprise-solutions/enterprise.git
   sudo bash enterprise/tools/demo-server/setup.sh 34.82.10.7.sslip.io
   ```

   The same works on any Debian 12 / Ubuntu 22.04+ machine, x64 or arm64, with 80 and 443 open.

## Looking after it

| | |
|---|---|
| The journal | `journalctl -u oes-web -u oes-demo-update` |
| The nightly running | `cat /opt/oes-demo/current/sha` |
| Take the nightly now, fresh bases | `sudo /opt/oes-demo/bin/oes-demo-update.sh --force` |
| A refused nightly | `/opt/oes-demo/releases/<sha>-<time>/FAILED`; its folder stays until the next nightly |
| Another name | `sudo bash setup.sh <name>` again |

⚠ The nightly's schedule is read from `master` (`.github/workflows/nightly.yml`): a release carries
`oes-nightly-bases.tar.gz` only after `master` has the workflow that packs it. Until then the update says the file
is not in the release yet, and waits.
