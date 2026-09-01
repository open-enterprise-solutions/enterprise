#ifndef __SERIALIZE_DATA_BUILDER_H__
#define __SERIALIZE_DATA_BUILDER_H__

////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibDataNode — the platform's universal, format-agnostic
//	              in-memory serialization structure.
//
//	Metadata, forms, and the spreadsheet document all Describe themselves
//	into an ibDataNode tree; a pluggable ibFormatProvider then serializes
//	that tree to/from a concrete format (binary now, json later). A node
//	never knows the on-disk format; a provider never knows the consumer.
//
//	This mirrors the L3 schema split one level up: the Describe-walk is the
//	ContributeTables analogue, the ibDataNode tree is the ibSchemaSnapshot
//	analogue, and the providers are the consumers (ibStructureBuilder /
//	ibDataMover analogues). See docs/schema-first-metadata.md.
////////////////////////////////////////////////////////////////////////////

#include <vector>
#include <utility>
#include <memory>
#include <wx/datetime.h>                    // wxDateTime — codec specialization (wxBase, not GUI)

#include "backend/backend_core.h"          // ibMetaID
#include "backend/clsid.h"                  // ibClassID
#include "backend/fileSystem/fs.h"          // ibWriter / ibReader, u32/u64, wxMemoryBuffer
#include "backend/fnumber.h"                // ibNumber — codec specialization

class BACKEND_API ibDataNode;   // a value can BE a child node (composite) — see ibDataKind::Child

////////////////////////////////////////////////////////////////////////////
// ibDataValue — a small tagged scalar for a node's named fields. This is the
// VALUE LAYER the providers materialize per format: binary writes positional
// bytes, json writes a typed key. A composite type (a TypeDescription) is NOT
// a scalar — it expands into a child ibDataNode walked by the same recursion.
//
// Empty / opaque-binary is the transitional kind used while a node's data is
// still carried as one un-decomposed block (the eDataBlock blob of the legacy
// format). As each metaobject's Describe is filled out, those blobs are
// replaced by real named scalars + child sub-nodes.
////////////////////////////////////////////////////////////////////////////
// Each value carries a TYPE SIGNATURE (m_kind), so it is self-describing: the
// internal store keeps the natural binary form per kind (number as an integer,
// bool as a byte, string as text, blob as bytes) and the provider writes the
// signature + the value. The signature preserves the type end-to-end, so a JSON
// view can render `42` / `true`, not `"42"`. C++<->value conversion is in ibDataCodec.
enum class ibDataKind : u8 {
	Empty = 0,
	Bool,
	Number,    // exact decimal later; integer scalars for now
	Date,      // a date/time scalar (ms ticks since epoch) — distinct from Number so it stays
	           // self-describing: a JSON view renders it as a readable ISO string, not a raw int
	String,
	Binary,    // opaque block (compiled form / module)
	Child,     // a COMPOSITE value — a nested ibDataNode a composite fills via its own read/write
	Array,     // an ORDERED LIST of values (e.g. a type's clsid list) — iterable, not a comma-string
};

class BACKEND_API ibDataValue {
public:
	ibDataValue() = default;

	ibDataKind Kind() const { return m_kind; }
	bool IsEmpty() const { return m_kind == ibDataKind::Empty; }

	static ibDataValue String(const wxString& text);
	static ibDataValue Bool(bool value);
	static ibDataValue Number(const ibNumber& value); // exact decimal (the full numeric payload)
	static ibDataValue Int(s64 value);    // small-int convenience over Number
	static ibDataValue UInt(u64 value);   // unsigned-int convenience over Number
	static ibDataValue Date(s64 ticks);   // date/time scalar (ms ticks since epoch)
	static ibDataValue Binary(const wxMemoryBuffer& data);
	static ibDataValue Child(const std::shared_ptr<ibDataNode>& child);
	static ibDataValue Array(const std::vector<ibDataValue>& items);

