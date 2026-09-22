#ifndef __VALUE_HTTP_H__
#define __VALUE_HTTP_H__

#include "backend/compiler/value.h"
#include "backend/system/systemEnum.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueSecureConnection.h"

#include <cstddef>
#include <memory>
#include <string>

// ⭐⭐ HTTP, ASKED BY A SCRIPT - a connection, a request, a response.
//
//     var connection = New HTTPConnection("api.example", 8080, "user", "password", 30);
//     var request = New HTTPRequest("/v1/sales?shift=42");
//     request.Headers.Insert("Accept", "application/json");
//     var response = connection.Get(request);
//     if (response.StatusCode = 200) { text = response.GetBodyAsString(); }
//
// ⚠ AN ANSWER AND A FAILURE ARE TWO THINGS. 404 and 500 are answers: a response with its StatusCode, and what
// to do about it is the script's decision. A failure is when there is NO answer - the name does not resolve, the
// port is closed, nobody spoke within the time allowed - and that raises, naming the host, the port and why.
//
// ⚠ THE TIME ALLOWED IS 30 SECONDS UNLESS SAID, never forever: a background job waiting on a server that went
// quiet is a job nobody will ever hear from again. It bounds connecting and reading the answer as a whole,
// not each read of it. Two things stand outside it: resolving the name (the resolver's own time) and writing
// a large body to a server that takes it slowly (each write is bounded on its own).
//
// A REDIRECT IS AN ANSWER: a 3xx comes back with its StatusCode and its Location, and whether to go there -
// with which method, which headers, to which host - is the script's decision, not made behind its back.
//
// The host is a NAME ("api.example"), not an address with a scheme: the path goes into the request, and
// https:// is the SecureConnection property (valueSecureConnection.h) - with it the port, when none was given,
// is 443 rather than 80.
//
// A body that goes out with no Content-Type said is sent as text/plain - the library's default, and rarely what
// an API wants: say yours (`request.Headers.Insert("Content-Type", "application/json")`). An answer is held in
// memory whole, up to 100 MB.
//
// The headers are a Structure, whose keys fold case - which is what a header's name does. The ones that say
// how the message is framed and carried - Content-Length, Transfer-Encoding, Host, Connection, Expect,
// Upgrade, Proxy-Authorization - are the engine's, and a script that sets one is refused by name. The
// engine adds nothing of its own beyond those and the library's User-Agent (which a script may replace);
// it does not ask for a compressed answer, and a script that does (Accept-Encoding) gets a failure, since
// no decoder is compiled in. A name a response carries twice is joined with ", " as the protocol says;
// Set-Cookie alone cannot be joined that way (its values hold commas of their own), so the last one stays.
//
// The encoding of an answer is the script's to say: GetBodyAsString reads UTF-8 unless given a
// TextEncoding, and does not read the charset a Content-Type may carry.

void ibValueHttpRequest_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueHttpRequest : public ibValueStaticMembers<&ibValueHttpRequest_BindNames> {
	enum Prop {
		enResourceAddress,
		enHeaders,
	};
	enum Func {
		enSetBodyFromString,
		enSetBodyFromBinaryData,
		enGetBodyAsString,
		enGetBodyAsBinaryData,
	};
public:

	ibValueHttpRequest();
	virtual ~ibValueHttpRequest();

	virtual bool Init() { return true; }                                    // New HTTPRequest()
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New HTTPRequest(resourceAddress, headers)

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// The same for a caller in C++ (and for the tests).
	const wxString& ResourceAddress() const { return m_resource; }
	void SetResourceAddress(const wxString& resource);      // "" is "/"; raises on what cannot stand in a request line
	const ibValueStructure& Headers() const { return *m_headers; }
	void SetHeaders(const ibValue& headers);                 // a Structure, or raises
	// Two names, not one overloaded: a string literal converts to `const void*` as readily as to a wxString, and
	// an overload pair would leave that to the compiler to guess (portability.md, section 1).
	void SetBodyText(const wxString& text, ibTextEncoding encoding);
	void SetBodyBytes(const void* bytes, size_t length);
	const std::string& Body() const { return m_body; }

private:
	wxString                     m_resource;
	ibValuePtr<ibValueStructure> m_headers;
	std::string                  m_body;                    // bytes, as they go out
};

void ibValueHttpResponse_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// Made by a connection, never by a script.
class BACKEND_API ibValueHttpResponse : public ibValueStaticMembers<&ibValueHttpResponse_BindNames> {
	enum Prop {
		enStatusCode,
		enHeaders,
	};
	enum Func {
		enGetBodyAsString,
		enGetBodyAsBinaryData,
	};
public:

	ibValueHttpResponse();
	ibValueHttpResponse(int status, const ibValuePtr<ibValueStructure>& headers, std::string body);
	virtual ~ibValueHttpResponse();

	virtual bool IsEmpty() const { return m_status == 0; }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	int StatusCode() const { return m_status; }
	const ibValueStructure& Headers() const { return *m_headers; }
	const std::string& Body() const { return m_body; }
	wxString BodyAsString(ibTextEncoding encoding) const;    // raises when the bytes are not text in it

private:
	int                          m_status = 0;
	ibValuePtr<ibValueStructure> m_headers;
	std::string                  m_body;
};

void ibValueHttpConnection_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueHttpConnection : public ibValueStaticMembers<&ibValueHttpConnection_BindNames> {
	enum Prop {
		enHost,
		enPort,
		enTimeout,
		enSecureConnection,
	};
	enum Func {
		enGet,
		enPost,
		enPut,
		enPatch,
		enDelete,
		enHead,
		enCallMethod,
	};
public:

	ibValueHttpConnection();
	virtual ~ibValueHttpConnection();

	virtual bool IsEmpty() const { return m_host.empty(); }

	virtual bool Init() { return false; }                                   // a connection to nowhere is not one
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New HTTPConnection(host, port, user, password, timeout)

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// The same door for a caller in C++ (and for the tests). Send raises when there is no answer. A port that
	// was not given (portGiven = false) is 80, or 443 once a secure connection is set.
	void Open(const wxString& host, int port, const wxString& user, const wxString& password, int timeoutSeconds, bool portGiven = true);
	void SetTimeout(int seconds);
	void SetSecureConnection(const ibValue& value);          // a SecureConnection, or Undefined for plain http://; or raises
	const ibValueSecureConnection* Secure() const { return m_secure; }
	int Port() const;
	ibValue Send(ibHttpMethod method, const ibValueHttpRequest& request);

private:
	struct ibTransport;                                      // the library lives in the .cpp, and only there
	std::unique_ptr<ibTransport> m_transport;                // made on the first request, dropped on any failure
	                                                         // and whenever a setting it was built from changes

	wxString m_host, m_user, m_password;
	int      m_port = 80;
	bool     m_portGiven = true;
	int      m_timeout = 30;
	ibValuePtr<ibValueSecureConnection> m_secure;            // empty: plain http://
};

#endif // !__VALUE_HTTP_H__
