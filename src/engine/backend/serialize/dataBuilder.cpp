////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibDataNode value layer + ibBinaryProvider (the internal,
//	              positional format). See serialize/dataBuilder.h.
////////////////////////////////////////////////////////////////////////////

#include "backend/serialize/dataBuilder.h"
#include "backend/backend_exception.h"   // ibBackendCoreException — kind-mismatch throw

#include <cstring>                       // std::memcmp — two packed values compared as bytes
#include <wx/datetime.h>                  // a format-1 node's instant, read by its local parts (ReadEntry)

////////////////////////////////////////////////////////////////////////////
// ibDataValue — typed factories
////////////////////////////////////////////////////////////////////////////
void ibDataValue::Expect(ibDataKind expected) const {
	// The payload stands at the place of its kind — one alternative a kind, in ibDataKind's order (Kind).
	static_assert(std::variant_size_v<decltype(m_payload)> == static_cast<size_t>(ibDataKind::Array) + 1,
		"one alternative a kind, in ibDataKind's order");
	static_assert(std::is_same_v<std::variant_alternative_t<static_cast<size_t>(ibDataKind::String), decltype(m_payload)>, wxString>
		&& std::is_same_v<std::variant_alternative_t<static_cast<size_t>(ibDataKind::Array), decltype(m_payload)>, std::vector<ibDataValue>>,
		"one alternative a kind, in ibDataKind's order");

	// Empty (an absent field) is allowed — As* yields the default, so a missing tag
	// loads as a default, never an error. Only a present, WRONG kind throws.
	if (Kind() != ibDataKind::Empty && Kind() != expected)
		ibBackendCoreException::Error(_("ibDataValue: wrong value kind (expected %d, got %d)"),
			(int)expected, (int)Kind());
}

wxString ibDataValue::AsString() const {
	Expect(ibDataKind::String);
	const wxString* const text = std::get_if<wxString>(&m_payload);
	return text != nullptr ? *text : wxString();
}

bool ibDataValue::AsBool() const {
	Expect(ibDataKind::Bool);
	const bool* const value = std::get_if<bool>(&m_payload);
	return value != nullptr && *value;
}

const ibNumber& ibDataValue::AsNumber() const {
	Expect(ibDataKind::Number);
	static const ibNumber s_zero;
	const ibNumber* const number = std::get_if<ibNumber>(&m_payload);
	return number != nullptr ? *number : s_zero;
}

ibDateTime ibDataValue::AsDate() const {
	Expect(ibDataKind::Date);
	const ibDateTime* const date = std::get_if<ibDateTime>(&m_payload);
	return date != nullptr ? *date : ibDateTime();
}

const wxMemoryBuffer& ibDataValue::AsBinary() const {
	Expect(ibDataKind::Binary);
	static const wxMemoryBuffer s_empty(0);
	const wxMemoryBuffer* const data = std::get_if<wxMemoryBuffer>(&m_payload);
	return data != nullptr ? *data : s_empty;
}

const std::shared_ptr<ibDataNode>& ibDataValue::AsChild() const {
	Expect(ibDataKind::Child);
	static const std::shared_ptr<ibDataNode> s_none;
	const std::shared_ptr<ibDataNode>* const child = std::get_if<std::shared_ptr<ibDataNode>>(&m_payload);
	return child != nullptr ? *child : s_none;
}

const std::vector<ibDataValue>& ibDataValue::AsArray() const {
	Expect(ibDataKind::Array);
	static const std::vector<ibDataValue> s_none;
	const std::vector<ibDataValue>* const items = std::get_if<std::vector<ibDataValue>>(&m_payload);
	return items != nullptr ? *items : s_none;
}

