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
//	⭐⭐ OES-RPC 1.0 — A NODE IS ITS BYTES (Max, 2026-10-26: "ibDataNode is in effect what OES-RPC 1.0 is"; "the
//	storage format is like JSON, with no parsing needed"). A node is held as binary JSON in ONE buffer —
//	written as it is filled, read where it lies: every container says how long it is (the jump past it) and how many
//	it holds, so a reader goes by those marks and never looks for anything. The same buffer is the node in memory, in
//	storage and on the wire: handing a node over is one more owner of its buffer.
//
//	   packet = "OES1" · version u16 · flags u16 · length u32 · 0 u32         — the root node at 16
//	   node   = length u32 · fields' length u32 · fields u32 · children u32 · raw length u32 · metaId i32 · clsid u64
//	            · raw (even) · fields · children
//	   field  = kind u8 · 0 u8 · name (length u16, UTF-16LE) · value        — an even size
//	   value  = Empty — · Bool u8 + 0 · Number (length u32, ibNumber's bytes, even) · a whole Number i64 · Date i64
//	            · String (length u32, UTF-16LE) · Binary (length u32, bytes, even) · Child — a node
//	            · Array (length u32 · count u32 · items — fields with no name)
//
//	Nothing in it is a place, only lengths: a node's bytes mean the same wherever they are copied (at an even offset).
////////////////////////////////////////////////////////////////////////////

#include <deque>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "core/core.h"                   // ibMetaID, ibClassID
#include "core/fdatetime.h"              // ibDateTime — the Date scalar
#include "core/fstring.h"                // ibString — the String scalar and every entry's name
#include "core/guid.h"                   // ibGuid — codec specialization
#include "core/memory/memoryStream.h"          // ibWriterMemory / ibReaderMemory, std::uint32_t/std::uint64_t, ibMemoryBuffer
#include "core/fnumber.h"                // ibNumber — codec specialization

class CORE_API ibDataNode;   // a value can BE a child node (composite) — see ibDataKind::Child

////////////////////////////////////////////////////////////////////////////
// ibDataRange — WHAT A NODE HOLDS, WALKED WHERE IT LIES: a node's entries, its children, a list's items, each one
// made as it is reached, the next one a jump past it. It holds the bytes it walks (one more owner of them), so a range
// of a value read out and dropped at once — `node.GetProperty("x").AsArray()` — is still good.
////////////////////////////////////////////////////////////////////////////
template<class Item>
class ibDataRange {
public:
	using ibMake = Item(*)(const ibMemoryBuffer& bytes, std::uint32_t at);
	using ibNext = std::uint32_t(*)(const unsigned char* base, std::uint32_t at);

	ibDataRange() noexcept = default;
	ibDataRange(const ibMemoryBuffer& bytes, std::uint32_t first, std::size_t count, ibMake make, ibNext next) noexcept
		: m_bytes(bytes), m_first(first), m_count(count), m_make(make), m_next(next) {}

	std::size_t size() const noexcept { return m_count; }
	bool        empty() const noexcept { return m_count == 0; }

	class iterator {
	public:
		using iterator_category = std::input_iterator_tag;
		using value_type = Item;
		using difference_type = std::ptrdiff_t;
		using pointer = void;
		using reference = Item;

		iterator(const ibDataRange* range, std::uint32_t at, std::size_t left) noexcept : m_range(range), m_at(at), m_left(left) {}
		Item      operator*() const { return m_range->m_make(m_range->m_bytes, m_at); }
		iterator& operator++() noexcept { m_at = m_range->m_next(Base(), m_at); --m_left; return *this; }
		iterator  operator++(int) noexcept { iterator was = *this; ++*this; return was; }
		bool      operator==(const iterator& other) const noexcept { return m_left == other.m_left; }
		bool      operator!=(const iterator& other) const noexcept { return m_left != other.m_left; }
	private:
		const unsigned char* Base() const noexcept { return static_cast<const unsigned char*>(m_range->m_bytes.GetData()); }
		const ibDataRange* m_range;
		std::uint32_t      m_at;
		std::size_t        m_left;
	};
	iterator begin() const noexcept { return iterator(this, m_first, m_count); }
	iterator end() const noexcept { return iterator(this, 0, 0); }

