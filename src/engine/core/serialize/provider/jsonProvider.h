#ifndef __SERIALIZE_JSON_PROVIDER_H__
#define __SERIALIZE_JSON_PROVIDER_H__

////////////////////////////////////////////////////////////////////////////
//	Description : ibJsonProvider — a READABLE, AI-facing view of the node tree.
//
//	A second ibFormatProvider next to ibBinaryProvider: it renders the SAME
//	ibDataNode tree as JSON text (scalar -> value, Bool -> true/false, Number ->
//	number, String -> "text", Guid -> "its text", Binary -> "base64:...", Child -> { object },
//	Array -> [ list ]). Structural keys are underscore-prefixed (_clsid, _metaId,
//	_children, _raw) so they read apart from the data.
//
//	Read (JSON -> tree) is IMPLEMENTED and complete — a self-contained recursive-descent
//	parser covering everything Write emits: nested children, arrays, base64 binaries, the
//	structural keys, the synthetic ones. It reads MESSAGES, never a configuration: the client
//	protocol and MCP parse every JSON-RPC request through it (rpc/rpcMessage.cpp), as does the
//	thin client; ibBinaryProvider stays the round-trip format.
//
//	Why it is not a round trip. The VIEW is lossy by design — readability is its whole
//	point — so two things do not survive Write -> Read, and no parser can undo them
//	(fields and properties were a third until a node held them as one set, 2026-10-26):
//	  1. Date becomes an ISO STRING — indistinguishable from a String that happens to look
//	     like a date, so it reads back as a String. So does a Guid's text; whoever asks for
//	     a date or a guid reads one out of it (ibDataValue::AsDate, AsGuid).
//	  2. NodeType emits the resolved type NAME (numeric clsid only as a fallback), and
//	     TypeDesc is injected beside typeId with no node entry behind it. TypeDesc is
//	     parsed and dropped; a name-form NodeType needs SetTypeLookup (below) to become a
//	     clsid again, and without one the node keeps clsid 0 rather than inventing a value —
//	     and the word itself, as its field NodeType, for whoever can look it up (LookupTypes).
//	Closing 1 means changing what WRITE emits — a separate lossless emitter, not more
//	parser. Malformed input throws with the byte offset rather than yielding a partial tree.
//	Nesting past kMaxNesting is malformed too: the reader recurses, and a request is parsed
//	before authentication, so an unbounded descent overflows the stack.
////////////////////////////////////////////////////////////////////////////

#include "core/serialize/dataBuilder.h"
#include <string>
#include <functional>

class CORE_API ibJsonProvider : public ibFormatProvider {
public:
	// Objects and arrays on one path, counting the root. Past this the text is refused.
	// The same ceiling the script's JSONReader already uses (valueJson.cpp, kMaxDepth).
	static constexpr int kMaxNesting = 256;

	// A JSON string as a value: a Binary where Write wrote one ("base64:…"), a String otherwise — for this provider's
	// text and for a tree another reader made of the same text (the thin client's, ibReadProtocolNode).
	static ibDataValue ReadString(const ibString& text);

	bool Write(const ibDataNode& root, ibWriterMemory& writer) const override;
	bool Read(ibReaderMemory& reader, ibDataNode& root) const override;  // messages, before any login (see above)

	// …and the same view as the ENGINE'S TEXT — the characters written straight into it, and read back from the very
	// buffer they lie in: a text that stays in the process (a file base's answer, its client's request) is never
	// encoded to bytes and decoded again, and handing it on is one more owner of it. Bytes are the socket's business.
	bool Write(const ibDataNode& root, ibString& text) const;
	bool Read(const ibString& text, ibDataNode& root) const;
	// …and what one key of an object is to its node, its value read by whoever read the text: the structural keys
	// (NodeType — by the lookup below, NodeId, NodeRaw; TypeDesc dropped), a node a property, the rest fields. False when
	// a structural key holds what it cannot. NodeChildren are not one: the reader descends into them itself.
	bool ReadEntry(ibDataNode& node, const ibString& key, ibDataValue value) const;

	// clsid -> readable type name. The names are the registry's — a configuration's own
	// (CatalogRef.X, DocumentRef.Y) and the built-in ones (Number, String) alike — so the
	// caller injects a resolver over it (ibMetaTypeResolver: ibMetaData::GetTypeCtor, then
	// the static registry). Without a resolver no type is named: the clsid alone is written.
	void SetTypeResolver(std::function<ibString(ibClassID)> fn) { m_resolveType = std::move(fn); }

	// The INVERSE, for Read: type name -> clsid. Same asymmetry as above — a reference
	// type's name only means something against the metadata that defined it, so the
	// caller supplies the lookup. Unset (the default) leaves a name-form NodeType at
	// clsid 0; a numeric NodeType needs no lookup and always resolves.
	void SetTypeLookup(std::function<ibClassID(const ibString&)> fn) { m_lookupType = std::move(fn); }
	// …and after a reading, for the words it kept: each node's NodeType field the lookup knows becomes its clsid, all the
	// way down. A text is read once by whoever does not hold the configuration yet (a call, before its caller is known),
	// and its types looked up by whoever does — not read again.
	void LookupTypes(ibDataNode& node) const;

	// One line, no space — the wire's: a protocol's answer is read by a program, and the line breaks and the indents of
	// the readable view were bytes on every call (ibRpcRenderNode). Off — readable — by default.
	void SetCompact(bool compact) { m_compact = compact; }

private:
	// `Text` — UTF-8 bytes (std::string) or the engine's text (ibString): one view, written into either.
	template<class Text> void EmitNode(const ibDataNode& node, Text& out, int depth) const;
	template<class Text> void EmitValue(const ibDataValue& value, Text& out, int depth) const;
	// The indent of a line of the readable view; nothing in the compact one.
	template<class Text> void Pad(Text& out, int depth) const;
	ibString ResolveType(ibClassID clsid) const;

	std::function<ibString(ibClassID)>       m_resolveType;
	std::function<ibClassID(const ibString&)> m_lookupType;
	bool                                      m_compact = false;
};

#endif // !__SERIALIZE_JSON_PROVIDER_H__
