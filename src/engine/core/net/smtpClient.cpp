#include "smtpClient.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <optional>
#include <random>

#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "core/utils/base64.h"   // ibBase64 - a text, an attachment, a header's encoded word, AUTH's words

#include "httpClient.h"   // ibTlsClientContext, ibHttpClientError - a client's TLS, as HTTP makes it
#include "httpStream.h"
#include "tlsStream.h"

namespace {

// What a server's reply may run to - an EHLO lists its extensions, nothing is longer.
constexpr std::size_t kMaxReplyBytes = 64 * 1024;
// A message goes out in pieces, each within the time allowed: a slow line is not a server that does not answer.
constexpr std::size_t kSendPiece = 64 * 1024;
// RFC 5322: a line is at most 998 bytes; an encoded header is folded well inside it, base64 at 76 characters.
constexpr std::size_t kBase64Line = 76;

bool IsAscii(const std::string& text)
{
	for (const unsigned char c : text) {
		if (c >= 0x80)
			return false;
	}
	return true;
}

bool HasLineBreak(const std::string& text)
{
	return text.find_first_of("\r\n") != std::string::npos;
}

std::string Trim(const std::string& text)
{
	const std::size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return std::string();
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string Upper(std::string text)
{
	for (char& c : text) {
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
	}
	return text;
}

std::string Lower(std::string text)
{
	for (char& c : text) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return text;
}

std::string Base64(const std::string& bytes)
{
	std::string out;
	ibBase64::Append(out, bytes.data(), bytes.size());
	return out;
}

// Bytes as base64, in lines of 76 characters, each ended by CRLF.
void AppendBase64Lines(std::string& out, const std::string& bytes)
{
	const std::string encoded = Base64(bytes);
	for (std::size_t at = 0; at < encoded.size(); at += kBase64Line) {
		out.append(encoded, at, kBase64Line);
		out += "\r\n";
	}
}

// How many bytes of `text` from `at` make whole UTF-8 characters and no more than `most`.
std::size_t WholeCharacters(const std::string& text, std::size_t at, std::size_t most)
{
	std::size_t end = std::min(text.size(), at + most);
	// A continuation byte (10xxxxxx) is never where a character starts: step back to one that is.
	while (end > at && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
		--end;
	return end > at ? end - at : std::min(text.size() - at, most);   // a broken sequence is cut where it stands
}

// A HEADER'S TEXT THAT IS NOT ASCII, as encoded words (RFC 2047): UTF-8 in base64, at most 75 characters a word, the
// words on lines of their own - a reader joins them back without the spaces between.
std::string EncodedWords(const std::string& text)
{
	std::string out;
	for (std::size_t at = 0; at < text.size();) {
		const std::size_t take = WholeCharacters(text, at, 45);   // 45 bytes -> 60 characters of base64
		if (!out.empty())
			out += "\r\n ";
		out += "=?UTF-8?B?" + Base64(text.substr(at, take)) + "?=";
		at += take;
	}
	return out;
}

// A header's free text (a subject): as it is when it is short ASCII, encoded words otherwise.
std::string HeaderText(const std::string& text)
{
	if (IsAscii(text) && text.size() < 900)
		return text;
	return EncodedWords(text);
}

// A name in a header's address: a quoted string when ASCII, encoded words when not.
std::string AddressHeader(const std::string& name, const std::string& address)
{
	if (name.empty())
		return address;
	if (!IsAscii(name))
		return EncodedWords(name) + " <" + address + ">";
	std::string quoted = "\"";
	for (const char c : name) {
		if (c == '"' || c == '\\')
			quoted += '\\';
		quoted += c;
	}
	return quoted + "\" <" + address + ">";
}

// A list of addresses, one to a line.
bool AddressList(const std::vector<std::string>& list, std::string& out, std::string& error)
{
	for (const std::string& one : list) {
		std::string name, address;
		if (!ibMailMessage::ParseAddress(one, name, address)) {
			error = "'" + one + "' is not an e-mail address";
			return false;
		}
		if (!out.empty())
			out += ",\r\n ";
		out += AddressHeader(name, address);
	}
	return true;
}

// The type an attachment is announced as, by its name's extension; any other is bytes.
std::string ContentTypeOf(const std::string& name)
{
	static const std::map<std::string, std::string> kTypes = {
		{ "pdf", "application/pdf" }, { "zip", "application/zip" }, { "xml", "application/xml" },
		{ "json", "application/json" }, { "txt", "text/plain" }, { "csv", "text/csv" }, { "htm", "text/html" },
		{ "html", "text/html" }, { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
		{ "gif", "image/gif" }, { "svg", "image/svg+xml" },
		{ "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
		{ "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
		{ "xls", "application/vnd.ms-excel" }, { "doc", "application/msword" },
	};
	const std::size_t dot = name.rfind('.');
	if (dot != std::string::npos) {
		const auto found = kTypes.find(Lower(name.substr(dot + 1)));
		if (found != kTypes.end())
			return found->second;
	}
	return "application/octet-stream";
}

// An ASCII name a parameter can carry quoted as it is.
bool IsPlainName(const std::string& name)
{
	for (const unsigned char c : name) {
		if (c < 0x20 || c >= 0x7F || c == '"' || c == '\\')
			return false;
	}
	return true;
}

// AN ATTACHMENT'S NAME in Content-Disposition: quoted when plain, otherwise RFC 2231 - UTF-8, percent-encoded, cut
// into numbered pieces on lines of their own so that no line runs past the limit.
std::string FileNameParameter(const std::string& name)
{
	if (IsPlainName(name) && name.size() < 900)
		return "filename=\"" + name + "\"";

	static const char digits[] = "0123456789ABCDEF";
	std::vector<std::string> pieces(1);
	for (std::size_t at = 0; at < name.size();) {
		const std::size_t take = WholeCharacters(name, at, 16);
		std::string encoded;
		for (std::size_t i = at; i < at + take; ++i) {
			const unsigned char c = static_cast<unsigned char>(name[i]);
			if (std::isalnum(c) || c == '.' || c == '-' || c == '_')
				encoded += static_cast<char>(c);
			else {
				encoded += '%';
				encoded += digits[c >> 4];
				encoded += digits[c & 0x0F];
			}
		}
		if (pieces.back().size() + encoded.size() > 60)
			pieces.emplace_back();
		pieces.back() += encoded;
		at += take;
	}
	std::string out;
	for (std::size_t n = 0; n < pieces.size(); ++n) {
		if (n > 0)
			out += ";\r\n ";
		out += "filename*" + std::to_string(n) + "*=" + (n == 0 ? "UTF-8''" : "") + pieces[n];
	}
	return out;
}

std::mt19937_64& Random()
{
	static thread_local std::mt19937_64 engine{ std::random_device{}() };
	return engine;
}

std::string RandomHex(int digits)
{
	static const char hex[] = "0123456789abcdef";
	std::string out;
	std::uint64_t word = 0;
	for (int n = 0; n < digits; ++n) {
		if (n % 16 == 0)
			word = Random()();
		out += hex[word & 0x0F];
		word >>= 4;
	}
	return out;
}

// RFC 5322's date, in UTC: `Wed, 25 Nov 2026 14:03:09 +0000`.
std::string DateHeader()
{
	const std::time_t now = std::time(nullptr);
	std::tm utc{};
#ifdef _WIN32
	gmtime_s(&utc, &now);
#else
	gmtime_r(&now, &utc);
#endif
	static const char* const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char* const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	char text[64] = {};
	std::snprintf(text, sizeof(text), "%s, %02d %s %04d %02d:%02d:%02d +0000", days[utc.tm_wday % 7], utc.tm_mday,
		months[utc.tm_mon % 12], utc.tm_year + 1900, utc.tm_hour, utc.tm_min, utc.tm_sec);
	return text;
}

// A part's headers and its bytes in base64.
std::string Base64Part(const std::string& headers, const std::string& bytes)
{
	std::string part = headers + "Content-Transfer-Encoding: base64\r\n\r\n";
	AppendBase64Lines(part, bytes);
	return part;
}

// Parts in a multipart body under `boundary`.
std::string Multipart(const std::string& boundary, const std::vector<std::string>& parts)
{
	std::string body;
	for (const std::string& part : parts)
		body += "--" + boundary + "\r\n" + part + "\r\n";
	body += "--" + boundary + "--\r\n";
	return body;
}

// The text of a message - plain, HTML, or both as alternatives (the plain first, as the reader's program prefers the
// last it can show). Empty when the message has none.
std::string TextPart(const std::string& plain, const std::string& html)
{
	const std::string plainPart = Base64Part("Content-Type: text/plain; charset=utf-8\r\n", plain);
	const std::string htmlPart = Base64Part("Content-Type: text/html; charset=utf-8\r\n", html);
	if (!plain.empty() && !html.empty()) {
		const std::string boundary = "=_alt_" + RandomHex(24);
		return "Content-Type: multipart/alternative; boundary=\"" + boundary + "\"\r\n\r\n"
			+ Multipart(boundary, { plainPart, htmlPart });
	}
	if (!html.empty())
		return htmlPart;
	if (!plain.empty())
		return plainPart;
	return std::string();
}

} // namespace

//*******************************************************************************************
//*                                      The message                                        *
//*******************************************************************************************

bool ibMailMessage::ParseAddress(const std::string& text, std::string& name, std::string& address)
{
	if (HasLineBreak(text))
		return false;
	const std::string whole = Trim(text);
	const std::size_t open = whole.rfind('<');
	if (open != std::string::npos) {
		if (whole.empty() || whole.back() != '>')
			return false;
		address = Trim(whole.substr(open + 1, whole.size() - open - 2));
		name = Trim(whole.substr(0, open));
		if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
			name = name.substr(1, name.size() - 2);
	}
	else {
		address = whole;
		name.clear();
	}
	const std::size_t at = address.rfind('@');
	if (address.empty() || at == std::string::npos || at == 0 || at + 1 == address.size())
		return false;
	for (const unsigned char c : address) {
		if (c <= 0x20 || c == 0x7F || c == '<' || c == '>' || c == '"' || c == ',' || c == ';' || c == '(' || c == ')')
			return false;
	}
	return true;
}

bool ibMailMessage::GetEnvelope(std::string& sender, std::vector<std::string>& recipients, std::string& error) const
{
	std::string name;
	if (Trim(m_from).empty()) {
		error = "the message has no sender (From)";
		return false;
	}
	if (!ParseAddress(m_from, name, sender)) {
		error = "'" + m_from + "' is not an e-mail address";
		return false;
	}
	recipients.clear();
	std::vector<std::string> seen;
	for (const std::vector<std::string>* list : { &m_to, &m_cc, &m_bcc }) {
		for (const std::string& one : *list) {
			std::string address;
			if (!ParseAddress(one, name, address)) {
				error = "'" + one + "' is not an e-mail address";
				return false;
			}
			const std::string key = Lower(address);
			if (std::find(seen.begin(), seen.end(), key) != seen.end())
				continue;
			seen.push_back(key);
			recipients.push_back(address);
		}
	}
	if (recipients.empty()) {
		error = "the message has no recipient (To, Cc or Bcc)";
		return false;
	}
	return true;
}

bool ibMailMessage::Compose(std::string& text, std::string& error) const
{
	std::string sender;
	std::vector<std::string> recipients;
	if (!GetEnvelope(sender, recipients, error))
		return false;
	if (HasLineBreak(m_subject)) {
		error = "the subject holds a line break";
		return false;
	}
	for (const ibMailAttachment& attachment : m_attachments) {
		if (HasLineBreak(attachment.m_name)) {
			error = "the name of an attachment holds a line break";
			return false;
		}
	}

	std::string from, to, cc;
	if (!AddressList({ m_from }, from, error) || !AddressList(m_to, to, error) || !AddressList(m_cc, cc, error))
		return false;

	text = "From: " + from + "\r\n";
	if (!to.empty())
		text += "To: " + to + "\r\n";
	if (!cc.empty())
		text += "Cc: " + cc + "\r\n";
	text += "Subject: " + HeaderText(m_subject) + "\r\n";
	text += "Date: " + DateHeader() + "\r\n";
	text += "Message-ID: <" + RandomHex(32) + "." + sender.substr(sender.rfind('@') + 1) + ">\r\n";
	text += "MIME-Version: 1.0\r\n";

	const std::string textPart = TextPart(m_plain, m_html);
	if (m_attachments.empty()) {
		text += textPart.empty() ? Base64Part("Content-Type: text/plain; charset=utf-8\r\n", std::string()) : textPart;
		return true;
	}

	std::vector<std::string> parts;
	if (!textPart.empty())
		parts.push_back(textPart);
	for (const ibMailAttachment& attachment : m_attachments) {
		const std::string name = attachment.m_name.empty() ? std::string("attachment") : attachment.m_name;
		std::string headers = "Content-Type: " + ContentTypeOf(name);
		if (IsPlainName(name) && name.size() < 900)
			headers += "; name=\"" + name + "\"";
		headers += "\r\nContent-Disposition: attachment; " + FileNameParameter(name) + "\r\n";
		parts.push_back(Base64Part(headers, attachment.m_bytes));
	}
	const std::string boundary = "=_mixed_" + RandomHex(24);
	text += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n\r\n" + Multipart(boundary, parts);
	return true;
}

//*******************************************************************************************
//*                                    The conversation                                     *
//*******************************************************************************************

// ONE CONNECTION'S I/O — on its own context, run by the caller's thread while it waits for one step. Every handler
// writes into what it was started with and nothing else, so a step given up on leaves nothing pointing at it.
struct ibSmtpClientState {

	struct ibStep {
		bool                                       m_done = false;
		std::error_code                            m_error;
		std::size_t                                m_bytes = 0;
		asio::ip::tcp::resolver::results_type      m_found;
	};

	using ibUntil = std::chrono::steady_clock::time_point;

	ibSmtpClientState(const std::string& host, std::uint16_t port) :
		m_host(host), m_port(port), m_resolver(m_io)
	{
	}

	// The step run until it is done or the time is up; false - it is not done.
	bool Run(const std::shared_ptr<ibStep>& step, ibUntil until)
	{
		while (!step->m_done) {
			m_io.restart();
			if (m_io.run_one_until(until) == 0)
				break;
		}
		return step->m_done;
	}

	// THE CONNECTION LET GO — closed, and what it still owes its handlers run here: each writes into its own step.
	void LetGo()
	{
		std::error_code ignored;
		m_resolver.cancel();
		if (m_connecting)
			m_connecting->close(ignored);
		if (m_stream)
			m_stream->Close();
		m_io.restart();
		m_io.poll();
		m_connecting.reset();
		m_stream.reset();
		m_in.clear();
		m_extensions.clear();
		m_loggedOn = false;
	}

	// A step that did not end well: the connection goes with it, and why is said.
	ibSmtpError Fail(ibSmtpError result, std::string why, std::string& error)
	{
		LetGo();
		error = std::move(why);
		return result;
	}

	ibSmtpError Lost(const std::shared_ptr<ibStep>& step, std::string& error)
	{
		if (!step->m_done)
			return Fail(ibSmtpError::Timeout, "nobody answered within the time allowed", error);
		if (step->m_error == asio::error::eof)
			return Fail(ibSmtpError::Lost, "the server closed the connection", error);
		return Fail(ibSmtpError::Lost, "the connection was lost: " + ibErrorText(step->m_error), error);
	}

	ibSmtpError Handshake(ibUntil until, std::string& error)
	{
		auto step = std::make_shared<ibStep>();
		m_stream->Handshake([step](const std::error_code& shaken) { step->m_error = shaken; step->m_done = true; });
		if (!Run(step, until))
			return Fail(ibSmtpError::Timeout, "the TLS handshake was not finished within the time allowed", error);
		if (!step->m_error)
			return ibSmtpError::None;
		std::string why;
		const ibHttpClientError refused = static_cast<const ibTlsStream&>(*m_stream).GetRefusal(m_host, step->m_error, why);
		const ibSmtpError result = refused == ibHttpClientError::Verification ? ibSmtpError::Verification
			: refused == ibHttpClientError::Hostname ? ibSmtpError::Hostname : ibSmtpError::Handshake;
		return Fail(result, std::move(why), error);
	}

	ibSmtpError Connect(ibUntil until, std::string& error)
	{
		auto found = std::make_shared<ibStep>();
		m_resolver.async_resolve(m_host, std::to_string(m_port),
			[found](const std::error_code& resolved, const asio::ip::tcp::resolver::results_type& results) {
				found->m_error = resolved;
				found->m_found = results;
				found->m_done = true;
			});
		if (!Run(found, until))
			return Fail(ibSmtpError::Timeout, "'" + m_host + "' was not found within the time allowed", error);
		if (found->m_error)
			return Fail(ibSmtpError::Connection, "'" + m_host + "' is not found: " + ibErrorText(found->m_error), error);

		auto connected = std::make_shared<ibStep>();
		m_connecting.emplace(m_io);
		asio::async_connect(*m_connecting, found->m_found,
			[connected](const std::error_code& made, const asio::ip::tcp::endpoint&) {
				connected->m_error = made;
				connected->m_done = true;
			});
		const std::string authority = m_host + ":" + std::to_string(m_port);
		if (!Run(connected, until))
			return Fail(ibSmtpError::Timeout, authority + " was not reached within the time allowed", error);
		if (connected->m_error)
			return Fail(ibSmtpError::Connection, authority + " is not reached: " + ibErrorText(connected->m_error), error);

		std::error_code ignored;
		m_connecting->set_option(asio::ip::tcp::no_delay(true), ignored);
		if (m_tls && m_implicit)
			m_stream = std::make_unique<ibTlsStream>(std::move(*m_connecting), m_tls, m_host);
		else
			m_stream = std::make_unique<ibPlainStream>(std::move(*m_connecting));
		m_connecting.reset();
		return m_tls && m_implicit ? Handshake(until, error) : ibSmtpError::None;
	}

	// The bytes are held here until the write is told: a write given up on still has the socket reading them.
	ibSmtpError Write(std::string bytes, ibUntil until, std::string& error)
	{
		m_out = std::move(bytes);
		auto step = std::make_shared<ibStep>();
		m_stream->Write(m_out.data(), m_out.size(), [step](const std::error_code& written, std::size_t sent) {
			step->m_error = written;
			step->m_bytes = sent;
			step->m_done = true;
		});
		if (!Run(step, until) || step->m_error)
			return Lost(step, error);
		return ibSmtpError::None;
	}

	// ONE REPLY — its code, and its lines' words joined (a multi-line reply is `250-…` until `250 …`).
	ibSmtpError Reply(int& code, std::vector<std::string>& lines, ibUntil until, std::string& error)
	{
		lines.clear();
		code = 0;
		for (;;) {
			const std::size_t end = m_in.find('\n');
			if (end == std::string::npos) {
				if (m_in.size() > kMaxReplyBytes)
					return Fail(ibSmtpError::Answer, "the server's reply is longer than " + std::to_string(kMaxReplyBytes) + " bytes", error);
				auto step = std::make_shared<ibStep>();
				m_stream->ReadSome(m_buffer, sizeof(m_buffer), [step](const std::error_code& read, std::size_t bytes) {
					step->m_error = read;
					step->m_bytes = bytes;
					step->m_done = true;
				});
				if (!Run(step, until) || step->m_error)
					return Lost(step, error);
				m_in.append(m_buffer, step->m_bytes);
				continue;
			}
			std::string line = m_in.substr(0, end);
			m_in.erase(0, end + 1);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const bool digits = line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[0]))
				&& std::isdigit(static_cast<unsigned char>(line[1])) && std::isdigit(static_cast<unsigned char>(line[2]));
			if (!digits || (line.size() > 3 && line[3] != ' ' && line[3] != '-'))
				return Fail(ibSmtpError::Answer, "what came back is not SMTP: '" + line.substr(0, 80) + "'", error);
			const int said = std::stoi(line.substr(0, 3));
			if (code != 0 && said != code)
				return Fail(ibSmtpError::Answer, "a reply changed its code midway", error);
			code = said;
			lines.push_back(line.size() > 4 ? line.substr(4) : std::string());
			if (line.size() == 3 || line[3] == ' ')
				return ibSmtpError::None;
		}
	}

	// A COMMAND AND ITS REPLY, the reply's first digit what `expected` is (2 - done, 3 - go on). `shown`: the command as
	// a refusal names it - AUTH's words are never repeated. A refusal keeps the connection: the server said no, and
	// it is still there.
	ibSmtpError Command(const std::string& command, int expected, const std::string& shown, ibUntil until,
		std::string& error, std::vector<std::string>* replied = nullptr)
	{
		ibSmtpError result = Write(command + "\r\n", until, error);
		if (result != ibSmtpError::None)
			return result;
		int code = 0;
		std::vector<std::string> lines;
		result = Reply(code, lines, until, error);
		if (result != ibSmtpError::None)
			return result;
		if (replied != nullptr)
			*replied = lines;
		if (code / 100 != expected) {
			std::string words;
			for (const std::string& one : lines)
				words += (words.empty() ? "" : " ") + one;
			error = shown + ": " + std::to_string(code) + " " + words;
			return ibSmtpError::Refused;
		}
		return ibSmtpError::None;
	}

	// The name the client says in EHLO: its own address, as a literal - it has no name a server could look up.
	std::string ClientName()
	{
		std::error_code failed;
		const asio::ip::tcp::endpoint local = m_stream->GetSocket().local_endpoint(failed);
		if (failed)
			return "[127.0.0.1]";
		const asio::ip::address address = local.address();
		return address.is_v6() ? "[IPv6:" + address.to_string() + "]" : "[" + address.to_string() + "]";
	}

	// EHLO, and what the server says it can do: each extension's keyword, upper case, with its parameters.
	ibSmtpError Hello(ibUntil until, std::string& error)
	{
		m_extensions.clear();
		std::vector<std::string> lines;
		const std::string name = ClientName();
		ibSmtpError result = Command("EHLO " + name, 2, "EHLO", until, error, &lines);
		if (result == ibSmtpError::Refused) {
			// A server from before EHLO: HELO, and no extensions.
			result = Command("HELO " + name, 2, "HELO", until, error);
			return result;
		}
		if (result != ibSmtpError::None)
			return result;
		for (std::size_t n = 1; n < lines.size(); ++n) {
			const std::string line = Trim(lines[n]);
			const std::size_t space = line.find(' ');
			m_extensions[Upper(line.substr(0, space))] = space == std::string::npos ? std::string() : Upper(Trim(line.substr(space + 1)));
		}
		return ibSmtpError::None;
	}

	bool Offers(const std::string& extension) const { return m_extensions.count(extension) != 0; }

	ibSmtpError StartTls(ibUntil until, std::string& error)
	{
		if (!Offers("STARTTLS"))
			return Fail(ibSmtpError::NoStartTls, m_host + " does not offer STARTTLS", error);
		const ibSmtpError result = Command("STARTTLS", 2, "STARTTLS", until, error);
		if (result != ibSmtpError::None)
			return result == ibSmtpError::Refused ? Fail(result, error, error) : result;
		// ⚠ NOTHING READ IN THE CLEAR GOES ON INTO TLS: bytes the server sent after its answer, before the handshake,
		// were sent by whoever stands between the two (a command injected into the conversation).
		if (!m_in.empty())
			return Fail(ibSmtpError::Answer, "the server sent more after its answer to STARTTLS", error);
		asio::ip::tcp::socket socket = std::move(m_stream->GetSocket());
		m_stream = std::make_unique<ibTlsStream>(std::move(socket), m_tls, m_host);
		const ibSmtpError shaken = Handshake(until, error);
		if (shaken != ibSmtpError::None)
			return shaken;
		return Hello(until, error);   // what it offers is asked again: a server tells more over TLS
	}

	ibSmtpError Login(const std::string& user, const std::string& password, ibUntil until, std::string& error)
	{
		const auto found = m_extensions.find("AUTH");
		const std::string mechanisms = found != m_extensions.end() ? " " + found->second + " " : std::string();
		if (mechanisms.find(" PLAIN ") != std::string::npos) {
			const std::string words = std::string(1, '\0') + user + std::string(1, '\0') + password;
			return Command("AUTH PLAIN " + Base64(words), 2, "AUTH PLAIN", until, error);
		}
		if (mechanisms.find(" LOGIN ") != std::string::npos) {
			ibSmtpError result = Command("AUTH LOGIN", 3, "AUTH LOGIN", until, error);
			if (result == ibSmtpError::None)
				result = Command(Base64(user), 3, "AUTH LOGIN (the user)", until, error);
			if (result == ibSmtpError::None)
				result = Command(Base64(password), 2, "AUTH LOGIN (the password)", until, error);
			return result;
		}
		return Fail(ibSmtpError::NoLogin, mechanisms.empty()
			? m_host + " offers no login"
			: m_host + " offers no login this client speaks (it offers " + Trim(mechanisms) + "; PLAIN or LOGIN is spoken)", error);
	}

	// The message as DATA carries it: a line that begins with a dot gets another (RFC 5321, 4.5.2), and the end is a
	// line holding a dot alone.
	static std::string Stuffed(const std::string& text)
	{
		std::string out;
		out.reserve(text.size() + text.size() / 64 + 8);
		bool lineStart = true;
		for (const char c : text) {
			if (lineStart && c == '.')
				out += '.';
			out += c;
			lineStart = c == '\n';
		}
		if (!out.empty() && out.back() != '\n')
			out += "\r\n";
		out += ".\r\n";
		return out;
	}

	asio::io_context                     m_io;
	std::string                          m_host;
	std::uint16_t                        m_port;
	asio::ip::tcp::resolver              m_resolver;
	std::optional<asio::ip::tcp::socket> m_connecting;
	std::unique_ptr<ibHttpStream>        m_stream;       // null - not connected
	std::shared_ptr<const ibTlsContext>  m_tls;          // null - in the clear
	bool                                 m_implicit = false;
	bool                                 m_loggedOn = false;
	std::map<std::string, std::string>   m_extensions;
	std::string                          m_in;           // read and not yet taken as a reply
	std::string                          m_out;          // on the socket now
	char                                 m_buffer[16 * 1024];
};

ibSmtpClient::ibSmtpClient(const std::string& host, std::uint16_t port) :
	m_state(std::make_unique<ibSmtpClientState>(host, port))
{
}

ibSmtpClient::~ibSmtpClient()
{
	m_state->LetGo();
}

ibSmtpError ibSmtpClient::Secure(const std::string& trusted, const std::string& certificate,
	const std::string& password, bool verify, bool implicit, std::string& error)
{
	std::shared_ptr<const ibTlsContext> context;
	switch (ibTlsClientContext(trusted, certificate, password, verify, context, error)) {
	case ibHttpClientError::None:
		break;
	case ibHttpClientError::Certificate:
		return ibSmtpError::Certificate;
	default:
		return ibSmtpError::Trusted;
	}
	// A connection made in the clear is not the one spoken over TLS.
	m_state->LetGo();
	m_state->m_tls = std::move(context);
	m_state->m_implicit = implicit;
	return ibSmtpError::None;
}

ibSmtpError ibSmtpClient::Logon(const std::string& user, const std::string& password, std::chrono::seconds wait,
	std::string& error)
{
	ibSmtpClientState& state = *m_state;
	state.LetGo();
	const auto until = [wait] { return std::chrono::steady_clock::now() + wait; };

	ibSmtpError result = state.Connect(until(), error);
	if (result != ibSmtpError::None)
		return result;

	int code = 0;
	std::vector<std::string> greeting;
	result = state.Reply(code, greeting, until(), error);
	if (result != ibSmtpError::None)
		return result;
	if (code != 220)
		return state.Fail(ibSmtpError::Refused, "the greeting: " + std::to_string(code) + " " + (greeting.empty() ? std::string() : greeting.front()), error);

	result = state.Hello(until(), error);
	if (result == ibSmtpError::None && state.m_tls && !state.m_implicit)
		result = state.StartTls(until(), error);
	if (result == ibSmtpError::None && !user.empty())
		result = state.Login(user, password, until(), error);
	if (result != ibSmtpError::None) {
		state.LetGo();   // a refusal keeps a connection, and nobody is logged on over it
		return result;
	}
	state.m_loggedOn = true;
	return ibSmtpError::None;
}

ibSmtpError ibSmtpClient::Send(const ibMailMessage& message, std::chrono::seconds wait, std::string& error)
{
	ibSmtpClientState& state = *m_state;
	if (!state.m_loggedOn) {
		error = "not logged on";
		return ibSmtpError::NotLoggedOn;
	}
	const auto until = [wait] { return std::chrono::steady_clock::now() + wait; };

	std::string sender, text;
	std::vector<std::string> recipients;
	if (!message.GetEnvelope(sender, recipients, error) || !message.Compose(text, error))
		return ibSmtpError::Message;

	const std::string data = ibSmtpClientState::Stuffed(text);
	const auto size = state.m_extensions.find("SIZE");
	if (size != state.m_extensions.end() && !size->second.empty()) {
		const unsigned long long most = std::strtoull(size->second.c_str(), nullptr, 10);
		if (most > 0 && data.size() > most) {
			error = "the message is " + std::to_string(data.size()) + " bytes, and the server takes at most " + std::to_string(most);
			return ibSmtpError::Message;
		}
	}

	bool international = !IsAscii(sender);
	for (const std::string& one : recipients)
		international = international || !IsAscii(one);
	if (international && !state.Offers("SMTPUTF8")) {
		error = "an address is not ASCII, and the server does not take such addresses (SMTPUTF8)";
		return ibSmtpError::Message;
	}

	// A refusal on the way leaves the server holding half a transaction: RSET clears it, so the next message starts
	// clean. Its own answer does not matter - a connection lost over it is let go, and logs off with it.
	const auto refused = [&state, &until](ibSmtpError result) {
		if (result == ibSmtpError::Refused && state.m_stream) {
			std::string ignored;
			state.Command("RSET", 2, "RSET", until(), ignored);
		}
		return result;
	};

	ibSmtpError result = state.Command("MAIL FROM:<" + sender + ">" + (international ? " SMTPUTF8" : ""), 2,
		"MAIL FROM:<" + sender + ">", until(), error);
	if (result != ibSmtpError::None)
		return refused(result);
	for (const std::string& one : recipients) {
		result = state.Command("RCPT TO:<" + one + ">", 2, "RCPT TO:<" + one + ">", until(), error);
		if (result != ibSmtpError::None)
			return refused(result);
	}
	result = state.Command("DATA", 3, "DATA", until(), error);
	if (result != ibSmtpError::None)
		return refused(result);

	for (std::size_t at = 0; at < data.size(); at += kSendPiece) {
		result = state.Write(data.substr(at, kSendPiece), until(), error);
		if (result != ibSmtpError::None)
			return result;
	}
	int code = 0;
	std::vector<std::string> lines;
	result = state.Reply(code, lines, until(), error);
	if (result != ibSmtpError::None)
		return result;
	if (code / 100 != 2) {
		error = "the message: " + std::to_string(code) + " " + (lines.empty() ? std::string() : lines.front());
		return ibSmtpError::Refused;
	}
	return ibSmtpError::None;
}

void ibSmtpClient::Logoff(std::chrono::seconds wait)
{
	ibSmtpClientState& state = *m_state;
	if (state.m_stream) {
		std::string ignored;
		state.Command("QUIT", 2, "QUIT", std::chrono::steady_clock::now() + wait, ignored);
	}
	state.LetGo();
}

bool ibSmtpClient::IsLoggedOn() const
{
	return m_state->m_loggedOn;
}