	// Typed accessors — each checks the signature and THROWS (ibBackendException) on
	// a wrong kind, so a mis-read fails loud instead of returning garbage. Callers
	// that may see another kind must gate on Kind() first (as ReadNodeValue does).
	wxString                         AsString() const { Expect(ibDataKind::String); return m_text; }
	bool                             AsBool()   const { Expect(ibDataKind::Bool);   return m_bool; }
	const ibNumber&                  AsNumber() const { Expect(ibDataKind::Number); return m_number; }
	s64                              AsInt()    const { Expect(ibDataKind::Number); s64 out = 0; m_number.ToInt(out); return out; }
	u64                              AsUInt()   const { Expect(ibDataKind::Number); u64 out = 0; m_number.ToInt(out); return out; }
	s64                              AsDate()   const { Expect(ibDataKind::Date);   return m_date; }
	const wxMemoryBuffer&            AsBinary() const { Expect(ibDataKind::Binary); return m_binary; }
	const std::shared_ptr<ibDataNode>& AsChild() const { Expect(ibDataKind::Child); return m_child; }
	const std::vector<ibDataValue>&  AsArray()  const { Expect(ibDataKind::Array);  return m_array; }

	// ⭐ TWO PACKED VALUES ARE THE SAME VALUE WHEN THEIR NODES SAY THE SAME THING. A stored value is
	// compared HERE rather than by building both back into runtime objects and asking them: what a
	// store holds is bytes, and bytes are what "did this change" is about (Max, 2026-08-29: "you can
	// compare the node — that is how you check the value for equality").
	bool operator==(const ibDataValue& other) const;
	bool operator!=(const ibDataValue& other) const { return !(*this == other); }

private:
	void Expect(ibDataKind expected) const; // throws ibBackendException on kind mismatch

	ibDataKind     m_kind = ibDataKind::Empty;  // the signature
	wxString       m_text;     // String payload
	bool           m_bool = false; // Bool payload
	ibNumber       m_number;   // Number payload (exact decimal, huge-capable)
	s64            m_date = 0; // Date payload (ms ticks since epoch) — never mixed with Number
	wxMemoryBuffer m_binary;   // Binary payload
	std::shared_ptr<ibDataNode> m_child; // Child payload (composite sub-node)
	std::vector<ibDataValue>    m_array; // Array payload (ordered list of values)
};

////////////////////////////////////////////////////////////////////////////
// ibDataCodec<T> — maps a C++ type to/from a signed ibDataValue. The node's
// templated SetValue<T>/GetValue<T> dispatch through it, so a new supported type
// is one new specialization (no new method). An unsupported T fails to compile
// (primary template undefined) — a guard.
////////////////////////////////////////////////////////////////////////////
template<class T> struct ibDataCodec;

template<> struct ibDataCodec<wxString> {
	static ibDataValue To(const wxString& v) { return ibDataValue::String(v); }
	static wxString    From(const ibDataValue& v) { return v.AsString(); }
};
template<> struct ibDataCodec<bool> {
	static ibDataValue To(bool v) { return ibDataValue::Bool(v); }
	static bool        From(const ibDataValue& v) { return v.AsBool(); }
};
template<> struct ibDataCodec<s32> {
	static ibDataValue To(s32 v) { return ibDataValue::Int(v); }
	static s32         From(const ibDataValue& v) { return (s32)v.AsInt(); }
};
template<> struct ibDataCodec<wxMemoryBuffer> {
	static ibDataValue To(const wxMemoryBuffer& v) { return ibDataValue::Binary(v); }
	static wxMemoryBuffer From(const ibDataValue& v) { return v.AsBinary(); }
};
template<> struct ibDataCodec<ibGuid> {           // a guid serializes as its string form
	static ibDataValue To(const ibGuid& v) { return ibDataValue::String(v.str()); }
	static ibGuid      From(const ibDataValue& v) { return ibGuid(v.AsString()); }
};
template<> struct ibDataCodec<ibNumber> {         // exact decimal — the Number payload
	static ibDataValue To(const ibNumber& v) { return ibDataValue::Number(v); }
	static ibNumber    From(const ibDataValue& v) { return v.AsNumber(); }
};
template<> struct ibDataCodec<wxDateTime> {       // a date/time — the Date scalar (ms ticks)
	static ibDataValue To(const wxDateTime& v) { return ibDataValue::Date(v.IsValid() ? v.GetValue().GetValue() : 0); }
	static wxDateTime  From(const ibDataValue& v) { const s64 ms = v.AsDate(); return ms != 0 ? wxDateTime(wxLongLong(ms)) : wxDateTime(); }
};

