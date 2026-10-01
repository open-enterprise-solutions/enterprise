# HTTP from a script

**What.** A script calls a web service: `HTTPConnection`, `HTTPRequest`, `HTTPResponse`, the
`HTTPMethod` enumeration, and for `https://` the `SecureConnection` and `CertificateFile` values.

**Why.** A business application exchanges data with systems it does not own: a bank, a tax
service, a marketplace, a courier, another application. Without an HTTP client, every such
exchange needs a program outside the platform.

## The surface

| Value | Made with | Holds / does |
|---|---|---|
| `HTTPConnection` | `(host [, port [, user, password [, timeout]]])` | `Host`, `Port`, `Timeout`, `SecureConnection`; `Get` `Post` `Put` `Patch` `Delete` `Head`, `CallMethod(HTTPMethod.X, request)` |
| `HTTPRequest` | `([resourceAddress [, headers]])` | `ResourceAddress`, `Headers`; body from a string (in a `TextEncoding`) or from `BinaryData`, and back |
| `HTTPResponse` | handed out by a call, never created | `StatusCode`, `Headers`; body as a string or as `BinaryData` |
| `SecureConnection` | `([clientCertificate [, trustedCertificates]])` | `ClientCertificate`, `TrustedCertificates`, `VerifyServerCertificate` |
| `CertificateFile` | `(path [, keyPassword])` | a PEM file: a certificate, or a certificate with its key |

The help corpus (`syntaxHelper/*/http.json`, `tls.json`) has an article for every member. A body
is text or bytes. JSON in it is read and written by [JSONReader / JSONWriter](json.md).

## What it guarantees

- **An answer and a failure are two things.** Any status, 404 and 500 included, is a response.
  Only no answer raises: the name does not resolve, nothing listens, the time ran out, the
  connection was lost. The error names the method, the address and the reason.
- **Time is bounded.** 30 seconds unless said, a day at most. Zero is refused, not read as "forever".
- **The host is a name.** A scheme, a path or a port written into the host is refused with a
  sentence saying where each one goes.
- **A redirect is an answer.** A 3xx comes back with its `Location` header. Whether to follow it
  is the script's decision.
- **Headers are a `Structure`**, so their names ignore case, as HTTP header names do. Headers that
  decide how a message is framed (`Content-Length`, `Transfer-Encoding`, `Host`, `Connection`,
  `Expect`, `Upgrade`, `Proxy-Authorization`) are set by the platform, and a script cannot set them.
- **With a secure connection, the server is checked by default**: the certificate chain, and the
  name the certificate was issued for. The trusted authorities are the operating system's own
  (the Windows certificate store, the macOS keychain, `/etc/ssl/certs`), or the one file named in
  `TrustedCertificates`. `VerifyServerCertificate = False` keeps the encryption and drops the
  check. It is meant for test stands only.
- **The port follows the connection:** 443 when it is secure, 80 when it is not, and exactly the
  given port when one is given.

## Where it stops

- A name that does not exist is only as unknown as the DNS resolver says. Some resolvers answer
  for every name with their own page, so a plain `http://` call to a mistyped host gets a 200.
  Over `https://` such a call is refused.
- A request that names no `User-Agent` goes out with the library's own (`cpp-httplib/<version>`).
- A body is held in memory. A response is not written to a file while it arrives.
- No proxy.
- Outbound calls are not restricted by role. A script may call any address the machine can reach.

## Underneath

The transport is [cpp-httplib](https://github.com/yhirose/cpp-httplib) and the TLS is
[Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6 (the long-term branch). Both are git submodules
pinned to a release (`src/3rdparty/`). Updating either one means moving that pin in a single
commit. TLS is compiled into the whole build, so the same library can also serve `https://`.
