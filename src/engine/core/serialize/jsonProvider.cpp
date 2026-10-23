#include "core/serialize/jsonProvider.h"
#include "core/serialize/jsonText.h"  // ibJsonText — how a string is spelled in JSON, said once
#include "core/utils/base64.h"        // ibBase64 — a binary's text, into the output and read in place
#include "core/exception.h"           // ibCoreException — malformed-JSON throw
#include "core/fiber/fiber.h"         // StackLow — the depth count's backstop when the reserve runs out first
#include <algorithm>                   // std::all_of — a whole number's digits
#include <charconv>                    // std::to_chars — a whole number's digits, on the stack
#include <cstdio>                      // std::snprintf — a date's digits
#include <cstring>                     // strlen — the JSON literal match

////////////////////////////////////////////////////////////////////////////

namespace {

// WHAT THE VIEW WRITES OF ITS OWN — punctuation, digits, the structural keys: ASCII, appended as it is to bytes and a
// character a byte to the engine's text. Neither text is made on the way.
void Put(std::string& out, const char* ascii, size_t n) { out.append(ascii, n); }
void Put(ibString& out, const char* ascii, size_t n)
{
	wchar_t chunk[64];
	while (n != 0) {
		const size_t k = n < 64 ? n : 64;
		for (size_t i = 0; i < k; ++i)
			chunk[i] = static_cast<wchar_t>(static_cast<unsigned char>(ascii[i]));
		out.append(chunk, k);
		ascii += k;
		n -= k;
	}
}
template<class Text> void Put(Text& out, const char* ascii) { Put(out, ascii, std::strlen(ascii)); }
// …and a decimal's text, in characters: a character a byte to bytes, as they are to the engine's text.
void Put(std::string& out, const wchar_t* ascii, size_t n)
{
	for (size_t i = 0; i < n; ++i)
		out += static_cast<char>(ascii[i]);
}
void Put(ibString& out, const wchar_t* ascii, size_t n) { out.append(ascii, n); }

// A text quoted — the value's own, or a resolved type's name. HOW is said in one place for the whole backend
// (ibJsonText) - the same rule the script's JSONWriter writes by.
template<class Text> void Quote(Text& out, const ibString& s) { ibJsonText::AppendQuoted(out, s.wc_str(), s.length()); }

template<class Text> void PutInteger(Text& out, long long value)
{
	char digits[24];
	const std::to_chars_result written = std::to_chars(digits, digits + sizeof(digits), value);
	Put(out, digits, static_cast<size_t>(written.ptr - digits));
}
template<class Text> void PutInteger(Text& out, unsigned long long value)
{
	char digits[24];
	const std::to_chars_result written = std::to_chars(digits, digits + sizeof(digits), value);
	Put(out, digits, static_cast<size_t>(written.ptr - digits));
}

// A number as its exact decimal. A whole one — every id and count the protocol carries — from the stack; any other
// through the decimal's own text.
template<class Text> void PutNumber(Text& out, const ibNumber& number)
{
	long long whole = 0;
	if (number.ToInt(whole) == 0 && ibNumber(whole) == number) {
		PutInteger(out, whole);
		return;
	}
	ibString text;
	number.ToString(text);   // digits, a sign and a point — ASCII
	Put(out, text.wc_str(), text.length());
}

// A binary as the view writes it — "base64:<...>", ASCII all through.
template<class Text> void PutBase64(Text& out, const ibMemoryBuffer& b)
{
	Put(out, "\"base64:");
	ibBase64::Append(out, b.GetData(), b.GetDataLen());
	Put(out, "\"");
}

} // namespace

////////////////////////////////////////////////////////////////////////////

// The injected resolver's answer — the registry is the engine's (SetTypeResolver). Empty when there is none, or the
// clsid is no registered type.
ibString ibJsonProvider::ResolveType(ibClassID clsid) const
{
	return m_resolveType ? m_resolveType(clsid) : ibString();
}

template<class Text>
void ibJsonProvider::Pad(Text& out, int depth) const
{
	if (m_compact)
		return;
	static const char kSpaces[] = "                                                                ";
	for (size_t n = static_cast<size_t>(depth) * 2; n != 0; ) {
		const size_t k = n < sizeof(kSpaces) - 1 ? n : sizeof(kSpaces) - 1;
		Put(out, kSpaces, k);
		n -= k;
	}
}