	Item front() const { return *begin(); }
	Item back() const { return (*this)[m_count - 1]; }
	// The item at its place — the ones before it jumped past.
	Item operator[](std::size_t index) const {
		iterator at = begin();
		while (index-- != 0)
			++at;
		return *at;
	}

private:
	ibMemoryBuffer m_bytes;
	std::uint32_t  m_first = 0;
	std::size_t    m_count = 0;
	ibMake         m_make = nullptr;
	ibNext         m_next = nullptr;
};

////////////////////////////////////////////////////////////////////////////
// ibDataValue — a small tagged scalar for a node's named fields. A composite type (a TypeDescription) is NOT a
// scalar — it expands into a child ibDataNode walked by the same recursion.
//
// Each value carries a TYPE SIGNATURE (Kind), so it is self-describing: the bytes keep the natural binary form per
// kind (number as an integer, bool as a byte, string as text, blob as bytes). The signature preserves the type
// end-to-end, so a JSON view can render `42` / `true`, not `"42"`. C++<->value conversion is in ibDataCodec.
//
// ⭐ A VALUE IS A FIELD WHERE IT LIES — the bytes it is in (held: one more owner of them) and its place there: one read
// out of a node is the node's own bytes, no copy; one made here (String, Number, …) is a field of its own, put into a
// node as it is (AddField copies its bytes in).
////////////////////////////////////////////////////////////////////////////
enum class ibDataKind : std::uint8_t {
	Empty = 0,
	Bool,
	Number,    // exact decimal
	Date,      // a date/time scalar (an ibDateTime, fdatetime.h) — distinct from Number so it stays
	           // self-describing: a JSON view renders it as a readable ISO string, not a raw int
	String,
	Binary,    // opaque block (compiled form / module)
	Child,     // a COMPOSITE value — a nested ibDataNode a composite fills via its own read/write
	Array,     // an ORDERED LIST of values (e.g. a type's clsid list) — iterable, not a comma-string
};

class CORE_API ibDataValue {
public:
	ibDataValue() = default;
	ibDataValue(const ibDataValue&) = default;
	ibDataValue(ibDataValue&&) noexcept = default;
	ibDataValue& operator=(const ibDataValue&) = default;
	ibDataValue& operator=(ibDataValue&&) noexcept = default;

	ibDataKind Kind() const;
	bool IsEmpty() const { return Kind() == ibDataKind::Empty; }

	static ibDataValue String(const ibString& text);
	static ibDataValue Bool(bool value);
	static ibDataValue Number(const ibNumber& value); // exact decimal (the full numeric payload)
	static ibDataValue Int(std::int64_t value);    // small-int convenience over Number
	static ibDataValue UInt(std::uint64_t value);   // unsigned-int convenience over Number
	static ibDataValue Date(const ibDateTime& date);   // date/time scalar (the wall-clock reading, fdatetime.h)
	static ibDataValue Binary(const ibMemoryBuffer& data);
	static ibDataValue Child(const std::shared_ptr<ibDataNode>& child);   // its bytes copied in; null — Empty
	static ibDataValue Array(const std::vector<ibDataValue>& items);
	static ibDataValue Array(std::vector<ibDataValue>&& items);

	// Typed accessors — each checks the signature and THROWS (ibBackendException) on a wrong kind, so a mis-read fails
	// loud instead of returning garbage. An Empty value (an absent field) reads as the kind's default. By value: a
	// text is made of the characters where they lie; a sub-node and a list are handles into the same bytes.
	ibString                  AsString() const;
	bool                      AsBool()   const;
	ibNumber                  AsNumber() const;
	std::int64_t              AsInt()    const;
	std::uint64_t             AsUInt()   const;
	ibDateTime                AsDate()   const;
	ibMemoryBuffer            AsBinary() const;
	std::optional<ibDataNode> AsChild()  const;   // none — not a sub-node
	ibDataRange<ibDataValue>  AsArray()  const;

	// ⭐ TWO PACKED VALUES ARE THE SAME VALUE WHEN THEIR BYTES ARE (Max, 2026-08-29: "you can compare the node — that
	// is how you check the value for equality"). A value has one spelling of each kind (a whole number is always
	// written whole), so the bytes compared are the answer.
	bool operator==(const ibDataValue& other) const;
	bool operator!=(const ibDataValue& other) const { return !(*this == other); }

