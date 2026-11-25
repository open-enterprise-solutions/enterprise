#ifndef _IB_MCP_ERA_H_
#define _IB_MCP_ERA_H_

////////////////////////////////////////////////////////////////////////////
//	Description : ibMcpEra — one era of the MCP protocol: its revisions, its door, the shape of its answer
////////////////////////////////////////////////////////////////////////////
//
// ⭐ TWO ERAS ON ONE ENDPOINT (2026-11-25). The specification calls them LEGACY — an `initialize` handshake opens
// the conversation; revisions 2025-03-26, 2025-06-18, 2025-11-25 — and MODERN — no handshake, every request carries
// its version in `_meta`; 2026-07-28. A request is of one or the other by how it came: a version in `_meta` is
// modern, anything else legacy (Of). The era is the REQUEST's, not the client's: the same client may be either on
// different days.
//
// ⭐ WHAT DIFFERS IS THE ENVELOPE, AND ALL OF IT IS HERE. The tools, the lists and the prompts are one body for both;
// an era says which revisions it is, what it asks of a request before the body runs (Admit), how every result is
// written (Complete) and how long a list may be kept (FillCacheable). The server asks the request's era and nothing
// else — a difference between the eras written in the server as an `if` is one this file has lost.
//
//   legacy  opened by `initialize`, which agrees one of its revisions; the header MCP-Protocol-Version (a client's
//           duty from 2025-06-18) names a revision this server speaks, checked by the transport; a result is written
//           as it is; the GET stream carries what the server says unasked — the person's messages, and sampling to a
//           client that declared it.
//   modern  opened by nothing — `server/discover` says who we are; the headers mirror the body and must agree with
//           it; every result says `resultType` and who gave it; a list says how long it may be kept. No request
//           comes FROM the server: the window reaches the model in the answer to its next call (`chat`).
//
// The legacy revisions differ on THIS server's side only in what the client is obliged to send, so one
// implementation answers all three honestly.
//
// WHAT IT SPOKE BEFORE: the constant read `2024-11-05` until 2026-09-05 — the first revision and the deprecated
// HTTP+SSE transport — while the server was Streamable HTTP all along: one endpoint, POST answered as JSON or as an
// SSE stream, GET a listen stream. That is the legacy shape exactly; sessions are optional there, and this server
// is stateless.
//
////////////////////////////////////////////////////////////////////////////

#include "frmserver/frmserver.h"
#include "backend/rpc/rpcMessage.h"   // ibRpcRequest — what an era reads

#include <vector>

// WHAT THE HTTP HEADERS SAID, for the parts of the message the transport mirrors into them. From 2025-06-18 a
// client states the protocol version in a header; from 2026-07-28 it also mirrors the method and the tool name,
// and the server MUST refuse a request whose header and body disagree — because a load balancer routes on the one
// and the server executes on the other.
//
// A struct of its own rather than the HTTP request: the JSON-RPC layer has never known what HTTP is, and the three
// strings it needs are not a reason to teach it.
struct ibMcpWireHeaders {
	ibString m_protocolVersion;   // MCP-Protocol-Version, empty when absent
	ibString m_method;            // Mcp-Method
	ibString m_name;              // Mcp-Name (tools/call, resources/read, prompts/get)
};

class FRMSERVER_API ibMcpEra {
public:

	virtual ~ibMcpEra() = default;

	// Its revisions, newest first: the first is what it answers in when the caller names none of them.
	virtual const std::vector<ibString>& GetVersions() const = 0;

	// ⭐ THE DOOR: what this era asks of a request before the body runs. The refusal written, its code in
	// `outErrorCode` (the server's own, for the transport's status), or empty when the request goes on.
	virtual ibString Admit(const ibRpcRequest& request, const ibMcpWireHeaders& headers, int* outErrorCode) const = 0;

	// Every result, as this era writes it.
	virtual void Complete(ibDataNode& result) const = 0;

	// A list, and `server/discover`: how long the answer may be kept.
	virtual void FillCacheable(ibDataNode& result) const = 0;

	// The era of a request: a version named in `_meta` is modern, anything else legacy.
	static const ibMcpEra& Of(const ibRpcRequest& request);

	// Whether either era speaks the revision named.
	static bool Speaks(const ibString& version);

	// ⭐ WHAT IS ON OFFER, built once — the modern revision first, then the legacy ones — for a version refusal and
	// for `server/discover`. AN ARRAY, not the numbered keys of a child node: `supported.includes(v)` finds nothing
	// in `{"0": …}`, and a caller that cannot read the list cannot tell what to fall back to.
	static std::vector<ibDataValue> SupportedVersionList();

	// WHAT A VERSION REFUSAL OWES THE CALLER: the list in `supported`, as the specification puts it, and the
	// version it could not serve in `requested`. A refusal that does not say what IS on offer leaves the client to
	// guess.
	static ibDataNode SupportedVersions(const ibString& asked);

	// WHO THIS SERVER IS, for the three that say it: `initialize`, `server/discover` and a modern result. THE REAL
	// BUILD — the number the About box shows — not a "1.0" that could never be wrong because it never changed.
	static void FillServerInfo(ibDataNode& info);
};

// 2025-11-25, 2025-06-18, 2025-03-26 — opened by `initialize`.
class FRMSERVER_API ibMcpLegacyEra final : public ibMcpEra {
public:

	virtual const std::vector<ibString>& GetVersions() const override;
	virtual ibString Admit(const ibRpcRequest& request, const ibMcpWireHeaders& headers, int* outErrorCode) const override;
	virtual void Complete(ibDataNode& result) const override;
	virtual void FillCacheable(ibDataNode& result) const override;
};

// 2026-07-28 — no handshake: every request says its revision, and every result says what it is.
class FRMSERVER_API ibMcpModernEra final : public ibMcpEra {
public:

	virtual const std::vector<ibString>& GetVersions() const override;
	virtual ibString Admit(const ibRpcRequest& request, const ibMcpWireHeaders& headers, int* outErrorCode) const override;
	virtual void Complete(ibDataNode& result) const override;
	virtual void FillCacheable(ibDataNode& result) const override;
};

#endif // _IB_MCP_ERA_H_
