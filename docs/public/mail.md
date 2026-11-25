# Mail from a script

**What.** `InternetMailProfile` says which server and who logs on. `InternetMailMessage` says what goes and to whom.
`InternetMail` sends it: `Logon(profile)`, `Send(message)`, `Logoff()`. Sending only — SMTP; no POP3, no IMAP.

**Why.** An invoice, an act, a report sent from the application to a customer or to the accountant. The conversation
and the MIME are the engine's own (`core/net/smtpClient`), its TLS the one HTTPS speaks.

```
var profile = New InternetMailProfile();
profile.SMTPServerAddress = "smtp.example.com";
profile.SMTPUser = "shop@example.com";
profile.SMTPPassword = password;
profile.SecureConnection = New SecureConnection();   // TLS from the first byte on 465

var message = New InternetMailMessage();
message.From = "Shop <shop@example.com>";
message.To.Add("buyer@example.net");
message.Subject = "Invoice 42";
message.Texts.Add("The invoice is attached.");
message.Attachments.Add("C:\out\invoice-42.pdf");

var mail = New InternetMail();
mail.Logon(profile);
mail.Send(message);
mail.Logoff();
```

## The profile

- `SMTPServerAddress` — a name, no scheme, no port. `SMTPPort` — given, or the security's: **465 with a
  `SecureConnection`, 25 without**.
- `SecureConnection` — the value `HTTPConnection` takes: whom to trust, the certificate to show, whether to check. On
  port 465 TLS starts with the first byte; on any other port (587, 25) the conversation is turned by STARTTLS, and **a
  server that does not offer STARTTLS is refused** — the password is never said in the clear because TLS was missing.
- `SMTPUser` empty — no login (a relay inside the network). Login is AUTH PLAIN, or AUTH LOGIN where PLAIN is not
  offered. `SMTPPassword` is written, never read back, never in a refusal.
- `Timeout` — seconds for each answer, 30 unless said, 1 to a day. The message goes out in 64 KB pieces, each within it.
- Read by `Logon`, and only then.

## The message

- Addresses: `address` or `Name <address>`. `To`, `Cc`, `Bcc` are arrays — `message.To.Add(...)`. An address in more
  than one of them is sent to once. **Bcc is in the envelope, never in the headers.**
- `Texts.Add(text, InternetMailTextType.PlainText | HTML)`. Texts of one kind are joined in the order added; a plain text
  and an HTML one go as alternatives.
- `Attachments.Add(BinaryData | fileName, name)`. **A file is read when added.** Without a name a file goes by its
  own, bytes as `attachment`. The type is told by the name's extension.
- On the wire: MIME, UTF-8; texts and attachments in base64; a header that is not ASCII as encoded words (RFC 2047),
  an attachment's name as RFC 2231. 7-bit, no line past 998 bytes — any server carries it.

## What it guarantees

- **A message goes to every recipient or to none.** A recipient the server refuses ends it before `DATA`; the refusal
  names the recipient and the server's code, and `RSET` clears the half transaction — the connection stays for the next.
- **A line break in a subject or an address is refused**, never sent: it would add a header of its maker's.
- **A failure raises, naming the server and why**: a name that does not resolve, nobody on the port, silence past the
  time allowed, a certificate not trusted or not for the host, a login refused, the server's own words for a refused
  command, a message larger than the server's `SIZE`.
- **Sent after a lost connection, `Send` is refused** until the next `Logon` — never spoken over a socket nobody holds.

## Where it stops

- Sending only. No reading of a mailbox.
- Login by PLAIN and LOGIN; no CRAM-MD5, no OAuth (XOAUTH2).
- An address that is not ASCII needs a server that offers SMTPUTF8; otherwise it is refused.
