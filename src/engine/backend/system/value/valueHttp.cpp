#include "valueHttp.h"

#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"            // ConvertToEnumValue<> is declared in value.h and DEFINED here
#include "backend/system/systemManagerEnum.h"
#include "backend/system/value/valueBinaryData.h"
#include "backend/system/value/valueTextFile.h"   // ibCreateTextConv - the one door from a TextEncoding to a converter

#include "core/net/httpClient.h"                   // the transport - the core's, with its TLS
#include "core/utils/base64.h"                     // ibBase64 - the user and password of Basic, in bytes

#include <wx/ffile.h>
#include <wx/log.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

// Plain or secure: one client, made over TLS by Secure before its first request.
struct ibValueHttpConnection::ibTransport {
	ibTransport(const std::string& host, std::uint16_t port) : m_client(host, port) {}
	ibHttpClient m_client;

	// ⚠ WHAT IT WAS BUILT FROM, so that it can be asked whether it still answers the question. A
	// SecureConnection is a VALUE a script holds: `secure.VerifyServerCertificate = True` after a request
	// has been made changes the object the connection points at, and a connection that read it once would
	// go on for the rest of the session with the check it started with - off. Kept alive by keep-alive,
	// which is to say for as long as the session.
	bool     m_secure = false;
	bool     m_verify = true;
	wxString m_client_certificate, m_client_password, m_trusted;
};