// ⭐⭐ EQUALITY OF PACKED VALUES — see the header. A schema lives entirely on serialisation, so "is
// this the same value" is a question about what was written, answered without building anything.
bool ibDataValue::operator==(const ibDataValue& other) const
{
	if (Kind() != other.Kind())
		return false;

	switch (Kind()) {
	case ibDataKind::Empty:  return true;
	case ibDataKind::String: return std::get<wxString>(m_payload) == std::get<wxString>(other.m_payload);
	case ibDataKind::Bool:   return std::get<bool>(m_payload) == std::get<bool>(other.m_payload);
	case ibDataKind::Number: return std::get<ibNumber>(m_payload) == std::get<ibNumber>(other.m_payload);
	case ibDataKind::Date:   return std::get<ibDateTime>(m_payload) == std::get<ibDateTime>(other.m_payload);
	case ibDataKind::Binary: {
		const wxMemoryBuffer& data = std::get<wxMemoryBuffer>(m_payload);
		const wxMemoryBuffer& otherData = std::get<wxMemoryBuffer>(other.m_payload);
		return data.GetDataLen() == otherData.GetDataLen()
			&& (data.GetDataLen() == 0 || std::memcmp(data.GetData(), otherData.GetData(), data.GetDataLen()) == 0);
	}
	case ibDataKind::Child: {
		// Two nodes, or two absences — a child held by pointer is compared by what it HOLDS.
		const std::shared_ptr<ibDataNode>& child = std::get<std::shared_ptr<ibDataNode>>(m_payload);
		const std::shared_ptr<ibDataNode>& otherChild = std::get<std::shared_ptr<ibDataNode>>(other.m_payload);
		if (!child || !otherChild)
			return !child && !otherChild;
		return *child == *otherChild;
	}
	case ibDataKind::Array:  return std::get<std::vector<ibDataValue>>(m_payload) == std::get<std::vector<ibDataValue>>(other.m_payload);
	}
	return false;
}

bool ibDataNode::operator==(const ibDataNode& other) const
{
	if (m_clsid != other.m_clsid || m_metaId != other.m_metaId)
		return false;
	if (m_fields != other.m_fields || m_props != other.m_props)
		return false;
	if (m_rawData.GetDataLen() != other.m_rawData.GetDataLen()
		|| (m_rawData.GetDataLen() != 0
			&& std::memcmp(m_rawData.GetData(), other.m_rawData.GetData(), m_rawData.GetDataLen()) != 0))
		return false;
	return m_children == other.m_children;
}

std::size_t ibDataValue::Weight() const
{
	switch (Kind()) {
	case ibDataKind::String: return std::get<wxString>(m_payload).length() + 2;
	case ibDataKind::Binary: return std::get<wxMemoryBuffer>(m_payload).GetDataLen();
	case ibDataKind::Child: {
		const std::shared_ptr<ibDataNode>& child = std::get<std::shared_ptr<ibDataNode>>(m_payload);
		return child ? child->Weight() : 1;
	}
	case ibDataKind::Array: {
		std::size_t weight = 2;
		for (const ibDataValue& item : std::get<std::vector<ibDataValue>>(m_payload))
			weight += item.Weight() + 1;
		return weight;
	}
	default:                 return 4;
	}
}

std::size_t ibDataNode::Weight() const
{
	std::size_t weight = 2 + m_rawData.GetDataLen();
	for (const auto& entry : m_fields)
		weight += entry.first.length() + entry.second.Weight();
	for (const auto& entry : m_props)
		weight += entry.first.length() + entry.second.Weight();
	for (const ibDataNode& child : m_children)
		weight += child.Weight();
	return weight;
}