	// HOW MUCH IT SAYS — the bytes it takes.
	std::size_t Weight() const;

private:
	friend class ibDataNode;
	friend class ibBinaryProvider;

	ibDataValue(const ibMemoryBuffer& bytes, std::uint32_t at) : m_bytes(bytes), m_at(at) {}

	void Expect(ibDataKind expected) const; // throws ibBackendException on kind mismatch
	const unsigned char* Base() const { return static_cast<const unsigned char*>(m_bytes.GetData()); }
	std::uint32_t ValueAt() const;          // past its name

	ibMemoryBuffer m_bytes;   // none — Empty
	std::uint32_t  m_at = 0;
};

////////////////////////////////////////////////////////////////////////////
// ibDataCodec<T> — maps a C++ type to/from a signed ibDataValue. The node's
// templated SetValue<T>/GetValue<T> dispatch through it, so a new supported type
// is one new specialization (no new method). An unsupported T fails to compile
// (primary template undefined) — a guard.
////////////////////////////////////////////////////////////////////////////
template<class T> struct ibDataCodec;

template<> struct ibDataCodec<ibString> {         // the engine's own string — the String scalar
	static ibDataValue To(const ibString& v) { return ibDataValue::String(v); }
	static ibString    From(const ibDataValue& v) { return v.AsString(); }
};
template<> struct ibDataCodec<bool> {
	static ibDataValue To(bool v) { return ibDataValue::Bool(v); }
	static bool        From(const ibDataValue& v) { return v.AsBool(); }
};
template<> struct ibDataCodec<std::int32_t> {
	static ibDataValue To(std::int32_t v) { return ibDataValue::Int(v); }
	static std::int32_t         From(const ibDataValue& v) { return (std::int32_t)v.AsInt(); }
};
template<> struct ibDataCodec<ibMemoryBuffer> {
	static ibDataValue To(const ibMemoryBuffer& v) { return ibDataValue::Binary(v); }
	static ibMemoryBuffer From(const ibDataValue& v) { return v.AsBinary(); }
};
template<> struct ibDataCodec<ibGuid> {           // a guid serializes as its string form
	static ibDataValue To(const ibGuid& v) { return ibDataValue::String(v.str()); }
	static ibGuid      From(const ibDataValue& v) { return ibGuid(v.AsString()); }
};
template<> struct ibDataCodec<ibNumber> {         // exact decimal — the Number payload
	static ibDataValue To(const ibNumber& v) { return ibDataValue::Number(v); }
	static ibNumber    From(const ibDataValue& v) { return v.AsNumber(); }
};
template<> struct ibDataCodec<ibDateTime> {       // the engine's own date — the Date scalar
	static ibDataValue To(const ibDateTime& v) { return ibDataValue::Date(v); }
	static ibDateTime  From(const ibDataValue& v) { return v.AsDate(); }
};
template<> struct ibDataCodec<ibDataValue> {      // a value whose kind it says itself — a picture: an id or its PNG
	static ibDataValue To(const ibDataValue& v) { return v; }
	static ibDataValue From(const ibDataValue& v) { return v; }
};

////////////////////////////////////////////////////////////////////////////
// ibDataKey — AN ENTRY'S NAME AS IT IS ASKED FOR — a protocol's word, a literal, either string — with no text made of
// it: what a lookup compares a field's name with, where it lies.
////////////////////////////////////////////////////////////////////////////
struct CORE_API ibDataKey {
	ibDataKey(const char* ascii) noexcept;
	ibDataKey(const ibChar* text) noexcept;
	ibDataKey(const ibString& text) noexcept : m_utf16(text.c_str()), m_length(text.length()) {}

	const char*    m_ascii = nullptr;   // one of the two
	const ibChar*  m_utf16 = nullptr;
	std::size_t    m_length = 0;
};

