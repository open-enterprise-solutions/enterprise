#ifndef __CORE_NET_SMTP_CLIENT_H__
#define __CORE_NET_SMTP_CLIENT_H__

// ⭐ MAIL OUT — a message written as MIME (ibMailMessage) and handed to a mail server over SMTP (ibSmtpClient,
// RFC 5321): the greeting, EHLO, STARTTLS or TLS from the first byte, AUTH, MAIL / RCPT / DATA, QUIT. SYNCHRONOUS, as
// ibHttpClient is: a script waits for its server's answer. Sending only — no POP3, no IMAP.
//
// The text is UTF-8 throughout: a header that is not ASCII goes as an encoded word (RFC 2047), a text and an
// attachment as base64, an attachment's name in RFC 2231's words. The message is 7-bit on the wire, so any server
// carries it.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/core.h"

// ONE ATTACHMENT — its name as the recipient sees it (UTF-8) and its bytes. The type is told by the name's extension.
struct ibMailAttachment {
	std::string m_name;
	std::string m_bytes;
};

// A MESSAGE AS IT IS WRITTEN — the addresses (`address` or `Name <address>`, UTF-8), the subject, the texts and the
// attachments. Bcc goes to the envelope and never into the headers.
class CORE_API ibMailMessage {
public:

	std::string              m_from;
	std::vector<std::string> m_to;
	std::vector<std::string> m_cc;
	std::vector<std::string> m_bcc;
	std::string              m_subject;
	std::string              m_plain;   // the text; empty — none
	std::string              m_html;    // …as HTML; both — the reader's program shows the one it can
	std::vector<ibMailAttachment> m_attachments;

	// THE MESSAGE AFTER DATA — the headers and the MIME body, every line ended by CRLF and none longer than 998 bytes;
	// not dot-stuffed (that is the conversation's). False, with why in `error`, when an address is not one, or a
	// header would carry a line break.
	bool Compose(std::string& text, std::string& error) const;

	// The envelope: the sender's bare address, and every recipient's — To, Cc and Bcc, each once. False, with why in
	// `error`, when one is not an address.
	bool GetEnvelope(std::string& sender, std::vector<std::string>& recipients, std::string& error) const;

	// `Name <address>` or `address` taken apart; false when the address is not one — no `@`, a space, a bracket or a
	// line break in it.
	static bool ParseAddress(const std::string& text, std::string& name, std::string& address);
};

// WHY A MESSAGE WAS NOT SENT — or a client not logged on: what a caller tells a person by, each its own sentence.
enum class ibSmtpError {
	None,
	Trusted,        // the trusted certificates are not readable, or the platform's store gave none
	Certificate,    // the client's certificate or its key is not readable, or the password does not open the key
	Connection,     // the name does not resolve, or nothing listens on the port
	Handshake,      // the server on the port does not speak TLS, or the handshake failed
	Verification,   // the server's certificate is not one the client trusts
	Hostname,       // …or is another host's
	NoStartTls,     // TLS was asked for, and the server does not offer STARTTLS
	NoLogin,        // a user was given, and the server offers no way to log in this client speaks (PLAIN, LOGIN)
	Refused,        // the server answered a command with a refusal — its code and its words in `error`
	Message,        // the message cannot be written (an address that is not one) or is larger than the server takes
	Lost,           // the connection was lost
	Answer,         // what came back is not SMTP
	Timeout,        // nobody answered within the time allowed
	NotLoggedOn,    // Send before Logon, or after Logoff
};

// The socket, its TLS and the conversation — Asio's and Mbed TLS's, kept in the core (smtpClient.cpp).
struct ibSmtpClientState;

// ONE MAIL SERVER, one connection: Logon opens it and says who sends, Send hands it a message — as many as there are —
// and Logoff says goodbye. The caller's thread waits for each answer within the time it allows.
class CORE_API ibSmtpClient {
public:

	ibSmtpClient(const std::string& host, std::uint16_t port);
	~ibSmtpClient();

	// OVER TLS — the words of ibTlsClientContext (httpClient.h). `implicit`: TLS from the first byte (port 465);
	// otherwise the conversation begins in the clear and STARTTLS turns it, before anything else is said.
	ibSmtpError Secure(const std::string& trusted, const std::string& certificate, const std::string& password,
		bool verify, bool implicit, std::string& error);

	// CONNECTED, GREETED, AND LOGGED IN when a user is given (AUTH PLAIN, else LOGIN). `wait` bounds each answer.
	ibSmtpError Logon(const std::string& user, const std::string& password, std::chrono::seconds wait,
		std::string& error);
	// ONE MESSAGE — every recipient taken, or none: a refused one ends it before DATA, and nothing is sent.
	ibSmtpError Send(const ibMailMessage& message, std::chrono::seconds wait, std::string& error);
	// QUIT, and the connection closed. A server that does not answer it is not waited for past `wait`.
	void Logoff(std::chrono::seconds wait);

	bool IsLoggedOn() const;

private:
	std::unique_ptr<ibSmtpClientState> m_state;
};

#endif