namespace {

const int kDefaultPort = 80;
const int kDefaultSecurePort = 443;
const int kDefaultTimeout = 30;
const int kMaxTimeout = 24 * 60 * 60;              // a day - "never forever" is not a number of seconds either
const size_t kMostPemBytes = 16u * 1024u * 1024u;  // a bundle of every authority there is is a fraction of it

// The fields a script may NOT set: they say how the message is framed and carried, and that is the engine's
// (a Content-Length that is not the body's length is a request smuggled past a proxy). Refused by name.
const ibChar* const kOwnedHeaders[] = {
	wxT("Content-Length"), wxT("Transfer-Encoding"), wxT("Host"), wxT("Connection"), wxT("Expect"),
	wxT("Proxy-Authorization"), wxT("Upgrade") };

std::string ibHttpUtf8(const wxString& text)
{
	const wxScopedCharBuffer utf8 = text.utf8_str();
	return std::string(utf8.data(), utf8.length());
}

// text -> bytes. No byte-order mark: a body is not a file, and a mark in front of JSON is a parse error at the
// other end.
std::string ibHttpEncode(const wxString& text, ibTextEncoding encoding)
{
	if (text.empty())
		return std::string();
	if (encoding == ibTextEncoding_UTF8)
		return ibHttpUtf8(text);
	const std::unique_ptr<wxMBConv> conv = ibCreateTextConv(encoding);
	const wxScopedCharBuffer bytes = text.mb_str(*conv);
	if (bytes.length() == 0)
		ibBackendCoreException::Error(_("HTTPRequest: the text cannot be written in this encoding"));
	return std::string(bytes.data(), bytes.length());
}

// bytes -> text, or a refusal: bytes that are not text in this encoding are not handed over as something else.
// The same door a TextReader reads a file through, so a mark at the head of a body is a mark there too.
wxString ibHttpDecode(const std::string& body, ibTextEncoding encoding, const ibChar* who)
{
	wxString text;
	if (!ibTextFromBytes(body.data(), body.size(), encoding, text))
		ibBackendCoreException::Error(_("%s: the body is not text in this encoding - take it with GetBodyAsBinaryData()"), wxString(who));
	return text;
}

// ⚠ WHETHER AN ARGUMENT WAS GIVEN IS ASKED OF ITS TYPE, NOT OF ITS EMPTINESS. To this engine a zero is an empty
// number, so `IsEmpty()` on a time allowed of 0 said "left out" and the default was quietly taken in its place -
// for the one value that has to be refused.
bool ibHttpGiven(ibValue** paParams, const long lSizeArray, const long at)
{
	return lSizeArray > at && paParams[at]->GetType() != ibValueTypes::TYPE_EMPTY;
}

// The object a value stands for, if it is a T. Asked through GetRef() - which answers the value itself when it
// refers to nothing - because ibValuePtr<T>(const ibValue&) reads the reference out of a union it shares with a
// string's pointer, and a string handed to it is a cast over somebody else's memory.
template <class T>
T* ibHttpHeld(const ibValue& value)
{
	return dynamic_cast<T*>(value.GetRef());
}

// The optional TextEncoding argument at `at`: UTF-8 when it is left out, and nothing but a TextEncoding otherwise.
ibTextEncoding ibHttpEncodingAt(ibValue** paParams, const long lSizeArray, const long at)
{
	if (!ibHttpGiven(paParams, lSizeArray, at))
		return ibTextEncoding_UTF8;
	if (paParams[at]->GetType() != ibValueTypes::TYPE_ENUM)
		ibBackendCoreException::Error(_("the encoding is a TextEncoding value - TextEncoding.UTF8, for one"));
	return paParams[at]->ConvertToEnumValue<ibTextEncoding>();
}

bool ibHttpHoldsControl(const wxString& text)
{
	for (size_t i = 0; i < text.length(); i++) {
		const wxUniChar::value_type c = text[i].GetValue();
		if (c < 0x20 || c == 0x7F)
			return true;
	}
	return false;
}

bool ibHttpIsAscii(const wxString& text)
{
	for (size_t i = 0; i < text.length(); i++) {
		if (text[i].GetValue() > 0x7E)
			return false;
	}
	return true;
}

// Whitespace around a header's value carries no meaning (RFC 9110, 5.5). Taken off here, so that "Bearer token "
// pasted with its trailing space goes out as the token.
wxString ibHttpTrimmed(const wxString& value)
{
	size_t from = 0, to = value.length();
	while (from < to && (value[from] == wxT(' ') || value[from] == wxT('\t'))) from++;
	while (to > from && (value[to - 1] == wxT(' ') || value[to - 1] == wxT('\t'))) to--;
	return value.Mid(from, to - from);
}

// RFC 9110: a field name is a token.
bool ibHttpIsToken(const std::string& name)
{
	if (name.empty())
		return false;
	static const std::string marks("!#$%&'*+-.^_`|~");
	for (const char c : name) {
		const bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		if (!alnum && marks.find(c) == std::string::npos)
			return false;
	}
	return true;
}

// What cannot stand in a request target goes out as %XX of its UTF-8; what is already there - a '%', a '?',
// an '&' - is the script's and stays: an address is encoded once and by one rule.
std::string ibHttpTarget(const wxString& resource)
{
	static const char digits[] = "0123456789ABCDEF";
	const std::string utf8 = ibHttpUtf8(resource);
	std::string target;
	target.reserve(utf8.size());
	for (const char byte : utf8) {
		const unsigned char c = static_cast<unsigned char>(byte);
		if (c > 0x20 && c < 0x7F) {
			target += byte;
			continue;
		}
		target += '%';
		target += digits[c >> 4];
		target += digits[c & 0x0F];
	}
	return target;
}

const char* ibHttpMethodName(ibHttpMethod method)
{
	switch (method) {
	case ibHttpMethod_Get: return "GET";
	case ibHttpMethod_Post: return "POST";
	case ibHttpMethod_Put: return "PUT";
	case ibHttpMethod_Patch: return "PATCH";
	case ibHttpMethod_Delete: return "DELETE";
	case ibHttpMethod_Head: return "HEAD";
	case ibHttpMethod_Options: return "OPTIONS";
	}
	return nullptr;
}

// A certificate file's text - read here, by its name in whatever script, and handed to the client as text; false
// when it cannot be read.
bool ibHttpReadPem(const wxString& path, std::string& text)
{
	// wxFFile says a file it cannot open through wxLog, which in the designer is a pane that pops up; the refusal
	// is the caller's sentence.
	wxLogNull quiet;
	wxFFile in(path, wxT("rb"));
	const wxFileOffset length = in.IsOpened() ? in.Length() : wxInvalidOffset;
	if (length < 0 || static_cast<unsigned long long>(length) > kMostPemBytes)
		return false;
	text.resize(static_cast<size_t>(length));
	return length == 0 || in.Read(&text[0], text.size()) == text.size();
}

} // namespace