template<class Text>
void ibJsonProvider::EmitValue(const ibDataValue& v, Text& out, int depth) const
{
	switch (v.Kind()) {
	case ibDataKind::Empty:  Put(out, "null"); break;
	case ibDataKind::Bool:   Put(out, v.AsBool() ? "true" : "false"); break;
	case ibDataKind::Number: PutNumber(out, v.AsNumber()); break; // exact decimal
	case ibDataKind::Date: {
		const ibDateTime date = v.AsDate();   // the wall-clock reading (fdatetime.h); the empty date is ""
		ibDateTimeParts p;
		date.ToParts(p);
		// readable ISO — digits only
		char text[32] = "";
		if (!date.IsEmpty())
			std::snprintf(text, sizeof(text), "%04d-%02u-%02u %02u:%02u:%02u", p.m_year, p.m_month, p.m_day, p.m_hour, p.m_minute, p.m_second);
		Put(out, "\"");
		Put(out, text);
		Put(out, "\"");
		break;
	}
	case ibDataKind::String: Quote(out, v.AsString()); break;
	case ibDataKind::Binary: PutBase64(out, v.AsBinary()); break;
	case ibDataKind::Child: {
		const std::shared_ptr<ibDataNode>& c = v.AsChild();
		if (c) EmitNode(*c, out, depth);
		else   Put(out, "null");
		break;
	}
	case ibDataKind::Array: {
		const std::vector<ibDataValue>& a = v.AsArray();
		if (a.empty()) { Put(out, "[]"); break; }
		Put(out, m_compact ? "[" : "[\n");
		for (size_t i = 0; i < a.size(); i++) {
			Pad(out, depth + 1);
			EmitValue(a[i], out, depth + 1);
			Put(out, (i + 1 < a.size()) ? (m_compact ? "," : ",\n") : (m_compact ? "" : "\n"));
		}
		Pad(out, depth);
		Put(out, "]");
		break;
	}
	}
}

template<class Text>
void ibJsonProvider::EmitNode(const ibDataNode& node, Text& out, int depth) const
{
	Put(out, m_compact ? "{" : "{\n");
	bool first = true;
	const int inner = depth + 1;

	auto key = [&](const char* keyLiteral) {
		if (!first) Put(out, m_compact ? "," : ",\n");
		first = false;
		Pad(out, inner);
		Put(out, "\"");
		Put(out, keyLiteral);
		Put(out, m_compact ? "\":" : "\": ");
	};
	auto namedKey = [&](const ibString& name) {
		if (!first) Put(out, m_compact ? "," : ",\n");
		first = false;
		Pad(out, inner);
		Quote(out, name);
		Put(out, m_compact ? ":" : ": ");
	};

	// identity (only when meaningful — keeps Child sub-nodes clean). NodeType is the
	// resolved class name ("Document", "Attribute"); falls back to the numeric id. The
	// "Node*" prefix keeps these structural keys readable AND clear of an object's own
	// properties (which may legitimately be named Type / Id / Predefined / …).
	if (node.GetClsid() != 0) {
		key("NodeType");
		const ibString name = ResolveType(node.GetClsid());
		if (!name.IsEmpty()) Quote(out, name);
		else                 PutInteger(out, (unsigned long long)node.GetClsid());
	}
	if (node.GetMetaId() != 0) { key("NodeId"); PutInteger(out, (long long)node.GetMetaId()); }

	// data — fields then properties, each a direct key (areas don't collide in practice).
	// "typeId" holds a clsid (unsigned); right after it we inject a readable "TypeDesc"
	// = the resolved type name, so an entry reads { TypeId: <num>, TypeDesc: "String", … }.
	auto emitEntry = [&](const ibString& k, const ibDataValue& v) {
		namedKey(k);

		// ⭐ THE SAME COURTESY FOR A PACKED VALUE'S TYPE. A value writes its clsid under `t`
		// (value.h, kValueFieldClsid) — the id is what the READER needs, so it stays exactly as it
		// is; but on its own it tells a human nothing. `{"t": "104754294576172486", "v": 42}` was
		// the first thing a sandbox ever serialized, and seventeen digits is not an answer anybody
		// can act on (2026-09-02). The NAME goes beside it, the way `typeId` already gets `TypeDesc`.
		if (k.IsSameAs(kValueFieldClsid, false)) {

			EmitValue(v, out, inner);

			unsigned long long clsid = 0;
			if (v.Kind() == ibDataKind::Number)
				clsid = (unsigned long long)v.AsUInt();
			else if (v.Kind() == ibDataKind::String)
				v.AsString().ToULongLong(&clsid);

			if (clsid != 0) {
				const ibString name = ResolveType((ibClassID)clsid);
				if (!name.IsEmpty()) { key("type"); Quote(out, name); }
			}
		}
		// ⚠ AND THE ID ARRIVES AS TEXT NOW, NOT ONLY AS A NUMBER — so the courtesy has to know both
		// spellings or it quietly stops happening. A type description carries its clsid as a string
		// (ibTypeDescriptionMemory::WriteNode: sixty-four bits do not survive a JSON number on the
		// far side), and this branch, testing for Number, stopped injecting the readable name the
		// moment that changed. The clsid branch above already reads either; this one did not.
		else if (k.IsSameAs(wxT("typeId"), false)
			&& (v.Kind() == ibDataKind::Number || v.Kind() == ibDataKind::String)) {

			unsigned long long clsid = 0;

			if (v.Kind() == ibDataKind::Number) {
				clsid = (unsigned long long)v.AsUInt();
				// Written out in full rather than through EmitValue - the digits are the identity.
				PutInteger(out, clsid);
			}
			else {
				v.AsString().ToULongLong(&clsid);
				EmitValue(v, out, inner);
			}

			const ibString name = ResolveType((ibClassID)clsid);
			if (!name.IsEmpty()) { key("TypeDesc"); Quote(out, name); }
		}
		else {
			EmitValue(v, out, inner);
		}
	};
	for (const auto& f : node.Fields())     emitEntry(f.first, f.second);
	for (const auto& p : node.Properties())  emitEntry(p.first, p.second);

	// transitional opaque remainder
	if (node.HasRawData()) { key("NodeRaw"); PutBase64(out, node.RawData()); }

	// metaobject children (recursion)
	const std::vector<ibDataNode>& children = node.Children();
	if (!children.empty()) {
		key("NodeChildren");
		Put(out, m_compact ? "[" : "[\n");
		for (size_t i = 0; i < children.size(); i++) {
			Pad(out, inner + 1);
			EmitNode(children[i], out, inner + 1);
			Put(out, (i + 1 < children.size()) ? (m_compact ? "," : ",\n") : (m_compact ? "" : "\n"));
		}
		Pad(out, inner);
		Put(out, "]");
	}

	if (!m_compact)
		Put(out, "\n");
	Pad(out, depth);
	Put(out, "}");
}