////////////////////////////////////////////////////////////////////////////
// ibDataNode — one uniform, self-similar node of the structure tree. Every
// node, at any depth, has the same shape: a type, an identity, an ordered set
// of named fields, and child nodes (recursion). The walk does not branch on
// "is this a Catalog or an Attribute" — it recursively visits uniform nodes
// and the node's type says how a consumer expands it.
//
// READ is BY NAME with an optimistic cursor: FindField tries the expected
// position first (matches the write order — the common case), and only falls
// back to a search when the order shifted. This is what gives forward-compat
// (add / drop / reorder a field without breaking older blobs).
////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibDataNode {
public:
	ibDataNode() = default;
	ibDataNode(ibClassID clsid, ibMetaID metaId) : m_clsid(clsid), m_metaId(metaId) {}

	// --- identity ---------------------------------------------------------
	ibClassID GetClsid()  const { return m_clsid; }
	ibMetaID  GetMetaId() const { return m_metaId; }
	void      SetClsid(ibClassID clsid) { m_clsid = clsid; }
	void      SetMetaId(ibMetaID metaId) { m_metaId = metaId; }

	// --- typed values, templated (explicit, readable) --------------------
	// Save uses the non-const Set* (writes the node); Load uses the const Get*
	// (reads it). No direction flag — the METHOD (SaveNode / LoadNode) decides,
	// so on load the node is genuinely const. Get*<T> RETURNS the value (inline);
	// Get*(name, out) deduces T. One pair for the FIELD area, one for PROPERTIES:
	//   save: node.SetValue("id", m_metaId);          node.SetProp("Name", GetName());
	//   load: m_metaId = node.GetValue<s32>("id");     SetName(node.GetProp<wxString>("Name"));
	// An absent name yields a default-constructed T — forward-compatible with
	// add / drop / reorder.
	template<class T> void SetValue(const wxString& name, const T& value) { AddField(name, ibDataCodec<T>::To(value)); }
	// return form: explicit <T>, value inline; out form: T deduced, `out` left at
	// its default when the name is absent.
	template<class T> T    GetValue(const wxString& name) const {
		if (const ibDataValue* v = FindField(name)) return ibDataCodec<T>::From(*v);
		return T();
	}
	template<class T> void GetValue(const wxString& name, T& out) const {
		if (const ibDataValue* v = FindField(name)) out = ibDataCodec<T>::From(*v);
	}
	template<class T> void SetProp(const wxString& name, const T& value) { SetProperty(name, ibDataCodec<T>::To(value)); }
	template<class T> T    GetProp(const wxString& name) const {
		if (const ibDataValue* v = FindProperty(name)) return ibDataCodec<T>::From(*v);
		return T();
	}
	template<class T> void GetProp(const wxString& name, T& out) const {
		if (const ibDataValue* v = FindProperty(name)) out = ibDataCodec<T>::From(*v);
	}

	// --- named-field primitives (internals of Field) ----------------------
	void AddField(const wxString& name, const ibDataValue& value) {
		m_fields.emplace_back(name, value);
	}

	// ⭐ REPLACE-OR-ADD, for a node that is a VIEW rather than a record. AddField appends, which is
	// right for a blob written once in order — reading it back finds the first and nothing repeats a
	// name. A node rendered as JSON is a different thing: a repeated key is invalid there, and a
	// lenient reader keeps whichever it saw last.
	//
	// 🛑 IT SHOWED. An MCP tool's schema is built argument BY argument, each declaring `type:
	// "object"` and each appending to `required` — so a seven-argument tool published seven `type`
	// keys and two `required` arrays, and a strict client would have taken the wrong one of them
	// (found 2026-09-01, reading a schema back off the running server).
	void SetField(const wxString& name, const ibDataValue& value) {

		for (std::pair<wxString, ibDataValue>& field : m_fields) {
			if (field.first == name) {
				field.second = value;
				return;
			}
		}

		m_fields.emplace_back(name, value);
	}
	// Optimistic-cursor lookup: try m_cursor first (write-order fast path),
	// else search from it. Returns nullptr (cursor unmoved) when the name is
	// absent — a new field against an old blob defaults, never errors.
	const ibDataValue* FindField(const wxString& name) const;
	const std::vector<std::pair<wxString, ibDataValue>>& Fields() const { return m_fields; }

	// --- properties area (the property bag — DISTINCT from plain fields) ---
	// Filled by the generic property walk (ibPropertyObject::DescribeProperties),
	// kept separate from m_fields so the structure SHOWS what is a property vs a
	// plain (hidden / intrinsic) class field. Stored faithfully (the property's
	// own SaveData bytes); per-property value-decomposition is a later concern.
	void SetProperty(const wxString& name, const ibDataValue& value) { m_props.emplace_back(name, value); }
	const ibDataValue* FindProperty(const wxString& name) const; // optimistic cursor over m_props
	// value by name (empty if absent) — pass straight into a property's ReadNodeValue
	ibDataValue GetProperty(const wxString& name) const {
		if (const ibDataValue* v = FindProperty(name)) return *v;
		return ibDataValue();
	}
	const std::vector<std::pair<wxString, ibDataValue>>& Properties() const { return m_props; }

	// …and the same question one storey up: two nodes are equal when their type, identity, fields,
	// properties, raw block and children all are. The read cursors are not state — they are where the
	// last optimistic lookup stopped — so they take no part in it.
	bool operator==(const ibDataNode& other) const;
	bool operator!=(const ibDataNode& other) const { return !(*this == other); }

	// --- transitional opaque data ----------------------------------------
	// The not-yet-decomposed remainder of this node (today: the eDataBlock
	// blob = SaveMeta output). Empty once a node is fully Described into
	// named fields. Kept separate so byte-identity holds during migration.
	const wxMemoryBuffer& RawData() const { return m_rawData; }
	void  SetRawData(const wxMemoryBuffer& data) { m_rawData = data; }
	bool  HasRawData() const { return m_rawData.GetDataLen() != 0; }

	// --- composite value as a named CHILD sub-node (Type / interface / form …) -
	// A composite fills its OWN values into this sub-node — that is how Type,
	// interface/roles and (later) a whole form decompose into the tree instead of
	// an opaque blob. Binary is reserved for genuine blobs (pictures, files).
	//   save: ibDataNode& sub = node.Child("Type");      // creates the sub-node
	//   load: if (const ibDataNode* sub = node.FindChild("Type")) ...   // reads it
	// The Child value lives in the PROPERTIES area.
	ibDataNode&       Child(const wxString& name);              // create (save)
	const ibDataNode* FindChild(const wxString& name) const;   // read (load)

	// ⚠ AND THE ONE THAT KEEPS ADDING TO A SUB-NODE THAT MAY ALREADY BE THERE. `Child` is the SAVE
	// half and always makes a FRESH node, which is right when a writer fills a sub-node once and
	// wrong the moment two writers contribute to the same one — the second silently threw away the
	// first (the MCP schema published one argument per tool that way, 2026-09-01).
	//
	// Written as an overload rather than left to the caller, because the alternative is a
	// const_cast at every such site: a non-const node handing back a const pointer to its own child
	// is the accident, not the rule.
	ibDataNode* FindChild(const wxString& name);

	// --- metaobject children (recursion over the object tree) ------------
	ibDataNode& AddChild(ibClassID clsid, ibMetaID metaId) {
		m_children.emplace_back(clsid, metaId);
		return m_children.back();
	}
	const std::vector<ibDataNode>& Children() const { return m_children; }
	std::vector<ibDataNode>&       Children()       { return m_children; }