//////////////////////////////////////////////////////////////////////
// HTTPRequest
//////////////////////////////////////////////////////////////////////

// The order here is the order of ::Prop and ::Func - the number is the index into each table.
void ibValueHttpRequest_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("HTTPRequest()"));
	helper.AppendConstructor(1, wxT("HTTPRequest(resourceAddress : string)"));
	helper.AppendConstructor(2, wxT("HTTPRequest(resourceAddress : string, headers : structure)"));

	helper.AppendProp(wxT("ResourceAddress"), true, true, wxNOT_FOUND);
	helper.AppendProp(wxT("Headers"), true, true, wxNOT_FOUND);

	helper.AppendFunc(wxT("SetBodyFromString"), 2, wxT("SetBodyFromString(text : string, encoding : TextEncoding = UTF8)"));
	helper.AppendFunc(wxT("SetBodyFromBinaryData"), 1, wxT("SetBodyFromBinaryData(data : binaryData)"));
	helper.AppendFunc(wxT("GetBodyAsString"), 1, wxT("GetBodyAsString(encoding : TextEncoding = UTF8)"));
	helper.AppendFunc(wxT("GetBodyAsBinaryData"), wxT("GetBodyAsBinaryData()"));
}

ibValueHttpRequest::ibValueHttpRequest()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_resource(wxT("/")), m_headers(new ibValueStructure())
{
}

ibValueHttpRequest::~ibValueHttpRequest() {}

bool ibValueHttpRequest::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray > 0)
		SetResourceAddress(paParams[0]->GetString());
	if (ibHttpGiven(paParams, lSizeArray, 1))
		SetHeaders(*paParams[1]);
	return true;
}

void ibValueHttpRequest::SetResourceAddress(const wxString& resource)
{
	if (resource.empty()) {
		m_resource = wxT("/");
		return;
	}
	if (resource[0] != wxT('/'))
		ibBackendCoreException::Error(_("HTTPRequest: a resource address starts with '/' - the host and the port are the connection's: '%s'"), resource);
	if (ibHttpHoldsControl(resource))
		ibBackendCoreException::Error(_("HTTPRequest: a resource address holds no line breaks and no control characters"));
	if (resource.Contains(wxT("#")))
		ibBackendCoreException::Error(_("HTTPRequest: a fragment (#...) is never sent to a server - leave it out, or spell a '#' in a query as %%23: '%s'"), resource);
	m_resource = resource;
}

void ibValueHttpRequest::SetHeaders(const ibValue& headers)
{
	ibValueStructure* const given = ibHttpHeld<ibValueStructure>(headers);
	if (given == nullptr)
		ibBackendCoreException::Error(_("HTTPRequest: the headers are a Structure - a name and its value"));
	m_headers = given;                                         // shared with whoever gave it, as any value assigned is
}

void ibValueHttpRequest::SetBodyText(const wxString& text, ibTextEncoding encoding)
{
	m_body = ibHttpEncode(text, encoding);
}

void ibValueHttpRequest::SetBodyBytes(const void* bytes, size_t length)
{
	m_body.assign(static_cast<const char*>(bytes), length);
}

bool ibValueHttpRequest::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enResourceAddress:
		pvarPropVal = m_resource;
		return true;
	case enHeaders:
		pvarPropVal = m_headers;
		return true;
	}
	return false;
}

bool ibValueHttpRequest::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case enResourceAddress:
		SetResourceAddress(varPropVal.GetString());
		return true;
	case enHeaders:
		SetHeaders(varPropVal);
		return true;
	}
	return false;
}