////////////////////////////////////////////////////////////////////////////
// ibDataNode — one uniform, self-similar node of the structure tree. Every
// node, at any depth, has the same shape: a type, an identity, an ordered set
// of named fields, and child nodes (recursion). The walk does not branch on
// "is this a Catalog or an Attribute" — it recursively visits uniform nodes
// and the node's type says how a consumer expands it.
//
// READ is BY NAME, by the marks: a field's name and its length, the next one a jump past it — nothing parsed.
// Forward-compatible (add / drop / reorder a field without breaking older blobs). Fields and properties are ONE set,
// as the JSON view has always had them; the two pairs of verbs are the names the two walks write and read by.
//
// ⭐ WRITTEN WHERE IT LIES, IN ANY ORDER. A field goes into its node's place in the buffer; what follows it moves on
// (nothing when the node is the last one written — the common case: a builder fills a node and goes on). A sub-node
// being filled (Child, AddChild) is a handle into its root's buffer, good while the root is; a node read out
// (FindChild, Children, AsChild) is the bytes held and its place — a change to it makes it a node of its own.
////////////////////////////////////////////////////////////////////////////
class CORE_API ibDataNode {
public:
	ibDataNode() = default;
	ibDataNode(ibClassID clsid, ibMetaID metaId);
	// A copy is the same bytes, one more owner of them; a change to either makes its own.
	ibDataNode(const ibDataNode& other);
	ibDataNode(ibDataNode&& other) noexcept;
	ibDataNode& operator=(const ibDataNode& other);
	ibDataNode& operator=(ibDataNode&& other) noexcept;
	~ibDataNode();

	// --- identity ---------------------------------------------------------
	ibClassID GetClsid()  const;
	ibMetaID  GetMetaId() const;
	void      SetClsid(ibClassID clsid);
	void      SetMetaId(ibMetaID metaId);

	// --- typed values, templated (explicit, readable) --------------------
	// Save uses the non-const Set* (writes the node); Load uses the const Get*
	// (reads it). No direction flag — the METHOD (SaveNode / LoadNode) decides,
	// so on load the node is genuinely const. Get*<T> RETURNS the value (inline);
	// Get*(name, out) deduces T. Fields and properties are one set, so one pair:
	//   save: node.SetValue("id", m_metaId);          node.SetValue("Name", GetName());
	//   load: m_metaId = node.GetValue<std::int32_t>("id");     SetName(node.GetValue<ibString>("Name"));
	// An absent name yields a default-constructed T — forward-compatible with
	// add / drop / reorder. (No SetProp/GetProp: <windows.h> renames SetProp to
	// SetPropW wherever it is seen first, and the call stops matching the declaration.)
	template<class T> void SetValue(const ibString& name, const T& value) { AddField(name, ibDataCodec<T>::To(value)); }
	template<class T> T    GetValue(const ibDataKey& name) const {
		if (const auto v = FindField(name)) return ibDataCodec<T>::From(*v);
		return T();
	}
	template<class T> void GetValue(const ibDataKey& name, T& out) const {
		if (const auto v = FindField(name)) out = ibDataCodec<T>::From(*v);
	}

	// --- named-field primitives (internals of Field) ----------------------
	void AddField(const ibString& name, const ibDataValue& value);

	// ⭐ REPLACE-OR-ADD, for a node that is a VIEW rather than a record. AddField appends, which is
	// right for a blob written once in order — reading it back finds the first and nothing repeats a
	// name. A node rendered as JSON is a different thing: a repeated key is invalid there, and a
	// lenient reader keeps whichever it saw last.
	//
	// 🛑 IT SHOWED. An MCP tool's schema is built argument BY argument, each declaring `type:
	// "object"` and each appending to `required` — so a seven-argument tool published seven `type`
	// keys and two `required` arrays, and a strict client would have taken the wrong one of them
	// (found 2026-09-01, reading a schema back off the running server).
	void SetField(const ibString& name, const ibDataValue& value);
	// …and the field taken out — a word a reader kept for later, once it is answered (ibJsonProvider::LookupTypes).
	void RemoveField(const ibDataKey& name);
	// The field under the name — none when the name is absent: a new field against an old blob defaults, never errors.
	// The name as it is handed, compared where the field's lies: a lookup makes no text.
	std::optional<ibDataValue> FindField(const ibDataKey& name) const;
	// …and whether there is one — a name given or not (the question ibProtocolNode::Has asks one storey up).
	bool Has(const ibDataKey& name) const;
	// The fields in the order they were written — each a name and its value.
	ibDataRange<std::pair<ibString, ibDataValue>> Fields() const;