// Each payload put in by its own type, never converted into one: a variant holding a bool takes a pointer or a text
// as a bool if left to choose.
ibDataValue ibDataValue::String(const wxString& text) {
	ibDataValue v; v.m_payload.emplace<wxString>(text); return v;
}
ibDataValue ibDataValue::Bool(bool value) {
	ibDataValue v; v.m_payload.emplace<bool>(value); return v;
}
ibDataValue ibDataValue::Number(const ibNumber& value) {
	ibDataValue v; v.m_payload.emplace<ibNumber>(value); return v;
}
ibDataValue ibDataValue::Int(s64 value) {
	ibDataValue v; v.m_payload.emplace<ibNumber>((int64_t)value); return v;
}
ibDataValue ibDataValue::UInt(u64 value) {
	ibDataValue v; v.m_payload.emplace<ibNumber>((uint64_t)value); return v;
}
ibDataValue ibDataValue::Date(const ibDateTime& date) {
	ibDataValue v; v.m_payload.emplace<ibDateTime>(date); return v;
}
ibDataValue ibDataValue::Binary(const wxMemoryBuffer& data) {
	ibDataValue v; v.m_payload.emplace<wxMemoryBuffer>(data); return v;
}
ibDataValue ibDataValue::Child(const std::shared_ptr<ibDataNode>& child) {
	ibDataValue v; v.m_payload.emplace<std::shared_ptr<ibDataNode>>(child); return v;
}
ibDataValue ibDataValue::Array(const std::vector<ibDataValue>& items) {
	ibDataValue v; v.m_payload.emplace<std::vector<ibDataValue>>(items); return v;
}

////////////////////////////////////////////////////////////////////////////
// ibDataNode — named-field lookup (optimistic cursor) + dual Field
////////////////////////////////////////////////////////////////////////////
const ibDataValue* ibDataNode::FindField(const wxString& name) const {
	const size_t n = m_fields.size();
	if (n == 0)
		return nullptr;
	// fast path: the expected (write-order) position
	if (m_cursor < n && m_fields[m_cursor].first == name)
		return &m_fields[m_cursor++].second;
	// slow path: search from the cursor, wrapping once
	for (size_t i = 0; i < n; i++) {
		const size_t idx = (m_cursor + i) % n;
		if (m_fields[idx].first == name) {
			m_cursor = idx + 1;
			return &m_fields[idx].second;
		}
	}
	// absent: cursor unmoved -> the member keeps its default, never an error
	return nullptr;
}

const ibDataValue* ibDataNode::FindProperty(const wxString& name) const {
	const size_t n = m_props.size();
	if (n == 0)
		return nullptr;
	if (m_propCursor < n && m_props[m_propCursor].first == name)
		return &m_props[m_propCursor++].second;
	for (size_t i = 0; i < n; i++) {
		const size_t idx = (m_propCursor + i) % n;
		if (m_props[idx].first == name) {
			m_propCursor = idx + 1;
			return &m_props[idx].second;
		}
	}
	return nullptr;
}

ibDataNode& ibDataNode::Child(const wxString& name) {
	// save: a fresh composite sub-node under `name`, returned for the caller to fill
	auto child = std::make_shared<ibDataNode>();
	SetProperty(name, ibDataValue::Child(child));
	return *child;
}

const ibDataNode* ibDataNode::FindChild(const wxString& name) const {
	// load: the parsed composite sub-node, or null if absent
	if (const ibDataValue* v = FindProperty(name)) {
		if (const std::shared_ptr<ibDataNode>& c = v->AsChild())
			return c.get();
	}
	return nullptr;
}

// The same lookup for a writer that means to KEEP FILLING a sub-node it may already have made —
// Child() would replace it. No cast: the value holds a shared_ptr to a node that was never const,
// so the pointer inside it comes out non-const on its own.
ibDataNode* ibDataNode::FindChild(const wxString& name) {
	if (const ibDataValue* v = FindProperty(name)) {
		if (const std::shared_ptr<ibDataNode>& c = v->AsChild())
			return c.get();
	}
	return nullptr;
}

////////////////////////////////////////////////////////////////////////////
// ibBinaryProvider — internal OWNED format (see the header for the layout).
// Block ids are a binary-format detail (they live here, not on the node).
////////////////////////////////////////////////////////////////////////////
namespace {
	const u64 kMetaBlock   = 0x2350; // node's own data: version + fields + props + raw remainder
	const u64 kChildBlock  = 0x2370; // node's children (full forms)
	// Per-node stamp; bump on any field-encoding change. A Date entry, by the stamp (ReadEntry):
	//   3 (2026-09-30) - an ibDateTime's count: milliseconds of the wall from 0001-01-01 (fdatetime.h);
	//   2              - the same reading counted from 1970-01-01, the first form of it (PR #217);
	//   1              - an instant of the writing machine's clock.
	const u32 kFormatVersion = 3;
}

