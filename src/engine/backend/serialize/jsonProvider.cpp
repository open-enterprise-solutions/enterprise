#include "backend/serialize/jsonProvider.h"
#include "backend/compiler/value.h"   // ibValue::GetAvailableCtor — clsid -> type name
#include "backend/objCtor.h"           // ibCtorAbstractType::GetClassName
#include "backend/backend_exception.h" // ibBackendCoreException — malformed-JSON throw
#include <wx/base64.h>
#include <cstring>                     // strlen / strncmp — the JSON literal match

////////////////////////////////////////////////////////////////////////////

namespace {

void Indent(std::string& out, int depth) { out.append((size_t)depth * 2, ' '); }

// Built-in clsid -> name (Number, String, …) via the STATIC factory. Reference
// types are metadata-specific and resolve only through the injected resolver.
wxString BuiltinTypeName(ibClassID clsid) {
	if (ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(clsid))
		return ctor->GetClassName();
	return wxString();
}

// JSON string literal: quote + escape. UTF-8 bytes pass through (JSON is UTF-8).
std::string JsonString(const wxString& s) {
	const wxScopedCharBuffer utf8 = s.utf8_str();
	std::string r = "\"";
	for (const char* p = utf8.data(); *p; ++p) {
		const unsigned char c = (unsigned char)*p;
		switch (c) {
		case '\"': r += "\\\""; break;
		case '\\': r += "\\\\"; break;
		case '\n': r += "\\n";  break;
		case '\r': r += "\\r";  break;
		case '\t': r += "\\t";  break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				r += buf;
			}
			else {
				r += (char)c;
			}
		}
	}
	r += "\"";
	return r;
}

std::string Base64(const wxMemoryBuffer& b) {
	return JsonString(wxT("base64:") + wxBase64Encode(b.GetData(), b.GetDataLen()));
}

} // namespace

////////////////////////////////////////////////////////////////////////////

// Injected resolver first (knows the config's reference types), then the static
// built-in factory. Empty when the clsid is no registered type.
wxString ibJsonProvider::ResolveType(ibClassID clsid) const
{
	if (m_resolveType) {
		const wxString name = m_resolveType(clsid);
		if (!name.IsEmpty())
			return name;
	}
	return BuiltinTypeName(clsid);
}

void ibJsonProvider::EmitValue(const ibDataValue& v, std::string& out, int depth) const
{
	switch (v.Kind()) {
	case ibDataKind::Empty:  out += "null"; break;
	case ibDataKind::Bool:   out += v.AsBool() ? "true" : "false"; break;
	case ibDataKind::Number: out += std::string(v.AsNumber().ToString().utf8_str()); break; // exact decimal
	case ibDataKind::Date: {
		const s64 ms = v.AsDate();
		const wxDateTime dt((wxLongLong)ms);
		out += JsonString(ms && dt.IsValid() ? dt.FormatISOCombined(' ') : wxString()); // readable ISO
		break;
	}
	case ibDataKind::String: out += JsonString(v.AsString()); break;
	case ibDataKind::Binary: out += Base64(v.AsBinary()); break;
	case ibDataKind::Child: {
		const std::shared_ptr<ibDataNode>& c = v.AsChild();
		if (c) EmitNode(*c, out, depth);
		else   out += "null";
		break;
	}
	case ibDataKind::Array: {
		const std::vector<ibDataValue>& a = v.AsArray();
		if (a.empty()) { out += "[]"; break; }
		out += "[\n";
		for (size_t i = 0; i < a.size(); i++) {
			Indent(out, depth + 1);
			EmitValue(a[i], out, depth + 1);
			out += (i + 1 < a.size()) ? ",\n" : "\n";
		}
		Indent(out, depth);
		out += "]";
		break;
	}
	}
}