////////////////////////////////////////////////////////////////////////////

// The room asked once, for what the tree weighs (ibDataNode::Weight): a text that grows as it is written is copied
// every time it does.
bool ibJsonProvider::Write(const ibDataNode& root, ibWriterMemory& writer) const
{
	std::string out;
	out.reserve(root.Weight());
	EmitNode(root, out, 0);
	if (!m_compact)
		Put(out, "\n");
	writer.w(out.data(), (std::uint32_t)out.size());
	return true;
}

bool ibJsonProvider::Write(const ibDataNode& root, ibString& text) const
{
	text.Clear();
	text.reserve(root.Weight());
	EmitNode(root, text, 0);
	if (!m_compact)
		Put(text, "\n");
	return true;
}

////////////////////////////////////////////////////////////////////////////
// Read — JSON text -> node tree
////////////////////////////////////////////////////////////////////////////

namespace {

// A decimal the fast road does not take, as the decimal reads it.
ibString NumberText(const char* p, size_t n) { return ibString::FromUTF8(p, n); }
ibString NumberText(const wchar_t* p, size_t n) { return ibString(p, n); }

// A small recursive-descent JSON reader over a contiguous buffer — of bytes (UTF-8, a socket's) or of the engine's
// characters (a text that never left the process): the same reading over either, and a text with no escape in it is
// taken from where it lies. Deliberately self-contained: the tree is the only thing that crosses out of here, so no
// third-party JSON dependency enters backend.dll for one provider.
//
// Errors are POSITIONAL and fatal — a half-parsed tree is worse than none, so
// every failure path returns false and the provider reports where it stopped.
template<class Char>
class ibJsonReader {
public:
	ibJsonReader(const ibJsonProvider& provider, const Char* begin, size_t size)
		: m_provider(provider), m_start(begin), m_p(begin), m_end(begin + size) {}

	size_t Offset() const { return (size_t)(m_p - m_start); }

	// The top level is one object = the root node.
	bool ParseNode(ibDataNode& node);

private:
	const ibJsonProvider& m_provider;   // what a key is to its node (ReadEntry)
	const Char* m_start;
	const Char* m_p;
	const Char* m_end;
	int m_depth = 0;