// A named entry = name + value payload (used by the fields / properties areas).
void ibBinaryProvider::WriteEntry(ibWriter& writer, const wxString& name, const ibDataValue& v) const {
	writer.w_stringZ(name);
	WriteValue(writer, v);
}

// A value payload = kind + value. Composites recurse: a Child writes its sub-node
// (fields + props); an Array writes its items (each a nameless value). This is the
// JSON-shaped model — scalars, objects (Child), arrays (Array). Binary is a leaf.
void ibBinaryProvider::WriteValue(ibWriter& writer, const ibDataValue& v) const {
	writer.w_u8((u8)v.Kind());
	switch (v.Kind()) {
	case ibDataKind::String: writer.w_stringZ(v.AsString()); break;
	case ibDataKind::Bool:   writer.w_u8(v.AsBool() ? 1 : 0); break;
	case ibDataKind::Date:   writer.w_s64(v.AsDate().GetValue()); break;
	case ibDataKind::Number: {
		const wxMemoryBuffer b = v.AsNumber().GetBuffer();
		writer.w_u32((u32)b.GetDataLen());
		if (b.GetDataLen())
			writer.w(b.GetData(), (u32)b.GetDataLen());
		break;
	}
	case ibDataKind::Binary: {
		const wxMemoryBuffer& b = v.AsBinary();
		writer.w_u32((u32)b.GetDataLen());
		if (b.GetDataLen())
			writer.w(b.GetData(), (u32)b.GetDataLen());
		break;
	}
	case ibDataKind::Child: {
		const std::shared_ptr<ibDataNode>& c = v.AsChild();
		WriteFields(*c, writer);
		WriteProps(*c, writer);
		WriteChildren(*c, writer);   // a Child value carries its WHOLE subtree (e.g. a form's control tree)
		break;
	}
	case ibDataKind::Array: {
		const std::vector<ibDataValue>& a = v.AsArray();
		writer.w_u32((u32)a.size());
		for (const ibDataValue& item : a)
			WriteValue(writer, item);
		break;
	}
	default: break; // Empty
	}
}

ibDataValue ibBinaryProvider::ReadEntry(ibReader& reader, u32 version) const {
	const ibDataKind kind = (ibDataKind)reader.r_u8();
	switch (kind) {
	case ibDataKind::String: return ibDataValue::String(reader.r_stringZ());
	case ibDataKind::Bool:   return ibDataValue::Bool(reader.r_u8() != 0);
	case ibDataKind::Date: {
		// ⭐ THE ONE DOOR A STORED DATE COMES BACK THROUGH, and where an older node is read (the stamp,
		// above). Format 3 holds the ibDateTime's own count. Format 2 held the same wall reading counted
		// from 1970-01-01, where 0 stood for "no date" (an invalid wxDateTime) - the empty date now. Format
		// 1 held an INSTANT of the writing machine's clock, with the empty date the literal
		// -62135604000000 and 0 for "no date": both are the empty date, and any other instant crosses the
		// bridge - the parts THIS machine's clock shows for it, which are the parts the writer saw wherever
		// this machine stands in the writer's zone (a configuration and its files rarely change zones).
		const s64 stored = reader.r_s64();
		if (version >= 3)
			return ibDataValue::Date(ibDateTime(stored));
		if (stored == 0 || (version == 1 && stored == -62135604000000ll))
			return ibDataValue::Date(ibDateTime());
		if (version == 2)
			return ibDataValue::Date(ibDateTime(1970, 1, 1).AddMilliseconds(stored));
		return ibDataValue::Date(ibDateTime::OfWxDateTime(wxDateTime(wxLongLong(stored))));
	}
	case ibDataKind::Number: {
		const u32 len = reader.r_u32();
		wxMemoryBuffer b;
		if (len) {
			reader.r(b.GetAppendBuf(len), len);
			b.UngetAppendBuf(len);
		}
		ibNumber n; n.SetBuffer(b);
		return ibDataValue::Number(n);
	}
	case ibDataKind::Binary: {
		const u32 len = reader.r_u32();
		wxMemoryBuffer b;
		if (len) {
			reader.r(b.GetAppendBuf(len), len);
			b.UngetAppendBuf(len);
		}
		return ibDataValue::Binary(b);
	}
	case ibDataKind::Child: {
		auto c = std::make_shared<ibDataNode>();
		ReadFields(reader, *c, version);
		ReadProps(reader, *c, version);
		ReadChildren(reader, *c, version);
		return ibDataValue::Child(c);
	}
	case ibDataKind::Array: {
		const u32 count = reader.r_u32();
		std::vector<ibDataValue> a;
		a.reserve(count);
		for (u32 i = 0; i < count; i++)
			a.push_back(ReadEntry(reader, version));
		return ibDataValue::Array(a);
	}
	default: return ibDataValue();
	}
}