void ibJsonProvider::EmitNode(const ibDataNode& node, std::string& out, int depth) const
{
	out += "{\n";
	bool first = true;
	const int inner = depth + 1;

	auto key = [&](const char* keyLiteral) {
		if (!first) out += ",\n";
		first = false;
		Indent(out, inner);
		out += "\"";
		out += keyLiteral;
		out += "\": ";
	};
	auto namedKey = [&](const wxString& name) {
		if (!first) out += ",\n";
		first = false;
		Indent(out, inner);
		out += JsonString(name);
		out += ": ";
	};

	// identity (only when meaningful — keeps Child sub-nodes clean). NodeType is the
	// resolved class name ("Document", "Attribute"); falls back to the numeric id. The
	// "Node*" prefix keeps these structural keys readable AND clear of an object's own
	// properties (which may legitimately be named Type / Id / Predefined / …).
	if (node.GetClsid() != 0) {
		key("NodeType");
		const wxString name = ResolveType(node.GetClsid());
		if (!name.IsEmpty()) out += JsonString(name);
		else                 out += std::to_string((unsigned long long)node.GetClsid());
	}
	if (node.GetMetaId() != 0) { key("NodeId"); out += std::to_string((long long)node.GetMetaId()); }

	// data — fields then properties, each a direct key (areas don't collide in practice).
	// "typeId" holds a clsid (unsigned); right after it we inject a readable "TypeDesc"
	// = the resolved type name, so an entry reads { TypeId: <num>, TypeDesc: "String", … }.
	auto emitEntry = [&](const wxString& k, const ibDataValue& v) {
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
				const wxString name = ResolveType((ibClassID)clsid);
				if (!name.IsEmpty()) { namedKey(wxT("type")); out += JsonString(name); }
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
				out += std::to_string(clsid);
			}
			else {
				v.AsString().ToULongLong(&clsid);
				EmitValue(v, out, inner);
			}

			const wxString name = ResolveType((ibClassID)clsid);
			if (!name.IsEmpty()) { namedKey(wxT("TypeDesc")); out += JsonString(name); }
		}
		else {
			EmitValue(v, out, inner);
		}
	};
	for (const auto& f : node.Fields())     emitEntry(f.first, f.second);
	for (const auto& p : node.Properties())  emitEntry(p.first, p.second);

	// transitional opaque remainder
	if (node.HasRawData()) { key("NodeRaw"); out += Base64(node.RawData()); }

	// metaobject children (recursion)
	const std::vector<ibDataNode>& children = node.Children();
	if (!children.empty()) {
		key("NodeChildren");
		out += "[\n";
		for (size_t i = 0; i < children.size(); i++) {
			Indent(out, inner + 1);
			EmitNode(children[i], out, inner + 1);
			out += (i + 1 < children.size()) ? ",\n" : "\n";
		}
		Indent(out, inner);
		out += "]";
	}

	out += "\n";
	Indent(out, depth);
	out += "}";
}

////////////////////////////////////////////////////////////////////////////

bool ibJsonProvider::Write(const ibDataNode& root, ibWriter& writer) const
{
	std::string out;
	EmitNode(root, out, 0);
	out += "\n";
	writer.w(out.data(), (u32)out.size());
	return true;
}

////////////////////////////////////////////////////////////////////////////
// Read — JSON text -> node tree
////////////////////////////////////////////////////////////////////////////

namespace {

// A small recursive-descent JSON reader over a contiguous buffer. Deliberately
// self-contained: the tree is the only thing that crosses out of here, so no
// third-party JSON dependency enters backend.dll for one provider.
//
// Errors are POSITIONAL and fatal — a half-parsed tree is worse than none, so
// every failure path returns false and the provider reports where it stopped.
class ibJsonReader {
public:
	ibJsonReader(const char* begin, size_t size) : m_start(begin), m_p(begin), m_end(begin + size) {}

	size_t Offset() const { return (size_t)(m_p - m_start); }

	// The top level is one object = the root node.
	bool ParseNode(ibDataNode& node,
		const std::function<ibClassID(const wxString&)>& lookupType);

private:
	const char* m_start;
	const char* m_p;
	const char* m_end;

	bool Eof() const { return m_p >= m_end; }
	char Peek() const { return Eof() ? '\0' : *m_p; }