	// One container on this path. The destructor releases it on every return,
	// including a failure halfway through, so a sibling is not counted as deeper.
	// Past kMaxNesting the text is refused: every level is a frame of the descent,
	// and a few thousand brackets overflow the stack (measured near 5000 on a Linux
	// Debug build, when each level also copied its subtree; it moves it now).
	// A stack that runs low first refuses here too — the backstop for a fiber whose
	// reserve runs out before the count. Every level enters through here, so it is asked once a level.
	// Each refusal raises with its own reason: the text is not malformed, it is too deep.
	class Depth {
	public:
		explicit Depth(ibJsonReader& reader)
			: m_reader(reader)
		{
			if (m_reader.m_depth >= ibJsonProvider::kMaxNesting)
				ibCoreException::Error(_("ibJsonProvider: the JSON is nested deeper than %d levels at byte %d"),
					ibJsonProvider::kMaxNesting, (int)m_reader.Offset());
			if (ibFiber::StackLow())
				ibCoreException::Error(_("ibJsonProvider: the JSON is nested deeper than the stack allows"));
			++m_reader.m_depth;
		}
		~Depth() { --m_reader.m_depth; }
		Depth(const Depth&) = delete;
		Depth& operator=(const Depth&) = delete;
	private:
		ibJsonReader& m_reader;
	};

	bool Eof() const { return m_p >= m_end; }
	Char Peek() const { return Eof() ? Char(0) : *m_p; }

