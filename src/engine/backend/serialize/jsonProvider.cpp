#include "backend/serialize/jsonProvider.h"
#include "backend/compiler/value.h"   // ibValue::GetAvailableCtor — clsid -> type name
#include "backend/objCtor.h"           // ibCtorAbstractType::GetClassName
#include <wx/base64.h>

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
		if (v.Kind() == ibDataKind::Number && k.IsSameAs(wxT("typeId"), false)) {
			out += std::to_string((unsigned long long)v.AsUInt());
			const wxString name = ResolveType((ibClassID)v.AsUInt());
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

bool ibJsonProvider::Read(ibReader& /*reader*/, ibDataNode& /*root*/) const
{
	// JSON -> node tree is a later step (import). Export is write-only for now.
	return false;
}