private:
	ibClassID m_clsid = 0;   // node type == chunk id in the binary layout
	ibMetaID  m_metaId = 0;  // node identity within its type

	std::vector<std::pair<wxString, ibDataValue>> m_fields;   // plain / hidden / intrinsic fields
	std::vector<std::pair<wxString, ibDataValue>> m_props;    // the property bag (separate area)
	wxMemoryBuffer                                m_rawData;  // transitional un-decomposed block
	std::vector<ibDataNode>                       m_children; // metaobject sub-nodes

	mutable size_t m_cursor = 0;          // optimistic read cursor over m_fields
	mutable size_t m_propCursor = 0;      // optimistic read cursor over m_props
};

////////////////////////////////////////////////////////////////////////////
// ibFormatProvider — the pluggable, BIDIRECTIONAL format adapter. Write
// renders a node tree into a format; Read parses a format back into a node
// tree. A new format (json, zip, …) is a NEW provider — zero changes to
// ibDataNode or to any consumer. The format lives only here.
////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibFormatProvider {
public:
	virtual ~ibFormatProvider() = default;

	virtual bool Write(const ibDataNode& root, ibWriter& writer) const = 0;
	virtual bool Read(ibReader& reader, ibDataNode& root) const = 0;
};

////////////////////////////////////////////////////////////////////////////
// ibBinaryProvider — INTERNAL format, OWNED by us (not the legacy layout).
// It serializes the builder's native structure directly: per node a
// version-stamped block of NAMED fields + a transitional raw remainder, plus
// the child sub-tree:
//
//    chunk(node.clsid) { chunk(node.metaId) {
//        chunk(kMetaBlock) { version, <named fields: name+kind+value>, rawLen+raw }
//        chunk(kChildBlock) { <each child, recursively> }
//    } }
//
// Named fields are read BY NAME (optimistic cursor) on the consumer side, so
// add / drop / reorder a field is forward-compatible. The raw remainder holds
// per-node data not yet decomposed into named fields (interface / roles /
// per-type Save) and shrinks as each type's Describe is filled out. Old legacy
// blobs are NOT readable by this format — version-gated, recreate the config.
////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibBinaryProvider : public ibFormatProvider {
public:
	bool Write(const ibDataNode& root, ibWriter& writer) const override;
	bool Read(ibReader& reader, ibDataNode& root) const override;