	void SkipWs() {
		while (!Eof() && (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r'))
			++m_p;
	}
	bool Accept(char c) { SkipWs(); if (Peek() == c) { ++m_p; return true; } return false; }
	bool Expect(char c) { return Accept(c); }

	bool ParseString(wxString& out);
	bool ParseNumber(ibNumber& out);
	bool ParseValue(ibDataValue& out, const std::function<ibClassID(const wxString&)>& lookupType);
	bool ParseLiteral(const char* text) {
		SkipWs();
		const size_t n = strlen(text);
		if ((size_t)(m_end - m_p) < n || strncmp(m_p, text, n) != 0) return false;
		m_p += n;
		return true;
	}
};

// JSON string -> wxString, unescaping the same set JsonString escapes (plus \/ and \b\f,
// which a hand-edited file may carry). \uXXXX is decoded as a UTF-16 code unit, with the
// surrogate pair joined — the emitter only ever produces \u for control characters, but a
// human-edited view may hold anything.
bool ibJsonReader::ParseString(wxString& out)
{
	if (!Accept('\"')) return false;
	std::string utf8;
	while (!Eof()) {
		const char c = *m_p++;
		if (c == '\"') {
			out = wxString::FromUTF8(utf8.data(), utf8.size());
			return true;
		}
		if (c != '\\') { utf8 += c; continue; }
		if (Eof()) return false;
		const char e = *m_p++;
		switch (e) {
		case '\"': utf8 += '\"'; break;
		case '\\': utf8 += '\\'; break;
		case '/':  utf8 += '/';  break;
		case 'b':  utf8 += '\b'; break;
		case 'f':  utf8 += '\f'; break;
		case 'n':  utf8 += '\n'; break;
		case 'r':  utf8 += '\r'; break;
		case 't':  utf8 += '\t'; break;
		case 'u': {
			if ((size_t)(m_end - m_p) < 4) return false;
			auto hex4 = [this]() -> int {
				int v = 0;
				for (int i = 0; i < 4; i++) {
					const char h = *m_p++;
					v <<= 4;
					if      (h >= '0' && h <= '9') v |= (h - '0');
					else if (h >= 'a' && h <= 'f') v |= (h - 'a' + 10);
					else if (h >= 'A' && h <= 'F') v |= (h - 'A' + 10);
					else return -1;
				}
				return v;
			};
			const int hi = hex4();
			if (hi < 0) return false;
			wxUniChar ch((wxChar32)hi);
			if (hi >= 0xD800 && hi <= 0xDBFF && (size_t)(m_end - m_p) >= 6 && m_p[0] == '\\' && m_p[1] == 'u') {
				m_p += 2;
				const int lo = hex4();
				if (lo < 0) return false;
				ch = wxUniChar((wxChar32)(0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00)));
			}
			utf8 += wxString(ch).utf8_str().data();
			break;
		}
		default: return false;
		}
	}
	return false;   // unterminated
}

// Numbers go through ibNumber::FromString — the emitter writes the EXACT decimal
// (ibNumber::ToString), so parsing it back through the same type is lossless.
bool ibJsonReader::ParseNumber(ibNumber& out)
{
	SkipWs();
	const char* start = m_p;
	if (!Eof() && (*m_p == '-' || *m_p == '+')) ++m_p;
	while (!Eof() && ((*m_p >= '0' && *m_p <= '9') || *m_p == '.' || *m_p == 'e' || *m_p == 'E'
		|| ((*m_p == '-' || *m_p == '+') && (m_p[-1] == 'e' || m_p[-1] == 'E'))))
		++m_p;
	if (m_p == start) return false;
	return out.FromString(wxString::FromUTF8(start, (size_t)(m_p - start)));
}

bool ibJsonReader::ParseValue(ibDataValue& out, const std::function<ibClassID(const wxString&)>& lookupType)
{
	SkipWs();
	const char c = Peek();

	if (c == '\"') {
		wxString s;
		if (!ParseString(s)) return false;
		// "base64:<...>" is how EmitValue renders Binary — the one string shape that is
		// NOT a String. Everything else stays a String, INCLUDING an emitted Date (see
		// the header): an ISO string and a string that looks like one are the same text.
		static const wxString kB64 = wxT("base64:");
		if (s.StartsWith(kB64))
			out = ibDataValue::Binary(wxBase64Decode(s.Mid(kB64.length())));
		else
			out = ibDataValue::String(s);
		return true;
	}
	if (c == '{') {
		std::shared_ptr<ibDataNode> child = std::make_shared<ibDataNode>();
		if (!ParseNode(*child, lookupType)) return false;
		out = ibDataValue::Child(child);
		return true;
	}
	if (c == '[') {
		if (!Accept('[')) return false;
		std::vector<ibDataValue> items;
		if (Accept(']')) { out = ibDataValue::Array(items); return true; }
		for (;;) {
			ibDataValue item;
			if (!ParseValue(item, lookupType)) return false;
			items.push_back(item);
			if (Accept(',')) continue;
			if (Accept(']')) break;
			return false;
		}
		out = ibDataValue::Array(items);
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

bool ibJsonReader::ParseNode(ibDataNode& node, const std::function<ibClassID(const wxString&)>& lookupType)
{
	if (!Expect('{')) return false;
	if (Accept('}')) return true;   // {} — a legitimately empty node

	for (;;) {
		wxString key;
		if (!ParseString(key)) return false;
		if (!Expect(':')) return false;

		// --- the structural keys EmitNode writes (see the emitter for each) ------------
		if (key == wxT("NodeType")) {
			SkipWs();
			if (Peek() == '\"') {
				wxString name;
				if (!ParseString(name)) return false;
				// The emitter wrote the RESOLVED NAME. Turning it back into a clsid needs the
				// same knowledge in reverse; without a lookup the node keeps clsid 0 rather
				// than inventing one.
				if (lookupType) node.SetClsid(lookupType(name));
			}
			else {
				ibNumber n;
				if (!ParseNumber(n)) return false;
				u64 raw = 0; n.ToInt(raw);
				node.SetClsid((ibClassID)raw);
			}
		}
		else if (key == wxT("NodeId")) {
			ibNumber n;
			if (!ParseNumber(n)) return false;
			s64 raw = 0; n.ToInt(raw);
			node.SetMetaId((ibMetaID)raw);
		}
		else if (key == wxT("NodeRaw")) {
			ibDataValue v;
			if (!ParseValue(v, lookupType)) return false;
			if (v.Kind() == ibDataKind::Binary) node.SetRawData(v.AsBinary());
		}
		else if (key == wxT("NodeChildren")) {
			if (!Expect('[')) return false;
			if (!Accept(']')) {
				for (;;) {
					// AddChild wants the identity up front, but it sits INSIDE the child object.
					// Parse into a scratch node and move it onto the vector instead — same result,
					// no throwaway default-constructed slot.
					ibDataNode scratch;
					if (!ParseNode(scratch, lookupType)) return false;
					node.Children().emplace_back(std::move(scratch));
					if (Accept(',')) continue;
					if (Accept(']')) break;
					return false;
				}
			}
		}
		else if (key == wxT("TypeDesc")) {
			// SYNTHETIC — injected next to typeId purely so a reader sees "String" beside the
			// number. There is no node entry behind it; parse and drop.
			ibDataValue discard;
			if (!ParseValue(discard, lookupType)) return false;
		}
		// --- ordinary data ------------------------------------------------------------
		else {
			ibDataValue v;
			if (!ParseValue(v, lookupType)) return false;
			// Which AREA a key came from is not in the text (EmitNode flattens Fields and
			// Properties into one key set). Child values are placed back into the property
			// area because that is where ibDataNode::Child puts them by construction; every
			// other key lands in Fields. See the header.
			if (v.Kind() == ibDataKind::Child) node.SetProperty(key, v);
			else                               node.AddField(key, v);
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
bool ibJsonProvider::Read(ibReader& reader, ibDataNode& root) const
{
	const int size = reader.elapsed();
	if (size <= 0)
		return false;

	const char* text = static_cast<const char*>(reader.pointer());
	ibJsonReader parser(text, (size_t)size);
	if (!parser.ParseNode(root, m_lookupType)) {
		ibBackendCoreException::Error(_("ibJsonProvider: malformed JSON at byte %d"),
			(int)parser.Offset());
		return false;   // not reached — Error throws
	}
	reader.advance(size);
	return true;
}
