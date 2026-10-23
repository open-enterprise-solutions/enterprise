////////////////////////////////////////////////////////////////////////////
//	Description : reading and writing the JSON-RPC envelope
////////////////////////////////////////////////////////////////////////////

#include "backend/rpc/rpcMessage.h"

#include "backend/backend_exception.h"
#include "core/serialize/jsonProvider.h"
#include "backend/compiler/value.h"            // ibValue::GetAvailableCtor — a built-in's name
#include "backend/objCtor.h"                   // ibCtorAbstractType::GetClassName
#include "core/description/compositionDescription.h"   // ibCompositionNodeName / …Clsid — a composition's parts, both ways
#include "backend/metadataConfiguration.h"    // activeMetaData — the registered types, for the lookup

// ⭐⭐ THE MIRROR OF THE RESOLVER BELOW, and the pair is what makes a schema READ, EDITED AND SENT
// BACK possible. An answer renders a node's type as a WORD; a request carrying that word back was
// read as clsid 0, because the parsing side had no lookup at all. So a caller could take report_get's
// answer, change one field and return it — and every typed part of it (its parameters, its outputs,
// its fields) arrived as untyped nodes, leaving a composition that was simply EMPTY. Nothing failed;
// the refusal said "this schema asks for no parameters" about a schema that declared one (measured
// 2026-09-06).
//
// ⚠ CHAINED, NOT CHOSEN — the same mistake the resolver below made once and says so. The metadata
// answers for registered types; a composition's own parts are registered nowhere, so they are the
// FLOOR under it. Asking only one of the two is how a lookup exists and never fires.
namespace {

ibClassID LookupType(const ibString& name, const ibMetaData* metaData)
{
	// ⚠ THE COMPOSITION'S OWN PARTS FIRST, AND NOT ONLY FOR SPEED. A lookup runs INSIDE the JSON
	// parse, and `GetIDObjectFromString` does not answer 0 for a name it does not know — it
	// THROWS ("Object '%s' is not exist"). Asked first, it threw on `CompositionVariant`, the
	// parse unwound, and the error reply was then assembled with no request id: a MALFORMED
	// ENVELOPE, which the far end reports as the server having broken rather than as a request
	// having failed. Every request carrying a node type the metadata does not know would have
	// done it (measured 2026-09-06, sending a hand-built schema).
	//
	// 🛑 I WROTE "CHAINED, NOT CHOSEN" AND CHAINED THEM THE WRONG WAY ROUND. The order of a
	// fallback chain is not a detail when one of its links can throw: the one that answers
	// quietly goes first, and the one that raises is wrapped.
	if (const ibClassID part = ibCompositionNodeClsid(name); part != ibClassID(0))
		return part;

	// ⭐⭐ AND IT IS THE METADATA'S LOOKUP, NOT THE VALUE FACTORY'S — the two are not
	// interchangeable and the order between them is the point (Max, 2026-09-06). `ibMetaData::
	// GetIDObjectFromString` searches the METAOBJECTS first and only then falls through to
	// `ibValue::GetIDObjectFromString`: reference types live in the metaobjects, so asking the
	// value factory alone finds no reference at all, and neither stage may be skipped.
	if (metaData != nullptr) {
		try {
			return metaData->GetIDObjectFromString(name);
		}
		catch (const ibCoreException&) {
			// Neither stage knew it — which is where that call raises. A node keeps clsid 0 and
			// is read as an untyped one, which is what an unknown name has always meant here.
		}
	}
	return ibClassID(0);
}

} // namespace

void ibRpcInstallTypeLookup(ibJsonProvider& provider)
{
	provider.SetTypeLookup([](const ibString& name) -> ibClassID { return LookupType(name, activeMetaData); });
}

ibString ibRpcRenderNode(const ibDataNode& node, const std::function<ibString(ibClassID)>& typeResolver)
{
	ibJsonProvider provider;
	// A message on the wire is one line: a program reads it, and the readable view's breaks and indents were bytes.
	provider.SetCompact(true);

	// ⭐ A NODE SAYS WHAT IT IS, IN WORDS — even when its type is a synthetic id nothing constructs.
	// The provider already names every REGISTERED type; what it could not name were the ids a
	// description makes for its own parts, and a caller then read `"NodeType": 67799176431653306`
	// where every other line of the same answer says a word (report_get, measured 2026-09-02).
	//
	// The caller's own resolver still wins where there is one — this is the FLOOR, and a floor goes
	// UNDER. 🛑 The first version chose between the two instead of chaining them, so it never fired
	// at all: every tool answer is rendered with the metadata resolver (mcpServer), which knows
	// registered types and says nothing about a composition's parts — the exact ids this exists for.
	// Written as "the caller's own resolver still wins" in the comment and as `? :` in the code.
	provider.SetTypeResolver([typeResolver](ibClassID clsid) -> ibString {
		if (typeResolver) {
			const ibString named = typeResolver(clsid);
			if (!named.IsEmpty())
				return named;
		}
		const wxString part = ibCompositionNodeName(clsid);
		if (!part.IsEmpty())
			return part;
		// …and under it the static registry — a built-in's name (Number, String), which the provider itself gave
		// before it moved into the core.
		const ibCtorAbstractType* const ctor = ibValue::GetAvailableCtor(clsid);
		return ctor != nullptr ? ctor->GetClassName() : wxString();
	});

	ibString text;
	if (!provider.Write(node, text))
		return ibString();
	return text;
}