	// --- the property walk's names for the same set ------------------------
	// Filled by the generic property walk (ibPropertyObject::DescribeProperties).
	void SetProperty(const ibString& name, const ibDataValue& value) { AddField(name, value); }
	std::optional<ibDataValue> FindProperty(const ibDataKey& name) const { return FindField(name); }
	// value by name (empty if absent) — pass straight into a property's ReadNodeValue
	ibDataValue GetProperty(const ibDataKey& name) const;

	// …and the same question one storey up: two nodes are equal when their bytes are — the type, the identity, the
	// fields in their order, the raw block and the children.
	bool operator==(const ibDataNode& other) const;
	bool operator!=(const ibDataNode& other) const { return !(*this == other); }

	// …and its weight: the bytes it takes.
	std::size_t Weight() const;

	// --- transitional opaque data ----------------------------------------
	// The not-yet-decomposed remainder of this node (today: the eDataBlock
	// blob = SaveMeta output). Empty once a node is fully Described into
	// named fields. Kept separate so byte-identity holds during migration.
	ibMemoryBuffer RawData() const;
	void           SetRawData(const ibMemoryBuffer& data);
	bool           HasRawData() const;

	// --- composite value as a named CHILD sub-node (Type / interface / form …) -
	// A composite fills its OWN values into this sub-node — that is how Type,
	// interface/roles and (later) a whole form decompose into the tree instead of
	// an opaque blob. Binary is reserved for genuine blobs (pictures, files).
	//   save: ibDataNode& sub = node.Child("Type");      // the sub-node under the name — made when there is none —
	//                                                     // filled where it lies
	//   load: if (const auto sub = node.FindChild("Type")) ...   // reads it
	ibDataNode&               Child(const ibString& name);
	std::optional<ibDataNode> FindChild(const ibDataKey& name) const;

	// --- metaobject children (recursion over the object tree) ------------
	ibDataNode& AddChild(ibClassID clsid, ibMetaID metaId);
	// …and one made already, its bytes copied in (a reader's, ParseNode's).
	ibDataNode& AddChild(const ibDataNode& child);
	ibDataRange<ibDataNode> Children() const;
	// …and all of them taken out.
	void RemoveChildren();

	// --- a list under a name (Array), added to at its end ------------------
	// A node at the end of the list — the list made when there is none — filled where it lies, as a Child is.
	ibDataNode& AddItem(const ibString& name);
	// …and a value at its end, its bytes copied in.
	void        AddItem(const ibString& name, const ibDataValue& value);

	// --- a part it holds already, to fill where it lies ---------------------
	// A node read out of this one as it is now — a sub-node under a name (FindChild), a child (Children), a node in a
	// list — as a handle, as Child, AddChild and AddItem give one. This one a root or a handle; a handle to a part a
	// change took out is an empty node of its own.
	ibDataNode& Handle(const ibDataNode& part);

private:
	friend class ibDataValue;
	friend class ibBinaryProvider;

	// A node read out: the bytes held, and its place in them.
	ibDataNode(const ibMemoryBuffer& bytes, std::uint32_t at) : m_bytes(bytes), m_at(at) {}

	// THE BUFFER IT IS IN — its own, or its root's for a sub-node being filled.
	const ibMemoryBuffer& Bytes() const { return m_root != nullptr ? m_root->m_bytes : m_bytes; }
	const unsigned char*  Base() const { return static_cast<const unsigned char*>(Bytes().GetData()); }
	// …and to change it: a node read out becomes a root of its own first (its bytes copied out of the ones it was read
	// from); a node with none yet gets its packet. The root that writes — this one, or the one a handle is into.
	ibDataNode& Root();
	// `size` bytes at `at` in this node's part `inFields` (its fields, else its children), what follows moved on, and
	// the lengths of the node and of every node it is in grown: where they go, to be written.
	unsigned char* Insert(std::uint32_t at, std::uint32_t size, bool inFields);
	void           Erase(std::uint32_t at, std::uint32_t size, bool inFields);
	// …and the lengths: of this node, and up through the nodes it is in.
	void Grow(std::int64_t delta, bool inFields);
	// A handle to a sub-node being filled — kept by the root, so a reference to it stays good while the root does.
	ibDataNode& Handle(std::uint32_t at, bool inField);
	// The list under the name — made empty when there is none — and the place past its last item, to add at.
	std::uint32_t ListEnd(const ibString& name, std::uint32_t& list);