bool ibValueHttpRequest::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enSetBodyFromString:
		if (lSizeArray < 1)
			return false;
		SetBodyText(paParams[0]->GetString(), ibHttpEncodingAt(paParams, lSizeArray, 1));
		return true;
	case enSetBodyFromBinaryData: {
		if (lSizeArray < 1)
			return false;
		const ibValueBinaryData* const data = ibHttpHeld<ibValueBinaryData>(*paParams[0]);
		if (data == nullptr)
			ibBackendCoreException::Error(_("HTTPRequest: SetBodyFromBinaryData takes a BinaryData"));
		SetBodyBytes(data->GetBuffer().GetData(), data->GetLength());
		return true;
	}
	case enGetBodyAsString:
		pvarRetValue = ibHttpDecode(m_body, ibHttpEncodingAt(paParams, lSizeArray, 0), wxT("HTTPRequest"));
		return true;
	case enGetBodyAsBinaryData: {
		const ibValuePtr<ibValueBinaryData> data(new ibValueBinaryData(m_body.data(), m_body.size()));
		pvarRetValue = data;
		return true;
	}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// HTTPResponse
//////////////////////////////////////////////////////////////////////

void ibValueHttpResponse_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("StatusCode"), true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Headers"), true, false, wxNOT_FOUND);

	helper.AppendFunc(wxT("GetBodyAsString"), 1, wxT("GetBodyAsString(encoding : TextEncoding = UTF8)"));
	helper.AppendFunc(wxT("GetBodyAsBinaryData"), wxT("GetBodyAsBinaryData()"));
}

ibValueHttpResponse::ibValueHttpResponse()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_headers(new ibValueStructure())
{
}

ibValueHttpResponse::ibValueHttpResponse(int status, const ibValuePtr<ibValueStructure>& headers, std::string body)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_status(status), m_headers(headers), m_body(std::move(body))
{
}

ibValueHttpResponse::~ibValueHttpResponse() {}

wxString ibValueHttpResponse::BodyAsString(ibTextEncoding encoding) const
{
	return ibHttpDecode(m_body, encoding, wxT("HTTPResponse"));
}

bool ibValueHttpResponse::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enStatusCode:
		pvarPropVal = m_status;
		return true;
	case enHeaders:
		pvarPropVal = m_headers;
		return true;
	}
	return false;
}

bool ibValueHttpResponse::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enGetBodyAsString:
		pvarRetValue = BodyAsString(ibHttpEncodingAt(paParams, lSizeArray, 0));
		return true;
	case enGetBodyAsBinaryData: {
		const ibValuePtr<ibValueBinaryData> data(new ibValueBinaryData(m_body.data(), m_body.size()));
		pvarRetValue = data;
		return true;
	}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// HTTPConnection
//////////////////////////////////////////////////////////////////////

void ibValueHttpConnection_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("HTTPConnection(host : string)"));
	helper.AppendConstructor(2, wxT("HTTPConnection(host : string, port : number)"));
	helper.AppendConstructor(4, wxT("HTTPConnection(host : string, port : number, user : string, password : string)"));
	helper.AppendConstructor(5, wxT("HTTPConnection(host : string, port : number, user : string, password : string, timeout : number)"));

	helper.AppendProp(wxT("Host"), true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Port"), true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Timeout"), true, true, wxNOT_FOUND);
	helper.AppendProp(wxT("SecureConnection"), true, true, wxNOT_FOUND);

	helper.AppendFunc(wxT("Get"), 1, wxT("Get(request : HTTPRequest)"));
	helper.AppendFunc(wxT("Post"), 1, wxT("Post(request : HTTPRequest)"));
	helper.AppendFunc(wxT("Put"), 1, wxT("Put(request : HTTPRequest)"));
	helper.AppendFunc(wxT("Patch"), 1, wxT("Patch(request : HTTPRequest)"));
	helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(request : HTTPRequest)"));
	helper.AppendFunc(wxT("Head"), 1, wxT("Head(request : HTTPRequest)"));
	helper.AppendFunc(wxT("CallMethod"), 2, wxT("CallMethod(method : HTTPMethod, request : HTTPRequest)"));
}

ibValueHttpConnection::ibValueHttpConnection()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true)
{
}

// Out of line: the transport's type is complete only here.
ibValueHttpConnection::~ibValueHttpConnection() {}

