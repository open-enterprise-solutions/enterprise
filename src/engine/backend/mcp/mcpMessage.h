#ifndef _IB_MCP_MESSAGE_H_
#define _IB_MCP_MESSAGE_H_

////////////////////////////////////////////////////////////////////////////
//	Description : the JSON-RPC envelope MCP is spoken in
////////////////////////////////////////////////////////////////////////////
//
// MCP is JSON-RPC 2.0: a request names a METHOD and carries PARAMS, an answer
// carries a RESULT or an ERROR under the request's id.
//
// NO SECOND JSON. The envelope is read and written with ibJsonProvider over
// ibDataNode — the pair already in the tree. Its Read half has been complete
// since 2026-08-02 with nothing calling it (ROADMAP § 4.4, "deliberately
// unwired"); this is its first consumer, and its emitter fits the protocol
// exactly: identity keys are written only when meaningful, so a plain node
// renders as a clean object, and an empty value renders as `null` — which is
// what JSON-RPC wants for the id of a request that could not be parsed.
//
// WHERE THE AREAS LAND. The parser puts every scalar key in the node's FIELD
// area and every nested object in the PROPERTIES area as a Child. So a request
// reads as: method / id from the fields, params through FindChild.
//
// A NOTIFICATION (a request with no id) is answered with NOTHING, per the
// protocol — which is why the id is carried as a value that can be empty
// rather than as a number with a sentinel.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "backend/serialize/dataBuilder.h"

#include <functional>

#include <wx/string.h>

struct BACKEND_API ibMcpRequest {

	wxString    m_method;
	ibDataValue m_id;       // Number or String; Empty means a notification
	ibDataNode  m_params;   // an empty node when the request carried none

	bool WantsAnswer() const { return !m_id.IsEmpty(); }
};

// JSON-RPC error codes, as the protocol spells them. Kept as an enum so a
// refusal names itself instead of arriving as a bare number at a call site.
enum class ibMcpError {
	Parse          = -32700,
	InvalidRequest = -32600,
	MethodNotFound = -32601,
	InvalidParams  = -32602,
	Internal       = -32603,

	// The two the MCP specification allocates for itself, out of the range JSON-RPC
	// reserves for implementations. They exist because the transport now mirrors
	// parts of the body into HTTP headers, and both failures they name are about
	// the two disagreeing.
	//
	// ⚠ AND THEIR MESSAGES ARE ENGLISH, not translated. What goes on the wire is read
	// by a CLIENT — another program, or a model — and never by the person at the
	// designer, who is shown platform errors through a different door entirely. A
	// refusal that changes wording with the machine's locale is one a client cannot
	// match on and a report nobody can compare; `_()` belongs where a human reads.
	//
	// HeaderMismatch — a header does not match the body it mirrors (or a required
	// one is missing). It matters because different boxes read different sources:
	// a load balancer routes on the header, the server executes on the body, and a
	// request where they differ is exactly the shape that gets past one and is
	// carried out by the other.
	HeaderMismatch = -32020,

	// UnsupportedProtocolVersion — carries `data.supported` listing what this
	// server speaks, so a client can retry with a version both sides have instead
	// of guessing why the answers look strange.
	UnsupportedVersion = -32022,
};

// Reads one request. Answers false and fills `error` when the text is not a
// JSON-RPC request — malformed JSON, or well-formed JSON that is not one.
BACKEND_API bool ibMcpParseRequest(const wxString& text, ibMcpRequest& request, wxString& error);

// A MESSAGE THAT IS AN ANSWER, not a call. Once the server can ask the client
// something (sampling), the same endpoint starts receiving both — and they are
// told apart by ONE fact: a request names a method, a response does not. Answers
// false for anything carrying a method, so the caller can go on to read it as a
// request.
//
// `payload` is the result (or the error) rendered back to text: what came back
// is the client's business, and this layer does not pretend to know its shape.
BACKEND_API bool ibMcpParseResponse(const wxString& text, ibDataValue& id,
	wxString& payload, bool& isError);

// ANY node as JSON text. `typeResolver` turns config-specific class ids into
// the portable names a configuration writes (metaIntrospect.h) — without one a
// described object answers with numbers that mean nothing outside this process.
// The envelope itself needs no resolver: it carries no types.
BACKEND_API wxString ibMcpRenderNode(const ibDataNode& node,
	const std::function<wxString(ibClassID)>& typeResolver = {});

// The two answers. `id` is the request's, verbatim: the protocol matches a
// reply to its call by that value and by nothing else.
BACKEND_API wxString ibMcpWriteResult(const ibDataValue& id, const ibDataNode& result);
BACKEND_API wxString ibMcpWriteError(const ibDataValue& id, ibMcpError code, const wxString& message);

// The same, with the `data` member a few refusals owe the caller — chiefly the list
// of versions this server speaks, which is what makes a version refusal answerable
// rather than merely final.
BACKEND_API wxString ibMcpWriteError(const ibDataValue& id, ibMcpError code, const wxString& message,
	const ibDataNode* data);

#endif // _IB_MCP_MESSAGE_H_