	ibMemoryBuffer m_bytes;                      // a root's, or the bytes a node read out is in
	std::uint32_t  m_at = 0;                     // its place there; 0 — no bytes yet (a new, empty node)
	ibDataNode*    m_root = nullptr;             // a handle's: the root whose bytes it is filled in
	ibDataNode*    m_parent = nullptr;           // …and the node it is in, whose length grows with it
	bool           m_inField = false;            // …in its fields (a Child) or its children
	std::uint32_t  m_list = 0;                   // …a list's item: the list's place, whose length grows with it
	std::unique_ptr<std::deque<ibDataNode>> m_handles;   // a root's: the handles it gave out
};

////////////////////////////////////////////////////////////////////////////
// ibFormatProvider — the pluggable, BIDIRECTIONAL format adapter. Write
// renders a node tree into a format; Read parses a format back into a node
// tree. A new format (json, zip, …) is a NEW provider — zero changes to
// ibDataNode or to any consumer. The format lives only here.
////////////////////////////////////////////////////////////////////////////
class CORE_API ibFormatProvider {
public:
	virtual ~ibFormatProvider() = default;

	virtual bool Write(const ibDataNode& root, ibWriterMemory& writer) const = 0;
	virtual bool Read(ibReaderMemory& reader, ibDataNode& root) const = 0;
};

////////////////////////////////////////////////////////////////////////////
// ibBinaryProvider — THE NODE'S OWN BYTES — storage and the wire alike (OES-RPC 1.0, the header). Nothing is
// converted either way: Write hands the node's buffer over (one more owner of it; a sub-node's bytes copied out into
// a packet of their own), Read takes bytes as the node they are — checked first, never parsed. Old blobs are NOT
// readable — no compatibility (Max, 2026-10-26): recreate the config.
////////////////////////////////////////////////////////////////////////////
class CORE_API ibBinaryProvider : public ibFormatProvider {
public:
	// A packet's first bytes — what tells one on a wire from anything else there: a message deflated (ibDeflate — raw
	// deflate's first byte is never 'O'), a debugger's command.
	static constexpr char kMagic[4] = { 'O', 'E', 'S', '1' };
	// …and whether bytes begin with them.
	static bool IsPacket(const void* bytes, std::size_t size);
	// Nesting past this is refused — the JSON view's ceiling (ibJsonProvider::kMaxNesting).
	static constexpr int kMaxNesting = 256;

	bool Write(const ibDataNode& root, ibWriterMemory& writer) const override;
	bool Read(ibReaderMemory& reader, ibDataNode& root) const override;

	// …and as the buffer itself. Read refuses — it throws, with why — bytes that are not a packet: a wrong header, a
	// length past its container, a count its lengths do not hold, a kind unknown, nesting past kMaxNesting.
	bool Write(const ibDataNode& root, ibMemoryBuffer& packet) const;
	bool Read(const ibMemoryBuffer& packet, ibDataNode& root) const;
};

////////////////////////////////////////////////////////////////////////////
// ibDataBuilder — the top-level holder owned by ibMetaData (and, later, by an
// external Report / DataProcessor / a form). It owns the root ibDataNode and
// drives save/load through a chosen provider. The consumer fills the tree
// (Describe-walk) on save, and drains it on load; the builder hands the tree
// to / from the provider. Nodes never see the format; the format never sees
// the consumer.
////////////////////////////////////////////////////////////////////////////
class CORE_API ibDataBuilder {
public:
	ibDataNode&       Root()       { return m_root; }
	const ibDataNode& Root() const { return m_root; }
	void Reset() { m_root = ibDataNode(); }

	bool Save(const ibFormatProvider& provider, ibWriterMemory& writer) const {
		return provider.Write(m_root, writer);
	}
	bool Load(const ibFormatProvider& provider, ibReaderMemory& reader) {
		Reset();
		return provider.Read(reader, m_root);
	}

private:
	ibDataNode m_root;
};

#endif // !__SERIALIZE_DATA_BUILDER_H__