bool ibValueHttpConnection::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	const bool portGiven = ibHttpGiven(paParams, lSizeArray, 1);
	const int port = portGiven ? static_cast<int>(paParams[1]->GetInteger()) : kDefaultPort;
	const wxString user = lSizeArray > 2 ? paParams[2]->GetString() : ibString();
	const wxString password = lSizeArray > 3 ? paParams[3]->GetString() : ibString();
	const int timeout = ibHttpGiven(paParams, lSizeArray, 4) ? static_cast<int>(paParams[4]->GetInteger()) : kDefaultTimeout;
	Open(paParams[0]->GetString(), port, user, password, timeout, portGiven);
	return true;
}

void ibValueHttpConnection::Open(const wxString& host, int port, const wxString& user, const wxString& password, int timeoutSeconds, bool portGiven)
{
	if (host.empty() || host.Contains(wxT("://")) || host.Contains(wxT("/")) || host.Contains(wxT("\\")) || host.Contains(wxT("@"))
		|| host.Contains(wxT("?")) || host.Contains(wxT("#")) || host.Contains(wxT(" ")) || ibHttpHoldsControl(host))
		ibBackendCoreException::Error(_("HTTPConnection: the host is a name - 'api.example' - not an address: the path goes into the request's ResourceAddress, and 'https' is the SecureConnection property: '%s'"), host);
	if (!ibHttpIsAscii(host))
		ibBackendCoreException::Error(_("HTTPConnection: a host name is given in its ASCII form - an international name as its punycode (xn--...): '%s'"), host);
	// A ':' is a port unless the host is an IPv6 address, which has at least two; the brackets an address
	// wears in a URL are the URL's, not the name's (the library puts them on the Host header itself).
	if (host.Contains(wxT("[")) || host.Contains(wxT("]")) || host.Freq(wxT(':')) == 1)
		ibBackendCoreException::Error(_("HTTPConnection: the port is the second argument, not a part of the host, and an IPv6 address is given bare - '::1', not '[::1]': '%s'"), host);
	if (user.Contains(wxT(":")))
		ibBackendCoreException::Error(_("HTTPConnection: a user name for Basic authorization cannot hold ':' - that is what parts it from the password"));
	if (port < 1 || port > 65535)
		ibBackendCoreException::Error(_("HTTPConnection: the port is 1 to 65535, and %d was given"), port);
	SetTimeout(timeoutSeconds);

	m_host = host;
	m_port = port;
	m_portGiven = portGiven;
	m_user = user;
	m_password = password;
	m_transport.reset();                                       // made on the first request, for this host and port
}

int ibValueHttpConnection::Port() const
{
	return m_portGiven ? m_port : (m_secure != nullptr ? kDefaultSecurePort : kDefaultPort);
}

void ibValueHttpConnection::SetSecureConnection(const ibValue& value)
{
	if (value.GetType() == ibValueTypes::TYPE_EMPTY) {
		m_secure = nullptr;
	}
	else {
		ibValueSecureConnection* const secure = dynamic_cast<ibValueSecureConnection*>(value.GetRef());
		if (secure == nullptr)
			ibBackendCoreException::Error(_("HTTPConnection: SecureConnection is a SecureConnection value, or Undefined for plain http://"));
		m_secure = secure;
	}
	m_transport.reset();                                       // the next request speaks the new way
}

void ibValueHttpConnection::SetTimeout(int seconds)
{
	if (seconds < 1)
		ibBackendCoreException::Error(_("HTTPConnection: the time allowed is a number of seconds, 1 or more - never 'forever': %d was given"), seconds);
	if (seconds > kMaxTimeout)
		ibBackendCoreException::Error(_("HTTPConnection: the time allowed is at most %d s, a day: %d was given"), kMaxTimeout, seconds);
	m_timeout = seconds;
	m_transport.reset();                                       // the next request is made with the new bound
}