void ibBinaryProvider::WriteFields(const ibDataNode& node, ibWriter& writer) const {
	const auto& fields = node.Fields();
	writer.w_u32((u32)fields.size());
	for (const auto& f : fields)
		WriteEntry(writer, f.first, f.second);
}

void ibBinaryProvider::ReadFields(ibReader& reader, ibDataNode& node, u32 version) const {
	const u32 count = reader.r_u32();
	for (u32 i = 0; i < count; i++) {
		wxString name = reader.r_stringZ();
		node.AddField(name, ReadEntry(reader, version));
	}
}

void ibBinaryProvider::WriteProps(const ibDataNode& node, ibWriter& writer) const {
	const auto& props = node.Properties();
	writer.w_u32((u32)props.size());
	for (const auto& p : props)
		WriteEntry(writer, p.first, p.second);
}

void ibBinaryProvider::ReadProps(ibReader& reader, ibDataNode& node, u32 version) const {
	const u32 count = reader.r_u32();
	for (u32 i = 0; i < count; i++) {
		wxString name = reader.r_stringZ();
		node.SetProperty(name, ReadEntry(reader, version));
	}
}

// A Child VALUE's subtree: each child positionally (clsid + metaId + fields + props + its
// own children, recursive). This is what lets a whole control tree — a form's header plus
// its subordinate controls — ride inside ONE property value. Distinct from the top-level
// chunk-framed WriteNode/kChildBlock used for the metaobject tree.
void ibBinaryProvider::WriteChildren(const ibDataNode& node, ibWriter& writer) const {
	const auto& children = node.Children();
	writer.w_u32((u32)children.size());
	for (const ibDataNode& child : children) {
		writer.w_u64((u64)child.GetClsid());
		writer.w_s32((s32)child.GetMetaId());
		WriteFields(child, writer);
		WriteProps(child, writer);
		WriteChildren(child, writer);
	}
}

void ibBinaryProvider::ReadChildren(ibReader& reader, ibDataNode& node, u32 version) const {
	const u32 count = reader.r_u32();
	for (u32 i = 0; i < count; i++) {
		const ibClassID clsid  = (ibClassID)reader.r_u64();
		const ibMetaID  metaId = (ibMetaID)reader.r_s32();
		ibDataNode& child = node.AddChild(clsid, metaId);
		ReadFields(reader, child, version);
		ReadProps(reader, child, version);
		ReadChildren(reader, child, version);
	}
}