private:
	void WriteInner(const ibDataNode& node, ibWriter& writer) const; // node CONTENT only (no identity frame); ROOT path
	void WriteNode(const ibDataNode& node, ibWriter& writer) const;  // identity-framed node; CHILD path
	void ReadNode(ibReader& reader, ibDataNode& node) const; // reads the inner; node.clsid/metaId pre-set by caller
	void WriteFields(const ibDataNode& node, ibWriter& writer) const;
	void ReadFields(ibReader& reader, ibDataNode& node) const;
	void WriteProps(const ibDataNode& node, ibWriter& writer) const;   // the property bag, separate area
	void ReadProps(ibReader& reader, ibDataNode& node) const;
	void WriteChildren(const ibDataNode& node, ibWriter& writer) const; // a Child value's subtree (form control tree)
	void ReadChildren(ibReader& reader, ibDataNode& node) const;
	void WriteEntry(ibWriter& writer, const wxString& name, const ibDataValue& value) const; // name + value payload
	void WriteValue(ibWriter& writer, const ibDataValue& value) const;                        // kind + value (recurses on Child / Array)
	ibDataValue ReadEntry(ibReader& reader) const;                                            // kind + value (name read by the caller)
};

////////////////////////////////////////////////////////////////////////////
// ibDataBuilder — the top-level holder owned by ibMetaData (and, later, by an
// external Report / DataProcessor / a form). It owns the root ibDataNode and
// drives save/load through a chosen provider. The consumer fills the tree
// (Describe-walk) on save, and drains it on load; the builder hands the tree
// to / from the provider. Nodes never see the format; the format never sees
// the consumer.
////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibDataBuilder {
public:
	ibDataNode&       Root()       { return m_root; }
	const ibDataNode& Root() const { return m_root; }
	void Reset() { m_root = ibDataNode(); }

	bool Save(const ibFormatProvider& provider, ibWriter& writer) const {
		return provider.Write(m_root, writer);
	}
	bool Load(const ibFormatProvider& provider, ibReader& reader) {
		Reset();
		return provider.Read(reader, m_root);
	}

private:
	ibDataNode m_root;
};

#endif // !__SERIALIZE_DATA_BUILDER_H__