// ONE ROAD FOR EVERY METHOD: the client's Send. Get / Post / Put and the rest differ in a word.
ibValue ibValueHttpConnection::Send(ibHttpMethod method, const ibValueHttpRequest& request)
{
	const char* const name = ibHttpMethodName(method);
	if (name == nullptr)
		ibBackendCoreException::Error(_("HTTPConnection: the method is an HTTPMethod value - HTTPMethod.Get, for one"));
	if (m_host.empty())
		ibBackendCoreException::Error(_("HTTPConnection: no host is set"));

	ibHttpRequest message;
	message.m_method = name;
	const std::string target = ibHttpTarget(request.ResourceAddress());
	const size_t question = target.find('?');
	message.m_path = target.substr(0, question);
	if (question != std::string::npos)
		message.m_query = target.substr(question + 1);
	message.m_body = request.Body();

	// ⚠ A HEADER THAT WOULD BREAK THE MESSAGE IS REFUSED HERE, BY NAME - a script that filled a Structure from a
	// file needs to know WHICH entry it was.
	bool authorized = false;
	for (const auto& entry : request.Headers().Entries()) {
		const wxString key = entry.key.GetString();
		const std::string field = ibHttpUtf8(key);
		if (!ibHttpIsToken(field))
			ibBackendCoreException::Error(_("HTTPConnection: '%s' is not a header name"), key);
		for (const ibChar* const owned : kOwnedHeaders) {
			if (key.IsSameAs(owned, false))
				ibBackendCoreException::Error(_("HTTPConnection: the header '%s' says how the message is carried, and that is not a script's to set"), key);
		}
		const ibValueTypes kind = entry.value.GetType();
		if (kind != ibValueTypes::TYPE_STRING && kind != ibValueTypes::TYPE_NUMBER)
			ibBackendCoreException::Error(_("HTTPConnection: the value of the header '%s' is a string or a number"), key);
		const wxString value = ibHttpTrimmed(entry.value.GetString());
		if (ibHttpHoldsControl(value))
			ibBackendCoreException::Error(_("HTTPConnection: the value of the header '%s' holds a line break or a control character"), key);
		message.m_headers.emplace_back(field, ibHttpUtf8(value));
		if (key.IsSameAs(wxT("Authorization"), false))
			authorized = true;
	}
	// The connection's user and password, unless the request says who it is by itself (RFC 7617: base64 of the
	// UTF-8 of "user:password").
	if (!authorized && !m_user.empty()) {
		const std::string pair = ibHttpUtf8(m_user) + ":" + ibHttpUtf8(m_password);
		std::string basic = "Basic ";
		ibBase64::Append(basic, pair.data(), pair.size());
		message.m_headers.emplace_back("Authorization", std::move(basic));
	}

	const bool secure = m_secure != nullptr;

	// The shape a transport has to have for THIS request. A SecureConnection can be changed in place, and
	// two connections can hold the same one, so what it says now is asked now rather than remembered.
	const ibValueCertificateFile* const mine = secure ? m_secure->ClientCertificate() : nullptr;
	const ibValueCertificateFile* const trusted = secure ? m_secure->TrustedCertificates() : nullptr;
	const bool verify = secure && m_secure->VerifyServerCertificate();
	const wxString certificatePath = mine != nullptr ? mine->Path() : wxString();
	const wxString certificatePassword = mine != nullptr ? mine->Password() : wxString();
	const wxString trustedPath = trusted != nullptr ? trusted->Path() : wxString();

	if (m_transport && (m_transport->m_secure != secure || m_transport->m_verify != verify
		|| m_transport->m_client_certificate != certificatePath || m_transport->m_client_password != certificatePassword
		|| m_transport->m_trusted != trustedPath))
		m_transport.reset();                                   // it was built to a shape nobody asks for any more

	if (!m_transport) {
		std::unique_ptr<ibTransport> made(new ibTransport(ibHttpUtf8(m_host), static_cast<std::uint16_t>(Port())));
		if (secure) {
			// ⚠ A FILE THAT CANNOT BE READ IS SAID HERE, BY NAME: a connection can hold two files, and "a
			// certificate file" names neither. Whom to trust: the file named, else the platform's own store. The
			// client's certificate and its key are one PEM file (valueSecureConnection.h), the password opens the
			// key; no file - no certificate to show. The check - the chain and the name - is one switch, and it is
			// on unless a script said otherwise.
			std::string trustedPem, certificatePem;
			if (trusted != nullptr && !ibHttpReadPem(trustedPath, trustedPem))
				ibBackendCoreException::Error(_("HTTPConnection: the trusted certificates could not be read from the certificate file '%s'"), trustedPath);
			if (mine != nullptr && !ibHttpReadPem(certificatePath, certificatePem))
				ibBackendCoreException::Error(_("HTTPConnection: the certificate file '%s' could not be read"), certificatePath);
			std::string error;
			switch (made->m_client.Secure(trustedPem, certificatePem, ibHttpUtf8(certificatePassword), verify, error)) {
			case ibHttpClientError::None:
				break;
			case ibHttpClientError::Trusted:
				if (trusted != nullptr)
					ibBackendCoreException::Error(_("HTTPConnection: the trusted certificates could not be read from the certificate file '%s' - not a PEM"), trustedPath);
				ibBackendCoreException::Error(_("HTTPConnection: the platform's trusted certificates could not be read - %s; name the authority's file in TrustedCertificates"),
					wxString::FromUTF8(error));
				break;
			case ibHttpClientError::Certificate:
				ibBackendCoreException::Error(_("HTTPConnection: the certificate file '%s' could not be read - not a PEM, or the key's password is wrong"), certificatePath);
				break;
			default:
				ibBackendCoreException::Error(_("HTTPConnection: a secure connection could not be prepared - %s"), wxString::FromUTF8(error));
			}
		}
		made->m_secure = secure;
		made->m_verify = verify;
		made->m_client_certificate = certificatePath;
		made->m_client_password = certificatePassword;
		made->m_trusted = trustedPath;
		m_transport = std::move(made);
	}

	// The whole request within the time allowed - the name's lookup, the connection, the request, the answer. A
	// REDIRECT IS AN ANSWER, NOT A ROAD: a 3xx comes back with its StatusCode and its Location, and following it -
	// with which method, which headers, to which host - is the script's decision.
	ibHttpResponse result;
	std::string detail;
	ibHttpClientError error = ibHttpClientError::None;
	try {
		error = m_transport->m_client.Send(message, result, std::chrono::seconds(m_timeout), detail);
	}
	catch (const std::exception& failure) {
		// What the allocator throws is not what a script's `try` catches: it reaches the interpreter as a crash.
		// Said in the engine's own voice instead. And the transport goes FIRST: this is the road where the
		// socket's state is least known - an answer read part-way leaves its tail on a kept-alive socket, and the
		// next request would read it as its own headers.
		m_transport.reset();
		ibBackendCoreException::Error(_("HTTPConnection: %s %s://%s:%d%s failed - %s"),
			wxString::FromAscii(name), wxString(secure ? wxT("https") : wxT("http")), m_host, Port(), request.ResourceAddress(), wxString::FromUTF8(failure.what()));
	}
	if (error != ibHttpClientError::None) {
		// ⚠ AND THE TRANSPORT GOES: whatever state the socket is in, the next request starts clean.
		m_transport.reset();

		wxString why;
		switch (error) {
		case ibHttpClientError::Timeout:
			why = wxString::Format(_("nobody answered within the %d s allowed"), m_timeout);
			break;
		case ibHttpClientError::Read:
			why = _("the connection was lost before the answer was whole");
			break;
		case ibHttpClientError::Answer:
			why = wxString::Format(_("what came back is not an HTTP answer, or is larger than the %u MB held"),
				static_cast<unsigned>(ibHttpClient::kMaxAnswerBytes / (1024u * 1024u)));
			break;
		case ibHttpClientError::Write:
			why = _("the connection was lost before the request was whole");
			break;
		case ibHttpClientError::Connection:
			why = _("the connection could not be made - the name does not resolve, or nothing listens on the port");
			break;
		case ibHttpClientError::Verification:
			why = wxString::Format(_("the server's certificate is not trusted - not signed by a trusted authority, expired, or not a certificate (%s); name the authority's file in TrustedCertificates"),
				wxString::FromUTF8(detail));
			break;
		case ibHttpClientError::Hostname:
			why = _("the server's certificate is not for this host name");
			break;
		case ibHttpClientError::Handshake:
			why = _("the server on this port does not speak TLS, or the handshake failed");
			break;
		default:
			why = wxString::FromUTF8(detail);   // the client's own word for the rest
		}
		ibBackendCoreException::Error(_("HTTPConnection: %s %s://%s:%d%s was not answered: %s"),
			wxString::FromAscii(name), wxString(secure ? wxT("https") : wxT("http")), m_host, Port(), request.ResourceAddress(), why);
	}

	// The headers as a Structure. A name met twice is joined with ", " (RFC 9110, 5.3) - except Set-Cookie,
	// whose values hold commas of their own: the last one stays.
	const ibValuePtr<ibValueStructure> headers(new ibValueStructure());
	for (const auto& field : result.m_headers) {
		const wxString key = wxString::FromUTF8(field.first.data(), field.first.size());
		wxString value = wxString::FromUTF8(field.second.data(), field.second.size());
		if (value.empty() && !field.second.empty())
			value = wxString::From8BitData(field.second.data(), field.second.size());
		const long held = headers->FindProp(key);
		if (held >= 0 && !key.IsSameAs(wxT("Set-Cookie"), false))
			value = headers->Entries()[static_cast<size_t>(held)].value.GetString() + wxT(", ") + value;
		headers->SetAt(ibValue(key), ibValue(value));
	}

	const ibValuePtr<ibValueHttpResponse> response(new ibValueHttpResponse(result.m_status, headers, std::move(result.m_body)));
	return response;
}