// Node CONTENT: { chunk(kMetaBlock){version+fields+props+raw}, chunk(kChildBlock){children} }.
// NO identity frame — the root's clsid/metaId frame is the CONTAINER's job (config frames the
// root with its known clsid; a standalone node like a form control needs none). This is the
// exact INNER that ReadNode consumes, so Write and Read are true inverses. CHILDREN inside
// kChildBlock ARE framed (WriteNode) so the load can iterate them by clsid/metaId.
void ibBinaryProvider::WriteInner(const ibDataNode& node, ibWriter& writer) const {
	// meta block = version + fields + props + raw remainder
	ibWriterMemory metaBuf;
	metaBuf.w_u32(kFormatVersion);
	WriteFields(node, metaBuf);
	WriteProps(node, metaBuf);
	const wxMemoryBuffer& raw = node.RawData();
	metaBuf.w_u32((u32)raw.GetDataLen());
	if (raw.GetDataLen())
		metaBuf.w(raw.GetData(), (u32)raw.GetDataLen());

	// child block = children as full (identity-framed) forms
	ibWriterMemory childBuf;
	for (const ibDataNode& child : node.Children())
		WriteNode(child, childBuf);

	writer.w_chunk(kMetaBlock, metaBuf.pointer(), metaBuf.size());
	writer.w_chunk(kChildBlock, childBuf.pointer(), childBuf.size());
}

// A node framed by identity: chunk(clsid){ chunk(metaId){ inner } }. The CHILD form — the
// parent's ReadNode iterates kChildBlock by clsid/metaId and reads each child's inner.
void ibBinaryProvider::WriteNode(const ibDataNode& node, ibWriter& writer) const {
	ibWriterMemory inner;
	WriteInner(node, inner);

	ibWriterMemory meta;
	meta.w_chunk((u64)node.GetMetaId(), inner.pointer(), inner.size());
	writer.w_chunk((u64)node.GetClsid(), meta.pointer(), meta.size());
}

void ibBinaryProvider::ReadNode(ibReader& reader, ibDataNode& node) const {
	// `reader` is this node's INNER content { kMetaBlock, kChildBlock }.
	// Children — each a full form chunk(clsid){chunk(metaId){inner}}. The
	// iterator self-closes the previous reader on every call (incl. the final
	// null-returning one), so the whole chain is freed without explicit close.
	if (ibReader* childBlock = reader.open_chunk(kChildBlock)) {
		ibReader* prevClsid = nullptr;
		for (;;) {
			u64 clsid = 0;
			ibReader* clsidChunk = childBlock->open_chunk_iterator(clsid, prevClsid);
			if (!clsidChunk)
				break;
			ibReader* prevMeta = nullptr;
			for (;;) {
				u64 metaId = 0;
				ibReader* metaChunk = clsidChunk->open_chunk_iterator(metaId, prevMeta);
				if (!metaChunk)
					break;
				ibDataNode& child = node.AddChild((ibClassID)clsid, (ibMetaID)metaId);
				ReadNode(*metaChunk, child);
				prevMeta = metaChunk;
			}
			prevClsid = clsidChunk;
		}
		childBlock->close();
	}

	if (ibReader* metaBlock = reader.open_chunk(kMetaBlock)) {
		const u32 version = metaBlock->r_u32();   // the node's format stamp - a Date entry reads by it
		ReadFields(*metaBlock, node, version);
		ReadProps(*metaBlock, node, version);
		const u32 rawLen = metaBlock->r_u32();
		if (rawLen) {
			wxMemoryBuffer raw;
			metaBlock->r(raw.GetAppendBuf(rawLen), rawLen);
			raw.UngetAppendBuf(rawLen);
			node.SetRawData(raw);
		}
		metaBlock->close();
	}
}

bool ibBinaryProvider::Write(const ibDataNode& root, ibWriter& writer) const {
	// Root CONTENT only — symmetric with Read. A container that needs an identity frame
	// (config: chunk(clsid){chunk(metaId){...}}) wraps this; a standalone node (form
	// control) needs no frame. Children are framed internally (WriteInner -> WriteNode).
	WriteInner(root, writer);
	return true;
}

bool ibBinaryProvider::Read(ibReader& reader, ibDataNode& root) const {
	// `reader` is the root's INNER content (what Write produced). A framed container
	// (config) peeled clsid/metaId before calling; a standalone node passes it straight.
	ReadNode(reader, root);
	return true;
}