	void SkipWs() {
		while (!Eof() && (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r'))
			++m_p;
	}
	bool Accept(char c) { SkipWs(); if (Peek() == Char(c)) { ++m_p; return true; } return false; }
	bool Expect(char c) { return Accept(c); }

	bool ParseString(ibString& out);
	bool ParseNumber(ibNumber& out);
	bool ParseValue(ibDataValue& out);
	bool ParseArray(ibDataValue& out);
	bool ParseScalar(ibDataValue& out);
	bool ParseLiteral(const char* text) {
		SkipWs();
		const size_t n = std::strlen(text);
		if ((size_t)(m_end - m_p) < n) return false;
		for (size_t i = 0; i < n; ++i)
			if (m_p[i] != Char(text[i])) return false;
		m_p += n;
		return true;
	}
};

// JSON string -> the engine's text, as JSON's spelling is read back for the whole backend (ibJsonText).
template<class Char>
bool ibJsonReader<Char>::ParseString(ibString& out)
{
	SkipWs();
	return ibJsonText::ReadQuoted(m_p, m_end, out);
}

// Numbers go through ibNumber::FromString — the emitter writes the EXACT decimal
// (ibNumber::ToString), so parsing it back through the same type is lossless.
template<class Char>
bool ibJsonReader<Char>::ParseNumber(ibNumber& out)
{
	SkipWs();
	const Char* start = m_p;
	if (!Eof() && (*m_p == '-' || *m_p == '+')) ++m_p;
	while (!Eof() && ((*m_p >= '0' && *m_p <= '9') || *m_p == '.' || *m_p == 'e' || *m_p == 'E'
		|| ((*m_p == '-' || *m_p == '+') && (m_p[-1] == 'e' || m_p[-1] == 'E'))))
		++m_p;
	if (m_p == start) return false;

	// A whole number that fits — an id, a count, a flag of the protocol's — is read where it lies; a fraction, an
	// exponent or a longer one goes the decimal's way.
	const Char* digits = start;
	const bool negative = *digits == '-';
	if (negative || *digits == '+') ++digits;
	if (m_p - digits > 0 && m_p - digits <= 18
		&& std::all_of(digits, m_p, [](Char c) { return c >= '0' && c <= '9'; })) {
		long long whole = 0;
		for (const Char* d = digits; d != m_p; ++d)
			whole = whole * 10 + (*d - '0');
		out = ibNumber(negative ? -whole : whole);
		return true;
	}
	return out.FromString(NumberText(start, (size_t)(m_p - start)));
}

// ⭐ A LEVEL OF THE DESCENT WEIGHS WHAT A CONTAINER NEEDS, NOT WHAT EVERY KIND NEEDS. Without the
// optimiser (and more so under ASan) a function keeps a slot for every local and temporary of every
// branch, taken or not; when the scalars were read in here, each level of [[[…]]] carried a string, a
// number and the literals' values, and the deepest legal text no longer fit a 512 KB thread once
// the stack check kept its slack (CI, Linux Debug + ASan, 2026-10-13). So the kinds are read apart.
template<class Char>
bool ibJsonReader<Char>::ParseValue(ibDataValue& out)
{
	SkipWs();
	const Char c = Peek();

	if (c == '{') {
		std::shared_ptr<ibDataNode> child = std::make_shared<ibDataNode>();
		if (!ParseNode(*child)) return false;
		out = ibDataValue::Child(child);
		return true;
	}
	if (c == '[')
		return ParseArray(out);
	return ParseScalar(out);
}

template<class Char>
bool ibJsonReader<Char>::ParseArray(ibDataValue& out)
{
	Depth depth(*this);
	if (!Accept('[')) return false;
	std::vector<ibDataValue> items;
	if (!Accept(']')) {
		for (;;) {
			ibDataValue item;
			if (!ParseValue(item)) return false;
			items.push_back(std::move(item));
			if (Accept(',')) continue;
			if (Accept(']')) break;
			return false;
		}
	}
	out = ibDataValue::Array(std::move(items));
	return true;
}

// A string, a literal or a number — a value with nothing inside it.
template<class Char>
bool ibJsonReader<Char>::ParseScalar(ibDataValue& out)
{
	SkipWs();
	if (Peek() == '\"') {
		ibString s;
		if (!ParseString(s)) return false;
		out = ibJsonProvider::ReadString(s);
		return true;
	}
	if (ParseLiteral("true"))  { out = ibDataValue::Bool(true);  return true; }
	if (ParseLiteral("false")) { out = ibDataValue::Bool(false); return true; }
	if (ParseLiteral("null"))  { out = ibDataValue();            return true; }

	ibNumber n;
	if (!ParseNumber(n)) return false;
	out = ibDataValue::Number(n);
	return true;
}

template<class Char>
bool ibJsonReader<Char>::ParseNode(ibDataNode& node)
{
	Depth depth(*this);

	if (!Expect('{')) return false;
	if (Accept('}')) return true;   // {} — a legitimately empty node

	for (;;) {
		ibString key;
		if (!ParseString(key)) return false;
		if (!Expect(':')) return false;

		// The children are the reader's to descend into; every other key is the provider's rule (ReadEntry).
		static const ibString kNodeChildren = wxT("NodeChildren");
		if (key == kNodeChildren) {
			if (!Expect('[')) return false;
			if (!Accept(']')) {
				for (;;) {
					// AddChild wants the identity up front, but it sits INSIDE the child object.
					// Parse into a scratch node and move it onto the vector instead — same result,
					// no throwaway default-constructed slot.
					ibDataNode scratch;
					if (!ParseNode(scratch)) return false;
					node.Children().emplace_back(std::move(scratch));
					if (Accept(',')) continue;
					if (Accept(']')) break;
					return false;
				}
			}
		}
		else {
			ibDataValue v;
			if (!ParseValue(v)) return false;
			if (!m_provider.ReadEntry(node, key, std::move(v))) return false;
		}

		if (Accept(',')) continue;
		if (Accept('}')) return true;
		return false;
	}
}

} // namespace

// JSON text -> node tree. Complete and standalone: it parses everything Write emits,
// including nested children, arrays, base64 binaries and the synthetic keys. It is NOT
// wired into any load path — the round-trip path stays ibBinaryProvider — because the
// view is lossy in three places the parser cannot undo (header). Use it for tooling that
// reads a JSON dump back into a tree, not to load a configuration.
bool ibJsonProvider::Read(ibReaderMemory& reader, ibDataNode& root) const
{
	const int size = reader.elapsed();
	if (size <= 0)
		return false;

	const char* text = static_cast<const char*>(reader.pointer());
	ibJsonReader<char> parser(*this, text, (size_t)size);
	if (!parser.ParseNode(root)) {
		ibCoreException::Error(_("ibJsonProvider: malformed JSON at byte %d"),
			(int)parser.Offset());
		return false;   // not reached — Error throws
	}
	reader.advance(size);
	return true;
}

// …and from the characters themselves: the reader walks the text's own buffer.
bool ibJsonProvider::Read(const ibString& text, ibDataNode& root) const
{
	if (text.IsEmpty())
		return false;

	ibJsonReader<wchar_t> parser(*this, text.wc_str(), text.length());
	if (!parser.ParseNode(root)) {
		ibCoreException::Error(_("ibJsonProvider: malformed JSON at character %d"),
			(int)parser.Offset());
		return false;   // not reached — Error throws
	}
	return true;
}

bool ibJsonProvider::ReadEntry(ibDataNode& node, const ibString& key, ibDataValue value) const
{
	// The keys as the engine's strings: a key is told from one by its length first — against a literal each was measured
	// and walked, every key of every node (ibString::operator==, 2026-10-22).
	static const ibString kNodeType = wxT("NodeType"), kNodeId = wxT("NodeId"), kNodeRaw = wxT("NodeRaw"),
		kTypeDesc = wxT("TypeDesc");

	// --- the structural keys EmitNode writes (see the emitter for each) ------------
	if (key == kNodeType) {
		if (value.Kind() == ibDataKind::String) {
			// The emitter wrote the RESOLVED NAME. Turning it back into a clsid needs the
			// same knowledge in reverse; without it the node keeps clsid 0 rather than
			// inventing one — and the word, as its field, for a later lookup (LookupTypes).
			const ibClassID clsid = m_lookupType ? m_lookupType(value.AsString()) : ibClassID(0);
			if (clsid != ibClassID(0)) node.SetClsid(clsid);
			else                       node.AddField(key, std::move(value));
			return true;
		}
		if (value.Kind() != ibDataKind::Number) return false;
		node.SetClsid((ibClassID)value.AsUInt());
		return true;
	}
	if (key == kNodeId) {
		if (value.Kind() != ibDataKind::Number) return false;
		node.SetMetaId((ibMetaID)value.AsInt());
		return true;
	}
	if (key == kNodeRaw) {
		if (value.Kind() == ibDataKind::Binary) node.SetRawData(value.AsBinary());
		return true;
	}
	// SYNTHETIC — injected next to typeId purely so a reader sees "String" beside the
	// number. There is no node entry behind it; dropped.
	if (key == kTypeDesc)
		return true;

	// --- ordinary data ------------------------------------------------------------
	// Which AREA a key came from is not in the text (EmitNode flattens Fields and
	// Properties into one key set). Child values are placed back into the property
	// area because that is where ibDataNode::Child puts them by construction; every
	// other key lands in Fields. See the header.
	if (value.Kind() == ibDataKind::Child) node.SetProperty(key, std::move(value));
	else                                   node.AddField(key, std::move(value));
	return true;
}

ibDataValue ibJsonProvider::ReadString(const ibString& text)
{
	// "base64:<...>" is how EmitValue renders Binary — the one string shape that is
	// NOT a String. Everything else stays a String, INCLUDING an emitted Date (see
	// the header): an ISO string and a string that looks like one are the same text —
	// and so does one after the prefix that is not base64: EmitValue did not write it.
	static const ibString kB64 = wxT("base64:");
	if (text.StartsWith(kB64)) {
		ibMemoryBuffer bytes;
		if (ibBase64::Read(text.wc_str() + kB64.length(), text.length() - kB64.length(), bytes))
			return ibDataValue::Binary(bytes);
	}
	return ibDataValue::String(text);
}

namespace {

using ibTypeLookup = std::function<ibClassID(const ibString&)>;

void LookupKept(ibDataNode& node, const ibTypeLookup& lookupType);

// A value's nodes: the one it is, or those of its list.
void LookupKept(const ibDataValue& value, const ibTypeLookup& lookupType)
{
	if (value.Kind() == ibDataKind::Child) {
		if (value.AsChild())
			LookupKept(*value.AsChild(), lookupType);
	}
	else if (value.Kind() == ibDataKind::Array) {
		for (const ibDataValue& item : value.AsArray())
			LookupKept(item, lookupType);
	}
}

// The word a reading kept, looked up — known: the node's type, the field gone; not known: kept on, written back as read.
void LookupKept(ibDataNode& node, const ibTypeLookup& lookupType)
{
	if (node.GetClsid() == ibClassID(0)) {
		const ibDataValue* const word = node.FindField(wxT("NodeType"));
		if (word != nullptr && word->Kind() == ibDataKind::String) {
			const ibClassID clsid = lookupType(word->AsString());
			if (clsid != ibClassID(0)) {
				node.SetClsid(clsid);
				node.RemoveField(wxT("NodeType"));
			}
		}
	}
	for (const auto& field : node.Fields())
		LookupKept(field.second, lookupType);
	for (const auto& property : node.Properties())
		LookupKept(property.second, lookupType);
	for (ibDataNode& child : node.Children())
		LookupKept(child, lookupType);
}

} // namespace

void ibJsonProvider::LookupTypes(ibDataNode& node) const
{
	if (m_lookupType)
		LookupKept(node, m_lookupType);
}