namespace {

ibString EmitNode(const ibDataNode& node) { return ibRpcRenderNode(node); }

// The envelope every answer shares. Written by hand rather than by a helper
// object because there are exactly two shapes and both are three lines.
ibDataNode Envelope(const ibDataValue& id)
{
	ibDataNode root;
	root.SetValue(wxT("jsonrpc"), ibString(wxT("2.0")));
	// The id goes back VERBATIM — a number stays a number, a string stays a
	// string. Rendering it through a type of our choosing would break the one
	// thing the protocol uses to pair an answer with its call.
	root.AddField(wxT("id"), id);
	return root;
}

// One request read by a provider whose type lookup is installed already — whose types it knows is the caller's.
bool ReadRequest(const ibString& text, ibJsonProvider& provider, ibRpcRequest& request, wxString& error)
{
	ibDataNode root;

	try {
		if (!provider.Read(text, root)) {
			error = wxT("the request could not be read as JSON");
			return false;
		}
	}
	catch (const ibCoreException& e) {
		// The parser throws with a byte offset — the most useful half of the
		// answer, so it is passed on rather than replaced with a summary.
		error = e.what();
		return false;
	}
	catch (...) {
		error = wxT("the request could not be read as JSON");
		return false;
	}

	// A scalar key lands in the FIELD area; `params`, being an object, lands in
	// the properties area as a Child.
	request.m_method = root.GetValue<ibString>(wxT("method"));
	if (request.m_method.IsEmpty()) {
		error = wxT("no method named");
		return false;
	}

	if (const ibDataValue* id = root.FindField(wxT("id")))
		request.m_id = *id;

	// The params node is the request's to give up: it is moved out of the tree just read, not copied.
	if (ibDataNode* params = root.FindChild(wxT("params")))
		request.m_params = std::move(*params);

	return true;
}

} // namespace

bool ibRpcParseRequest(const ibString& text, ibRpcRequest& request, wxString& error)
{
	ibJsonProvider provider;
	ibRpcInstallTypeLookup(provider);
	return ReadRequest(text, provider, request, error);
}

bool ibRpcParseRequest(const ibString& text, ibRpcRequest& request, wxString& error, bool& unknownType)
{
	// By no configuration, not the thread's — a thread that works for no base has none to ask.
	unknownType = false;
	ibJsonProvider provider;
	provider.SetTypeLookup([&unknownType](const ibString& name) -> ibClassID {
		const ibClassID clsid = LookupType(name, nullptr);
		if (clsid == ibClassID(0))
			unknownType = true;
		return clsid;
	});
	return ReadRequest(text, provider, request, error);
}

void ibRpcLookupTypes(ibDataNode& node, const ibMetaData* metaData)
{
	ibJsonProvider provider;
	provider.SetTypeLookup([metaData](const ibString& name) -> ibClassID { return LookupType(name, metaData); });
	provider.LookupTypes(node);
}

bool ibRpcParseResponse(const ibString& text, ibDataValue& id, ibString& payload, bool& isError)
{
	ibDataNode root;

	try {
		ibJsonProvider provider;
		ibRpcInstallTypeLookup(provider);
		if (!provider.Read(text, root))
			return false;
	}
	catch (...) {
		return false;
	}

	// ONE FACT TELLS THEM APART: a request names a method, an answer does not.
	if (!root.GetValue<ibString>(wxT("method")).IsEmpty())
		return false;

	const ibDataNode* result = root.FindChild(wxT("result"));
	const ibDataNode* failed = root.FindChild(wxT("error"));

	if (result == nullptr && failed == nullptr)
		return false;   // neither a call nor an answer — not ours to read

	if (const ibDataValue* found = root.FindField(wxT("id")))
		id = *found;

	isError = (failed != nullptr);
	payload = ibRpcRenderNode(isError ? *failed : *result);
	return true;
}

ibString ibRpcWriteResult(const ibDataValue& id, ibDataNode result)
{
	ibDataNode root = Envelope(id);
	root.Child(wxT("result")) = std::move(result);
	return EmitNode(root);
}

ibString ibRpcWriteError(const ibDataValue& id, ibRpcError code, const wxString& message)
{
	return ibRpcWriteError(id, code, message, nullptr);
}

// The error answer, whoever numbers the code — JSON-RPC's own, or the application's.
static ibString WriteError(const ibDataValue& id, std::int64_t code, const wxString& message, const ibDataNode* data)
{
	ibDataNode root = Envelope(id);

	ibDataNode& error = root.Child(wxT("error"));
	// Built as a VALUE, not through SetValue<T>: the node's codecs cover a fixed
	// set (wxString / bool / std::int32_t / buffer / guid / ibNumber / date) and a 64-bit
	// integer is not one of them.
	error.AddField(wxT("code"), ibDataValue::Int(code));
	error.SetValue(wxT("message"), message);

	// ⭐ WHAT THE CALLER CAN DO ABOUT IT, when the refusal has an answer. A version
	// refusal that does not list the versions on offer leaves the client to guess,
	// which is the same as no answer; the specification therefore puts the list in
	// `data.supported`. Optional because most refusals have nothing to add — a
	// malformed message is not made actionable by decorating it.
	if (data != nullptr)
		error.Child(wxT("data")) = *data;

	return EmitNode(root);
}

ibString ibRpcWriteError(const ibDataValue& id, ibRpcError code, const wxString& message,
	const ibDataNode* data)
{
	return WriteError(id, static_cast<std::int64_t>(code), message, data);
}

ibString ibRpcWriteError(const ibDataValue& id, std::int32_t code, const wxString& message)
{
	return WriteError(id, code, message, nullptr);
}