bool ibValueHttpConnection::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enHost:
		pvarPropVal = m_host;
		return true;
	case enPort:
		pvarPropVal = Port();
		return true;
	case enTimeout:
		pvarPropVal = m_timeout;
		return true;
	case enSecureConnection:
		pvarPropVal = m_secure;
		return true;
	}
	return false;
}

bool ibValueHttpConnection::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case enTimeout:
		SetTimeout(static_cast<int>(varPropVal.GetInteger()));
		return true;
	case enSecureConnection:
		SetSecureConnection(varPropVal);
		return true;
	}
	return false;
}

bool ibValueHttpConnection::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	ibHttpMethod method = ibHttpMethod();
	long requestAt = 0;
	switch (lMethodNum) {
	case enGet: method = ibHttpMethod_Get; break;
	case enPost: method = ibHttpMethod_Post; break;
	case enPut: method = ibHttpMethod_Put; break;
	case enPatch: method = ibHttpMethod_Patch; break;
	case enDelete: method = ibHttpMethod_Delete; break;
	case enHead: method = ibHttpMethod_Head; break;
	case enCallMethod:
		if (lSizeArray < 1)
			return false;
		// An HTTPMethod value and nothing else: a string that "means" one is what the enumeration is there
		// instead of.
		if (paParams[0]->GetType() == ibValueTypes::TYPE_ENUM)
			method = paParams[0]->ConvertToEnumValue<ibHttpMethod>();
		requestAt = 1;
		break;
	default:
		return false;
	}
	// NOT `lSizeArray <= requestAt -> return false`: the runtime sizes a method's frame by its DECLARED
	// arity and leaves the slots a caller did not fill EMPTY rather than absent (procUnit.cpp - "the arity
	// check only catches TOO MANY arguments"), so the slot is there to read, and an empty one is a wrong
	// type like any other. Returning false instead handed the script Undefined and no sentence at all.
	const ibValueHttpRequest* const request = ibHttpHeld<ibValueHttpRequest>(*paParams[requestAt]);
	if (request == nullptr)
		ibBackendCoreException::Error(_("HTTPConnection: what is sent is an HTTPRequest"));

	pvarRetValue = Send(method, *request);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueHttpConnection, "HTTPConnection", value_to_clsid("VL_HTCN"));
VALUE_TYPE_REGISTER(ibValueHttpRequest, "HTTPRequest", value_to_clsid("VL_HTRQ"));
SYSTEM_TYPE_REGISTER(ibValueHttpResponse, "HTTPResponse", system_to_clsid("VL_HTRS"));
