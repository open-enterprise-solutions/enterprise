////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibDataNode — a node held as its bytes (OES-RPC 1.0), its values,
//	              and ibBinaryProvider, which hands the bytes over. See
//	              serialize/dataBuilder.h for the layout.
////////////////////////////////////////////////////////////////////////////

#include "core/serialize/dataBuilder.h"
#include "core/exception.h"              // ibCoreException — a kind mismatch, a packet refused
#include "core/fiber/fiber.h"            // StackLow — the check descends a native frame a node

#include "core/localization/dictionary.h"   // _() — the refusals' words

#include <climits>                       // LLONG_MAX — an unsigned number past it is no whole one
#include <cstring>                       // std::memcpy / memmove / memcmp — the bytes, read and moved
#include <string>                        // std::char_traits — a key's length

namespace {

static_assert(sizeof(ibChar) == 2, "a name's and a text's character is two bytes");

// The bytes are little-endian, as every platform the engine builds for is — said where it would break.
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#error "OES-RPC packets are little-endian; a big-endian build needs byte swaps in dataBuilder.cpp"
#endif

constexpr std::uint32_t kPacketHeader = 16;
constexpr std::uint32_t kNodeHeader = 32;
constexpr std::uint16_t kVersion = 1;

// A field's kind as its byte says it — ibDataKind's, and a Number written whole after them.
constexpr unsigned char kWhole = 8;

// The node's header, by its places (dataBuilder.h).
constexpr std::uint32_t kLength = 0, kFieldsLength = 4, kFieldCount = 8, kChildCount = 12, kRawLength = 16,
	kMetaId = 20, kClsid = 24;

template<class T>
T Rd(const unsigned char* base, std::uint32_t at) noexcept
{
	T value;
	std::memcpy(&value, base + at, sizeof(T));
	return value;
}

template<class T>
void Wr(unsigned char* base, std::uint32_t at, T value) noexcept
{
	std::memcpy(base + at, &value, sizeof(T));
}

inline std::uint32_t Even(std::uint32_t n) noexcept { return (n + 1) & ~1u; }

// --- a field: its name, its value, its size — the jump past it ------------------------------------------------------

inline std::uint32_t NameLength(const unsigned char* b, std::uint32_t field) noexcept { return Rd<std::uint16_t>(b, field + 2); }
inline std::uint32_t ValueOf(const unsigned char* b, std::uint32_t field) noexcept { return field + 4 + 2 * NameLength(b, field); }

std::uint32_t ValueSize(const unsigned char* b, unsigned char kind, std::uint32_t value) noexcept
{
	switch (kind) {
	case static_cast<unsigned char>(ibDataKind::Empty):  return 0;
	case static_cast<unsigned char>(ibDataKind::Bool):   return 2;
	case static_cast<unsigned char>(ibDataKind::Number): return 4 + Even(Rd<std::uint32_t>(b, value));
	case kWhole:
	case static_cast<unsigned char>(ibDataKind::Date):   return 8;
	case static_cast<unsigned char>(ibDataKind::String): return 4 + 2 * Rd<std::uint32_t>(b, value);
	case static_cast<unsigned char>(ibDataKind::Binary): return 4 + Even(Rd<std::uint32_t>(b, value));
	case static_cast<unsigned char>(ibDataKind::Child):  return Rd<std::uint32_t>(b, value + kLength);
	case static_cast<unsigned char>(ibDataKind::Array):  return Rd<std::uint32_t>(b, value);
	}
	return 0;
}

inline std::uint32_t NextField(const unsigned char* b, std::uint32_t field) noexcept
{
	const std::uint32_t value = ValueOf(b, field);
	return value + ValueSize(b, b[field], value);
}

// --- a node: its parts ----------------------------------------------------------------------------------------------

inline std::uint32_t NodeLength(const unsigned char* b, std::uint32_t node) noexcept { return Rd<std::uint32_t>(b, node + kLength); }
inline std::uint32_t NextNode(const unsigned char* b, std::uint32_t node) noexcept { return node + NodeLength(b, node); }
inline std::uint32_t FieldsOf(const unsigned char* b, std::uint32_t node) noexcept
{
	return node + kNodeHeader + Even(Rd<std::uint32_t>(b, node + kRawLength));
}
inline std::uint32_t ChildrenOf(const unsigned char* b, std::uint32_t node) noexcept
{
	return FieldsOf(b, node) + Rd<std::uint32_t>(b, node + kFieldsLength);
}

// --- characters: the engine's are the bytes' (UTF-16LE) -------------------------------------------------------------

// A text's characters into the bytes, as they are.
void ToUtf16(const ibString& text, unsigned char* to) noexcept
{
	if (!text.IsEmpty())
		std::memcpy(to, text.c_str(), text.length() * 2);
}

ibString FromUtf16(const unsigned char* at, std::uint32_t units)
{
	return ibString(reinterpret_cast<const ibChar*>(at), units);   // the characters as they lie, at an even place
}

// A field's name, the key it is asked by: the same characters, compared where they lie.
bool NameIs(const unsigned char* b, std::uint32_t field, const ibDataKey& key) noexcept
{
	const std::uint32_t units = NameLength(b, field);
	const unsigned char* const name = b + field + 4;
	if (key.m_ascii != nullptr) {
		if (units != key.m_length)
			return false;
		for (std::uint32_t i = 0; i < units; ++i) {
			if (Rd<std::uint16_t>(name, 2 * i) != static_cast<unsigned char>(key.m_ascii[i]))
				return false;
		}
		return true;
	}
	return units == key.m_length && (units == 0 || std::memcmp(name, key.m_utf16, 2 * units) == 0);
}

// A field made of nothing but its value — a value's own bytes: its kind, no name, and `size` bytes to fill.
unsigned char* NewField(ibMemoryBuffer& bytes, unsigned char kind, std::uint32_t size)
{
	unsigned char* const field = static_cast<unsigned char*>(bytes.GetWriteBuf(4 + size));
	bytes.UngetWriteBuf(4 + size);
	field[0] = kind;
	field[1] = 0;
	Wr(field, 2, static_cast<std::uint16_t>(0));
	return field + 4;
}

// The block to write in — this buffer's alone (a copy of it when another owner shares it: a copy of the node, a value
// or a range read out of it). GetData() owns nothing (memoryBuffer.h).
unsigned char* Writable(ibMemoryBuffer& bytes)
{
	const std::size_t length = bytes.GetDataLen();
	unsigned char* const base = static_cast<unsigned char*>(bytes.GetWriteBuf(length));
	bytes.UngetWriteBuf(length);
	return base;
}

// An empty node's header — what a node with no bytes reads as.
void EmptyNode(unsigned char* at, ibClassID clsid = 0, ibMetaID metaId = 0) noexcept
{
	std::memset(at, 0, kNodeHeader);
	Wr(at, kLength, kNodeHeader);
	Wr(at, kMetaId, static_cast<std::int32_t>(metaId));
	Wr(at, kClsid, static_cast<std::uint64_t>(clsid));
}

void PacketHeader(unsigned char* at, std::uint32_t length) noexcept
{
	std::memcpy(at, ibBinaryProvider::kMagic, sizeof(ibBinaryProvider::kMagic));
	Wr(at, 4, kVersion);
	Wr(at, 6, static_cast<std::uint16_t>(0));
	Wr(at, 8, length);
	Wr(at, 12, static_cast<std::uint32_t>(0));
}

} // namespace

////////////////////////////////////////////////////////////////////////////
// ibDataKey
////////////////////////////////////////////////////////////////////////////

ibDataKey::ibDataKey(const char* ascii) noexcept : m_ascii(ascii != nullptr ? ascii : ""), m_length(std::strlen(m_ascii)) {}
ibDataKey::ibDataKey(const ibChar* text) noexcept
	: m_utf16(text != nullptr ? text : ibT("")), m_length(std::char_traits<ibChar>::length(m_utf16)) {}

////////////////////////////////////////////////////////////////////////////
// ibDataValue
////////////////////////////////////////////////////////////////////////////

ibDataKind ibDataValue::Kind() const
{
	if (m_bytes.IsEmpty())
		return ibDataKind::Empty;
	const unsigned char kind = Base()[m_at];
	return kind == kWhole ? ibDataKind::Number : static_cast<ibDataKind>(kind);
}

std::uint32_t ibDataValue::ValueAt() const
{
	return ValueOf(Base(), m_at);
}

void ibDataValue::Expect(ibDataKind expected) const
{
	// Empty (an absent field) is allowed — As* yields the default, so a missing tag
	// loads as a default, never an error. Only a present, WRONG kind throws.
	if (Kind() != ibDataKind::Empty && Kind() != expected)
		ibCoreException::Error(_("ibDataValue: wrong value kind (expected %d, got %d)"),
			(int)expected, (int)Kind());
}

ibString ibDataValue::AsString() const
{
	Expect(ibDataKind::String);
	if (Kind() != ibDataKind::String)
		return ibString();
	const std::uint32_t value = ValueAt();
	return FromUtf16(Base() + value + 4, Rd<std::uint32_t>(Base(), value));
}

bool ibDataValue::AsBool() const
{
	Expect(ibDataKind::Bool);
	return Kind() == ibDataKind::Bool && Base()[ValueAt()] != 0;
}

ibNumber ibDataValue::AsNumber() const
{
	Expect(ibDataKind::Number);
	if (Kind() != ibDataKind::Number)
		return ibNumber();
	const std::uint32_t value = ValueAt();
	if (Base()[m_at] == kWhole)
		return ibNumber(static_cast<int64_t>(Rd<std::int64_t>(Base(), value)));
	ibNumber number;
	number.SetBuffer(Base() + value + 4, Rd<std::uint32_t>(Base(), value));
	return number;
}

std::int64_t ibDataValue::AsInt() const
{
	if (!m_bytes.IsEmpty() && Base()[m_at] == kWhole)
		return Rd<std::int64_t>(Base(), ValueAt());
	std::int64_t out = 0;
	AsNumber().ToInt(out);
	return out;
}

std::uint64_t ibDataValue::AsUInt() const
{
	if (!m_bytes.IsEmpty() && Base()[m_at] == kWhole)
		return static_cast<std::uint64_t>(Rd<std::int64_t>(Base(), ValueAt()));
	std::uint64_t out = 0;
	AsNumber().ToInt(out);
	return out;
}

ibDateTime ibDataValue::AsDate() const
{
	// ⭐ A DATE THAT CAME AS ITS TEXT is read by the one who asks for a date. The JSON view writes a date as its ISO
	// string (jsonProvider.cpp) — the wire's clients read it so — and a reading of that text cannot tell it from a
	// string; whoever asks for a date can. "" is the empty date, as the view writes it; a text that names none refuses.
	if (Kind() == ibDataKind::String) {
		ibDateTime date;
		const ibString text = AsString();
		if (!text.IsEmpty() && !date.FromString(text))
			ibCoreException::Error(_("ibDataValue: '%s' is no date"), text);
		return date;
	}
	Expect(ibDataKind::Date);
	return Kind() == ibDataKind::Date ? ibDateTime(Rd<std::int64_t>(Base(), ValueAt())) : ibDateTime();
}

ibMemoryBuffer ibDataValue::AsBinary() const
{
	Expect(ibDataKind::Binary);
	ibMemoryBuffer data;
	if (Kind() == ibDataKind::Binary) {
		const std::uint32_t value = ValueAt();
		const std::uint32_t size = Rd<std::uint32_t>(Base(), value);
		if (size != 0)
			data.AppendData(Base() + value + 4, size);
	}
	return data;
}

std::optional<ibDataNode> ibDataValue::AsChild() const
{
	Expect(ibDataKind::Child);
	if (Kind() != ibDataKind::Child)
		return std::nullopt;
	return ibDataNode(m_bytes, ValueAt());
}

ibDataRange<ibDataValue> ibDataValue::AsArray() const
{
	Expect(ibDataKind::Array);
	if (Kind() != ibDataKind::Array)
		return ibDataRange<ibDataValue>();
	const std::uint32_t value = ValueAt();
	return ibDataRange<ibDataValue>(m_bytes, value + 8, Rd<std::uint32_t>(Base(), value + 4),
		[](const ibMemoryBuffer& bytes, std::uint32_t at) { return ibDataValue(bytes, at); }, &NextField);
}

bool ibDataValue::operator==(const ibDataValue& other) const
{
	if (Kind() != other.Kind())
		return false;
	if (Kind() == ibDataKind::Empty)
		return true;
	if (Base()[m_at] != other.Base()[other.m_at])
		return false;
	const std::uint32_t value = ValueAt(), otherValue = other.ValueAt();
	const std::uint32_t size = ValueSize(Base(), Base()[m_at], value);
	return size == ValueSize(other.Base(), other.Base()[other.m_at], otherValue)
		&& std::memcmp(Base() + value, other.Base() + otherValue, size) == 0;
}

std::size_t ibDataValue::Weight() const
{
	return m_bytes.IsEmpty() ? 4 : NextField(Base(), m_at) - m_at;
}

ibDataValue ibDataValue::String(const ibString& text)
{
	ibDataValue v;
	const std::size_t units = text.length();
	unsigned char* const value = NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::String), 4 + 2 * static_cast<std::uint32_t>(units));
	Wr(value, 0, static_cast<std::uint32_t>(units));
	ToUtf16(text, value + 4);
	return v;
}

ibDataValue ibDataValue::Bool(bool flag)
{
	ibDataValue v;
	unsigned char* const value = NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::Bool), 2);
	value[0] = flag ? 1 : 0;
	value[1] = 0;
	return v;
}

ibDataValue ibDataValue::Number(const ibNumber& number)
{
	// ONE SPELLING A NUMBER: a whole one that fits is written whole — so two equal numbers are equal bytes.
	long long whole = 0;
	if (number.ToInt(whole) == 0 && ibNumber(whole) == number)
		return Int(whole);
	ibDataValue v;
	const ibMemoryBuffer digits = number.GetBuffer();
	const std::uint32_t size = static_cast<std::uint32_t>(digits.GetDataLen());
	unsigned char* const value = NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::Number), 4 + Even(size));
	Wr(value, 0, size);
	if (size != 0)
		std::memcpy(value + 4, digits.GetData(), size);
	if ((size & 1u) != 0)
		value[4 + size] = 0;
	return v;
}

ibDataValue ibDataValue::Int(std::int64_t number)
{
	ibDataValue v;
	Wr(NewField(v.m_bytes, kWhole, 8), 0, number);
	return v;
}

ibDataValue ibDataValue::UInt(std::uint64_t number)
{
	if (number <= static_cast<std::uint64_t>(LLONG_MAX))
		return Int(static_cast<std::int64_t>(number));
	return Number(ibNumber(static_cast<uint64_t>(number)));
}

ibDataValue ibDataValue::Date(const ibDateTime& date)
{
	ibDataValue v;
	Wr(NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::Date), 8), 0, static_cast<std::int64_t>(date.GetValue()));
	return v;
}

ibDataValue ibDataValue::Binary(const ibMemoryBuffer& data)
{
	ibDataValue v;
	const std::uint32_t size = static_cast<std::uint32_t>(data.GetDataLen());
	unsigned char* const value = NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::Binary), 4 + Even(size));
	Wr(value, 0, size);
	if (size != 0)
		std::memcpy(value + 4, data.GetData(), size);
	if ((size & 1u) != 0)
		value[4 + size] = 0;
	return v;
}

ibDataValue ibDataValue::Child(const std::shared_ptr<ibDataNode>& child)
{
	if (!child)
		return ibDataValue();
	ibDataValue v;
	if (child->m_at == 0 && child->m_root == nullptr) {
		EmptyNode(NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::Child), kNodeHeader));
		return v;
	}
	const std::uint32_t length = NodeLength(child->Base(), child->m_at);
	std::memcpy(NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::Child), length), child->Base() + child->m_at, length);
	return v;
}

ibDataValue ibDataValue::Array(const std::vector<ibDataValue>& items)
{
	// Each item a field with no name: its kind and its value's bytes, copied as they are.
	std::uint32_t size = 8;
	for (const ibDataValue& item : items)
		size += item.m_bytes.IsEmpty() ? 4 : 4 + ValueSize(item.Base(), item.Base()[item.m_at], item.ValueAt());
	ibDataValue v;
	unsigned char* const value = NewField(v.m_bytes, static_cast<unsigned char>(ibDataKind::Array), size);
	Wr(value, 0, size);
	Wr(value, 4, static_cast<std::uint32_t>(items.size()));
	unsigned char* at = value + 8;
	for (const ibDataValue& item : items) {
		at[1] = 0;
		Wr(at, 2, static_cast<std::uint16_t>(0));
		if (item.m_bytes.IsEmpty()) {
			at[0] = static_cast<unsigned char>(ibDataKind::Empty);
			at += 4;
			continue;
		}
		const unsigned char kind = item.Base()[item.m_at];
		const std::uint32_t from = item.ValueAt();
		const std::uint32_t length = ValueSize(item.Base(), kind, from);
		at[0] = kind;
		if (length != 0)
			std::memcpy(at + 4, item.Base() + from, length);
		at += 4 + length;
	}
	return v;
}

ibDataValue ibDataValue::Array(std::vector<ibDataValue>&& items)
{
	return Array(static_cast<const std::vector<ibDataValue>&>(items));
}

////////////////////////////////////////////////////////////////////////////
// ibDataNode — the bytes, and where a change goes in them
////////////////////////////////////////////////////////////////////////////

ibDataNode::ibDataNode(ibClassID clsid, ibMetaID metaId)
{
	unsigned char* const at = static_cast<unsigned char*>(m_bytes.GetWriteBuf(kPacketHeader + kNodeHeader));
	m_bytes.UngetWriteBuf(kPacketHeader + kNodeHeader);
	PacketHeader(at, kPacketHeader + kNodeHeader);
	EmptyNode(at + kPacketHeader, clsid, metaId);
	m_at = kPacketHeader;
}

ibDataNode::ibDataNode(const ibDataNode& other)
	: m_bytes(other.Bytes()), m_at(other.m_at)
{
}

ibDataNode::ibDataNode(ibDataNode&& other) noexcept
	: m_at(other.m_at)
{
	if (other.m_root != nullptr) {
		m_bytes = other.Bytes();   // a handle moved out is the node read out of its root, as a copy is
		return;
	}
	m_bytes = std::move(other.m_bytes);
	m_handles = std::move(other.m_handles);
	other.m_at = 0;
	// The handles it gave out are this one's now: their root and the node they are in moved here.
	if (m_handles) {
		for (ibDataNode& handle : *m_handles) {
			handle.m_root = this;
			if (handle.m_parent == &other)
				handle.m_parent = this;
		}
	}
}

ibDataNode& ibDataNode::operator=(const ibDataNode& other)
{
	if (this == &other)
		return *this;
	if (m_root != nullptr) {
		// A sub-node being filled, set to another node: that node's bytes in its place.
		const ibMemoryBuffer source = other.Bytes();   // held: `other` may lie in the very block a write moves
		unsigned char empty[kNodeHeader];
		EmptyNode(empty);
		const unsigned char* const from = other.m_at != 0 ? static_cast<const unsigned char*>(source.GetData()) + other.m_at : empty;
		const std::uint32_t length = NodeLength(from, 0);
		const std::uint32_t was = NodeLength(Base(), m_at);
		if (length > was)
			Insert(m_at + was, length - was, false);
		else if (length < was)
			Erase(m_at + length, was - length, false);
		// …its own length grew with the parts above; the bytes laid over it say their own.
		std::memcpy(Writable(m_root->m_bytes) + m_at, from, length);
		return *this;
	}
	const ibMemoryBuffer bytes = other.Bytes();
	m_handles.reset();
	m_bytes = bytes;
	m_at = other.m_at;
	return *this;
}

ibDataNode& ibDataNode::operator=(ibDataNode&& other) noexcept
{
	if (this == &other)
		return *this;
	if (m_root != nullptr || other.m_root != nullptr)
		return *this = static_cast<const ibDataNode&>(other);
	m_bytes = std::move(other.m_bytes);
	m_at = other.m_at;
	m_handles = std::move(other.m_handles);
	other.m_at = 0;
	if (m_handles) {
		for (ibDataNode& handle : *m_handles) {
			handle.m_root = this;
			if (handle.m_parent == &other)
				handle.m_parent = this;
		}
	}
	return *this;
}

ibDataNode::~ibDataNode() = default;

ibDataNode& ibDataNode::Root()
{
	if (m_root != nullptr)
		return *m_root;
	if (m_at == 0) {
		// A new node: its packet, and the node in it.
		unsigned char* const at = static_cast<unsigned char*>(m_bytes.GetWriteBuf(kPacketHeader + kNodeHeader));
		m_bytes.UngetWriteBuf(kPacketHeader + kNodeHeader);
		PacketHeader(at, kPacketHeader + kNodeHeader);
		EmptyNode(at + kPacketHeader);
		m_at = kPacketHeader;
	}
	else if (m_at != kPacketHeader) {
		// A node read out of another's bytes: a packet of its own, its bytes copied out of them.
		const std::uint32_t length = NodeLength(Base(), m_at);
		ibMemoryBuffer own;
		unsigned char* const at = static_cast<unsigned char*>(own.GetWriteBuf(kPacketHeader + length));
		own.UngetWriteBuf(kPacketHeader + length);
		PacketHeader(at, kPacketHeader + length);
		std::memcpy(at + kPacketHeader, Base() + m_at, length);
		m_bytes = std::move(own);
		m_at = kPacketHeader;
	}
	return *this;
}

unsigned char* ibDataNode::Insert(std::uint32_t at, std::uint32_t size, bool inFields)
{
	ibDataNode& root = m_root != nullptr ? *m_root : *this;
	const std::uint32_t length = static_cast<std::uint32_t>(root.m_bytes.GetDataLen());
	// Its own block to write in — a copy when another owner shares it (a copy of the node, a value read out of it).
	unsigned char* const base = static_cast<unsigned char*>(root.m_bytes.GetWriteBuf(length + size));
	std::memmove(base + at + size, base + at, length - at);
	root.m_bytes.UngetWriteBuf(length + size);
	Wr(base, 8, length + size);
	// What moved on — the sub-nodes being filled past the place.
	if (at < length && root.m_handles) {
		for (ibDataNode& handle : *root.m_handles) {
			if (handle.m_at >= at)
				handle.m_at += size;
			if (handle.m_list != 0 && handle.m_list >= at)
				handle.m_list += size;
		}
	}
	Grow(size, inFields);
	return base + at;
}

void ibDataNode::Erase(std::uint32_t at, std::uint32_t size, bool inFields)
{
	ibDataNode& root = m_root != nullptr ? *m_root : *this;
	const std::uint32_t length = static_cast<std::uint32_t>(root.m_bytes.GetDataLen());
	unsigned char* const base = static_cast<unsigned char*>(root.m_bytes.GetWriteBuf(length));
	std::memmove(base + at, base + at + size, length - at - size);
	root.m_bytes.UngetWriteBuf(length - size);
	Wr(base, 8, length - size);
	if (root.m_handles) {
		for (ibDataNode& handle : *root.m_handles) {
			// A handle to what went is an empty node of its own — not a place in bytes that are no longer it.
			if (handle.m_at >= at && handle.m_at < at + size) {
				handle.m_at = 0;
				handle.m_root = nullptr;
				handle.m_parent = nullptr;
				handle.m_list = 0;
				continue;
			}
			if (handle.m_at > at)
				handle.m_at -= size;
			if (handle.m_list > at)
				handle.m_list -= size;
		}
	}
	Grow(-static_cast<std::int64_t>(size), inFields);
}

void ibDataNode::Grow(std::int64_t delta, bool inFields)
{
	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	bool fields = inFields;
	for (ibDataNode* node = this; node != nullptr; node = node->m_parent) {
		Wr(base, node->m_at + kLength, static_cast<std::uint32_t>(Rd<std::uint32_t>(base, node->m_at + kLength) + delta));
		if (fields)
			Wr(base, node->m_at + kFieldsLength, static_cast<std::uint32_t>(Rd<std::uint32_t>(base, node->m_at + kFieldsLength) + delta));
		// A list's item: the list it is in grows with it — a value among the fields of the node above.
		if (node->m_list != 0)
			Wr(base, node->m_list, static_cast<std::uint32_t>(Rd<std::uint32_t>(base, node->m_list) + delta));
		fields = node->m_inField;   // where it lies in the node it is in
	}
}

ibDataNode& ibDataNode::Handle(std::uint32_t at, bool inField)
{
	ibDataNode& root = Root();
	if (!root.m_handles)
		root.m_handles = std::make_unique<std::deque<ibDataNode>>();
	root.m_handles->emplace_back();
	ibDataNode& handle = root.m_handles->back();
	handle.m_root = &root;
	handle.m_parent = this;
	handle.m_at = at;
	handle.m_inField = inField;
	return handle;
}

////////////////////////////////////////////////////////////////////////////
// ibDataNode — read where it lies
////////////////////////////////////////////////////////////////////////////

ibClassID ibDataNode::GetClsid() const
{
	return m_at != 0 ? static_cast<ibClassID>(Rd<std::uint64_t>(Base(), m_at + kClsid)) : 0;
}

ibMetaID ibDataNode::GetMetaId() const
{
	return m_at != 0 ? static_cast<ibMetaID>(Rd<std::int32_t>(Base(), m_at + kMetaId)) : 0;
}

std::optional<ibDataValue> ibDataNode::FindField(const ibDataKey& name) const
{
	if (m_at == 0)
		return std::nullopt;
	const unsigned char* const b = Base();
	std::uint32_t field = FieldsOf(b, m_at);
	for (std::uint32_t i = Rd<std::uint32_t>(b, m_at + kFieldCount); i != 0; --i) {
		if (NameIs(b, field, name))
			return ibDataValue(Bytes(), field);
		field = NextField(b, field);
	}
	return std::nullopt;
}

ibDataRange<std::pair<ibString, ibDataValue>> ibDataNode::Fields() const
{
	if (m_at == 0)
		return ibDataRange<std::pair<ibString, ibDataValue>>();
	return ibDataRange<std::pair<ibString, ibDataValue>>(Bytes(), FieldsOf(Base(), m_at), Rd<std::uint32_t>(Base(), m_at + kFieldCount),
		[](const ibMemoryBuffer& bytes, std::uint32_t at) {
			const unsigned char* const b = static_cast<const unsigned char*>(bytes.GetData());
			return std::pair<ibString, ibDataValue>(FromUtf16(b + at + 4, NameLength(b, at)), ibDataValue(bytes, at));
		}, &NextField);
}

bool ibDataNode::Has(const ibDataKey& name) const
{
	return FindField(name).has_value();
}

ibDataValue ibDataNode::GetProperty(const ibDataKey& name) const
{
	if (auto v = FindField(name))
		return std::move(*v);
	return ibDataValue();
}

std::optional<ibDataNode> ibDataNode::FindChild(const ibDataKey& name) const
{
	const auto value = FindField(name);
	if (!value || value->Kind() != ibDataKind::Child)
		return std::nullopt;
	return ibDataNode(Bytes(), value->ValueAt());
}

ibDataRange<ibDataNode> ibDataNode::Children() const
{
	if (m_at == 0)
		return ibDataRange<ibDataNode>();
	return ibDataRange<ibDataNode>(Bytes(), ChildrenOf(Base(), m_at), Rd<std::uint32_t>(Base(), m_at + kChildCount),
		[](const ibMemoryBuffer& bytes, std::uint32_t at) { return ibDataNode(bytes, at); }, &NextNode);
}

ibMemoryBuffer ibDataNode::RawData() const
{
	ibMemoryBuffer data;
	if (m_at != 0) {
		const std::uint32_t size = Rd<std::uint32_t>(Base(), m_at + kRawLength);
		if (size != 0)
			data.AppendData(Base() + m_at + kNodeHeader, size);
	}
	return data;
}

bool ibDataNode::HasRawData() const
{
	return m_at != 0 && Rd<std::uint32_t>(Base(), m_at + kRawLength) != 0;
}

bool ibDataNode::operator==(const ibDataNode& other) const
{
	unsigned char empty[kNodeHeader];
	EmptyNode(empty);
	const unsigned char* const one = m_at != 0 ? Base() + m_at : empty;
	const unsigned char* const two = other.m_at != 0 ? other.Base() + other.m_at : empty;
	const std::uint32_t length = Rd<std::uint32_t>(one, kLength);
	return length == Rd<std::uint32_t>(two, kLength) && std::memcmp(one, two, length) == 0;
}

std::size_t ibDataNode::Weight() const
{
	return m_at != 0 ? NodeLength(Base(), m_at) : kNodeHeader;
}

////////////////////////////////////////////////////////////////////////////
// ibDataNode — written where it lies
////////////////////////////////////////////////////////////////////////////

// The bytes taken first: a new node gets them, and its place, from Root() — its place read in the same call was 0
// (the order a call's arguments are worked out in is the compiler's), and a builder's root wrote its clsid over its counts.
void ibDataNode::SetClsid(ibClassID clsid)
{
	unsigned char* const base = Writable(Root().m_bytes);
	Wr(base, m_at + kClsid, static_cast<std::uint64_t>(clsid));
}

void ibDataNode::SetMetaId(ibMetaID metaId)
{
	unsigned char* const base = Writable(Root().m_bytes);
	Wr(base, m_at + kMetaId, static_cast<std::int32_t>(metaId));
}

void ibDataNode::AddField(const ibString& name, const ibDataValue& value)
{
	Root();
	// The value's bytes held — a value read out of this very node lies in the block a write moves: held, it stays put
	// and the node writes in a copy.
	const ibMemoryBuffer source = value.m_bytes;
	const unsigned char kind = source.IsEmpty() ? static_cast<unsigned char>(ibDataKind::Empty) : value.Base()[value.m_at];
	const std::uint32_t from = source.IsEmpty() ? 0 : value.ValueAt();
	const std::uint32_t size = source.IsEmpty() ? 0 : ValueSize(value.Base(), kind, from);

	const std::uint32_t units = static_cast<std::uint32_t>(name.length());
	unsigned char* const field = Insert(ChildrenOf(Base(), m_at), 4 + 2 * units + size, true);
	field[0] = kind;
	field[1] = 0;
	Wr(field, 2, static_cast<std::uint16_t>(units));
	ToUtf16(name, field + 4);
	if (size != 0)
		std::memcpy(field + 4 + 2 * units, static_cast<const unsigned char*>(source.GetData()) + from, size);

	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	Wr(base, m_at + kFieldCount, Rd<std::uint32_t>(base, m_at + kFieldCount) + 1);
}

void ibDataNode::SetField(const ibString& name, const ibDataValue& value)
{
	if (m_at == 0) {
		AddField(name, value);
		return;
	}
	const unsigned char* b = Base();
	std::uint32_t field = FieldsOf(b, m_at);
	std::uint32_t i = Rd<std::uint32_t>(b, m_at + kFieldCount);
	for (; i != 0 && !NameIs(b, field, ibDataKey(name)); --i)
		field = NextField(b, field);
	if (i == 0) {
		AddField(name, value);
		return;
	}
	// Replaced where it stands — its name kept, its value's bytes resized in place. Its place counted from the node:
	// a node read out is a root of its own once it is written (Root), the same bytes at another place.
	const std::uint32_t place = field - m_at;
	const ibMemoryBuffer source = value.m_bytes;   // held, as AddField holds it
	const unsigned char kind = source.IsEmpty() ? static_cast<unsigned char>(ibDataKind::Empty) : value.Base()[value.m_at];
	const std::uint32_t from = source.IsEmpty() ? 0 : value.ValueAt();
	const std::uint32_t size = source.IsEmpty() ? 0 : ValueSize(value.Base(), kind, from);

	Root();
	field = m_at + place;
	b = Base();
	const std::uint32_t at = ValueOf(b, field);
	const std::uint32_t was = ValueSize(b, b[field], at);
	if (size > was)
		Insert(at + was, size - was, true);
	else if (size < was)
		Erase(at + size, was - size, true);
	unsigned char* const base = Writable(Root().m_bytes);
	base[field] = kind;
	if (size != 0)
		std::memcpy(base + at, static_cast<const unsigned char*>(source.GetData()) + from, size);
}

void ibDataNode::RemoveField(const ibDataKey& name)
{
	if (m_at == 0)
		return;
	const unsigned char* const b = Base();
	std::uint32_t field = FieldsOf(b, m_at);
	for (std::uint32_t i = Rd<std::uint32_t>(b, m_at + kFieldCount); i != 0; --i) {
		if (NameIs(b, field, name)) {
			const std::uint32_t place = field - m_at, size = NextField(b, field) - field;   // counted from the node (SetField)
			Root();
			Erase(m_at + place, size, true);
			unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
			Wr(base, m_at + kFieldCount, Rd<std::uint32_t>(base, m_at + kFieldCount) - 1);
			return;
		}
		field = NextField(b, field);
	}
}

void ibDataNode::SetRawData(const ibMemoryBuffer& data)
{
	Root();
	const std::uint32_t size = static_cast<std::uint32_t>(data.GetDataLen());
	const std::uint32_t was = Even(Rd<std::uint32_t>(Base(), m_at + kRawLength));
	const std::uint32_t now = Even(size);
	const std::uint32_t at = m_at + kNodeHeader;
	if (now > was)
		Insert(at + was, now - was, false);
	else if (now < was)
		Erase(at + now, was - now, false);
	unsigned char* const base = Writable(Root().m_bytes);
	if (size != 0)
		std::memcpy(base + at, data.GetData(), size);
	if (now != size)
		base[at + size] = 0;
	Wr(base, m_at + kRawLength, size);
}

ibDataNode& ibDataNode::Child(const ibString& name)
{
	Root();
	// The one under the name already, filled on where it lies (a writer that comes back to it: a schema's arguments).
	{
		const unsigned char* const b = Base();
		std::uint32_t field = FieldsOf(b, m_at);
		for (std::uint32_t i = Rd<std::uint32_t>(b, m_at + kFieldCount); i != 0; --i) {
			if (b[field] == static_cast<unsigned char>(ibDataKind::Child) && NameIs(b, field, ibDataKey(name)))
				return Handle(ValueOf(b, field), true);
			field = NextField(b, field);
		}
	}
	const std::uint32_t units = static_cast<std::uint32_t>(name.length());
	const std::uint32_t at = ChildrenOf(Base(), m_at);
	unsigned char* const field = Insert(at, 4 + 2 * units + kNodeHeader, true);
	field[0] = static_cast<unsigned char>(ibDataKind::Child);
	field[1] = 0;
	Wr(field, 2, static_cast<std::uint16_t>(units));
	ToUtf16(name, field + 4);
	EmptyNode(field + 4 + 2 * units);
	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	Wr(base, m_at + kFieldCount, Rd<std::uint32_t>(base, m_at + kFieldCount) + 1);
	return Handle(at + 4 + 2 * units, true);
}

ibDataNode& ibDataNode::AddChild(ibClassID clsid, ibMetaID metaId)
{
	Root();
	const std::uint32_t at = NextNode(Base(), m_at);
	EmptyNode(Insert(at, kNodeHeader, false), clsid, metaId);
	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	Wr(base, m_at + kChildCount, Rd<std::uint32_t>(base, m_at + kChildCount) + 1);
	return Handle(at, false);
}

ibDataNode& ibDataNode::AddChild(const ibDataNode& child)
{
	Root();
	// Its bytes held, as a value's are (AddField).
	const ibMemoryBuffer source = child.Bytes();
	unsigned char empty[kNodeHeader];
	EmptyNode(empty);
	const unsigned char* const from = child.m_at != 0 ? static_cast<const unsigned char*>(source.GetData()) + child.m_at : empty;
	const std::uint32_t length = NodeLength(from, 0);
	const std::uint32_t at = NextNode(Base(), m_at);
	std::memcpy(Insert(at, length, false), from, length);
	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	Wr(base, m_at + kChildCount, Rd<std::uint32_t>(base, m_at + kChildCount) + 1);
	return Handle(at, false);
}

void ibDataNode::RemoveChildren()
{
	if (m_at == 0 || Rd<std::uint32_t>(Base(), m_at + kChildCount) == 0)
		return;
	// The children are the node's last part: from where they begin to its end. Counted from the node (SetField).
	const std::uint32_t place = ChildrenOf(Base(), m_at) - m_at, size = NodeLength(Base(), m_at) - place;
	Root();
	Erase(m_at + place, size, false);
	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	Wr(base, m_at + kChildCount, static_cast<std::uint32_t>(0));
}

std::uint32_t ibDataNode::ListEnd(const ibString& name, std::uint32_t& list)
{
	Root();
	const unsigned char* b = Base();
	std::uint32_t field = FieldsOf(b, m_at);
	std::uint32_t i = Rd<std::uint32_t>(b, m_at + kFieldCount);
	for (; i != 0; --i) {
		if (b[field] == static_cast<unsigned char>(ibDataKind::Array) && NameIs(b, field, ibDataKey(name)))
			break;
		field = NextField(b, field);
	}
	if (i == 0) {
		// None yet: an empty one, the last of the fields — where the loop stopped.
		AddField(name, ibDataValue::Array(std::vector<ibDataValue>()));
		b = Base();
	}
	list = ValueOf(b, field);
	return list + Rd<std::uint32_t>(b, list);
}

ibDataNode& ibDataNode::AddItem(const ibString& name)
{
	std::uint32_t list = 0;
	const std::uint32_t at = ListEnd(name, list);
	unsigned char* const item = Insert(at, 4 + kNodeHeader, true);
	item[0] = static_cast<unsigned char>(ibDataKind::Child);
	item[1] = 0;
	Wr(item, 2, static_cast<std::uint16_t>(0));
	EmptyNode(item + 4);
	// …the list grows by it — its length and its count; the node it is in grew with Insert.
	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	Wr(base, list, Rd<std::uint32_t>(base, list) + 4 + kNodeHeader);
	Wr(base, list + 4, Rd<std::uint32_t>(base, list + 4) + 1);
	ibDataNode& handle = Handle(at + 4, true);
	handle.m_list = list;
	return handle;
}

void ibDataNode::AddItem(const ibString& name, const ibDataValue& value)
{
	// The value's bytes held, as AddField holds them.
	const ibMemoryBuffer source = value.m_bytes;
	const unsigned char kind = source.IsEmpty() ? static_cast<unsigned char>(ibDataKind::Empty) : value.Base()[value.m_at];
	const std::uint32_t from = source.IsEmpty() ? 0 : value.ValueAt();
	const std::uint32_t size = source.IsEmpty() ? 0 : ValueSize(value.Base(), kind, from);

	std::uint32_t list = 0;
	const std::uint32_t at = ListEnd(name, list);
	unsigned char* const item = Insert(at, 4 + size, true);
	item[0] = kind;
	item[1] = 0;
	Wr(item, 2, static_cast<std::uint16_t>(0));
	if (size != 0)
		std::memcpy(item + 4, static_cast<const unsigned char*>(source.GetData()) + from, size);
	unsigned char* const base = static_cast<unsigned char*>(Root().m_bytes.GetData());
	Wr(base, list, Rd<std::uint32_t>(base, list) + 4 + size);
	Wr(base, list + 4, Rd<std::uint32_t>(base, list + 4) + 1);
}

ibDataNode& ibDataNode::Handle(const ibDataNode& part)
{
	// Where it lies in this one: among its children, else in its fields — a sub-node's value, or a list's item, whose
	// list grows with it. Its place is in the bytes it was read from; this one, read out of them too, is a packet of its
	// own once Root copies it out, and the place moves with it.
	const std::uint32_t was = m_at;
	Root();
	const unsigned char* const b = Base();
	const std::uint32_t at = part.m_at - was + m_at;
	if (at >= ChildrenOf(b, m_at))
		return Handle(at, false);
	std::uint32_t field = FieldsOf(b, m_at);
	for (std::uint32_t i = Rd<std::uint32_t>(b, m_at + kFieldCount); i != 0; --i) {
		const std::uint32_t next = NextField(b, field);
		if (at < next) {
			if (b[field] != static_cast<unsigned char>(ibDataKind::Array))
				break;
			const std::uint32_t list = ValueOf(b, field);
			ibDataNode& handle = Handle(at, true);
			handle.m_list = list;
			return handle;
		}
		field = next;
	}
	return Handle(at, true);
}

////////////////////////////////////////////////////////////////////////////
// ibBinaryProvider — the bytes handed over, and taken as the node they are
////////////////////////////////////////////////////////////////////////////
namespace {

// Bytes from outside, checked before anything is read through them: every length inside its container, every count
// what its lengths hold, every kind known, the nesting bounded. Inline all through, so the walk is linear.
struct ibPacketCheck {
	const unsigned char* m_base;

	// Throws (ibCoreException::Error) — nothing past it runs.
	void Refuse(const ibChar* why, std::uint32_t at) const
	{
		ibCoreException::Error(_("ibBinaryProvider: the bytes are no OES-RPC packet: %s at byte %u"), ibString(why), at);
	}

	void Node(std::uint32_t node, std::uint32_t end, int depth) const
	{
		if (depth > ibBinaryProvider::kMaxNesting)
			Refuse(ibT("nesting too deep"), node);
		// …and a reserve that runs out first is the reader's, not the bytes': the packet may be a sound one.
		if (ibFiber::StackLow())
			ibCoreException::Error(_("ibBinaryProvider: the packet is nested deeper than the stack allows"));
		if (std::uint64_t(node) + kNodeHeader > end)
			Refuse(ibT("a node's header past its container"), node);
		const std::uint32_t length = Rd<std::uint32_t>(m_base, node + kLength);
		if (length < kNodeHeader || (length & 1u) != 0 || std::uint64_t(node) + length > end)
			Refuse(ibT("a node's length out of its container"), node);
		const std::uint32_t stop = node + length;
		const std::uint64_t fields = std::uint64_t(node) + kNodeHeader + Even(Rd<std::uint32_t>(m_base, node + kRawLength));
		const std::uint64_t children = fields + Rd<std::uint32_t>(m_base, node + kFieldsLength);
		if (children > stop)
			Refuse(ibT("a node's parts past its length"), node);
		std::uint32_t at = static_cast<std::uint32_t>(fields);
		for (std::uint32_t i = Rd<std::uint32_t>(m_base, node + kFieldCount); i != 0; --i)
			at = Field(at, static_cast<std::uint32_t>(children), depth);
		if (at != children)
			Refuse(ibT("a node's fields not what their length says"), node);
		for (std::uint32_t i = Rd<std::uint32_t>(m_base, node + kChildCount); i != 0; --i) {
			Node(at, stop, depth + 1);
			at += Rd<std::uint32_t>(m_base, at + kLength);
		}
		if (at != stop)
			Refuse(ibT("a node's children not what its length says"), node);
	}

	// The field at `field`, inside `end`; the place past it.
	std::uint32_t Field(std::uint32_t field, std::uint32_t end, int depth) const
	{
		if (std::uint64_t(field) + 4 > end)
			Refuse(ibT("a field past its container"), field);
		const unsigned char kind = m_base[field];
		if (kind > kWhole)
			Refuse(ibT("an unknown kind"), field);
		const std::uint64_t value = std::uint64_t(field) + 4 + 2ull * NameLength(m_base, field);
		std::uint64_t size = 0;
		switch (kind) {
		case static_cast<unsigned char>(ibDataKind::Empty):  break;
		case static_cast<unsigned char>(ibDataKind::Bool):   size = 2; break;
		case kWhole:
		case static_cast<unsigned char>(ibDataKind::Date):   size = 8; break;
		case static_cast<unsigned char>(ibDataKind::Number):
		case static_cast<unsigned char>(ibDataKind::Binary):
			if (value + 4 > end)
				Refuse(ibT("a length past its container"), field);
			size = 4 + Even(Rd<std::uint32_t>(m_base, static_cast<std::uint32_t>(value)));
			break;
		case static_cast<unsigned char>(ibDataKind::String):
			if (value + 4 > end)
				Refuse(ibT("a length past its container"), field);
			size = 4 + 2ull * Rd<std::uint32_t>(m_base, static_cast<std::uint32_t>(value));
			break;
		case static_cast<unsigned char>(ibDataKind::Child):
			Node(static_cast<std::uint32_t>(value), end, depth + 1);
			size = Rd<std::uint32_t>(m_base, static_cast<std::uint32_t>(value) + kLength);
			break;
		case static_cast<unsigned char>(ibDataKind::Array): {
			if (value + 8 > end)
				Refuse(ibT("a list's header past its container"), field);
			if (depth + 1 > ibBinaryProvider::kMaxNesting)
				Refuse(ibT("nesting too deep"), field);
			size = Rd<std::uint32_t>(m_base, static_cast<std::uint32_t>(value));
			if (size < 8 || value + size > end)
				Refuse(ibT("a list's length out of its container"), field);
			const std::uint32_t stop = static_cast<std::uint32_t>(value + size);
			std::uint32_t at = static_cast<std::uint32_t>(value) + 8;
			for (std::uint32_t i = Rd<std::uint32_t>(m_base, static_cast<std::uint32_t>(value) + 4); i != 0; --i)
				at = Field(at, stop, depth + 1);
			if (at != stop)
				Refuse(ibT("a list's items not what its length says"), field);
			break;
		}
		}
		if (value + size > end)
			Refuse(ibT("a value past its container"), field);
		return static_cast<std::uint32_t>(value + size);
	}
};

} // namespace

bool ibBinaryProvider::IsPacket(const void* bytes, std::size_t size)
{
	return bytes != nullptr && size >= sizeof(kMagic) && std::memcmp(bytes, kMagic, sizeof(kMagic)) == 0;
}

bool ibBinaryProvider::Write(const ibDataNode& root, ibMemoryBuffer& packet) const
{
	// A root is its packet already: handed over, one more owner of it.
	if (root.m_root == nullptr && root.m_at == kPacketHeader) {
		packet = root.m_bytes;
		return true;
	}
	// A node with no bytes yet, or one inside another's: a packet of its own.
	const std::uint32_t length = root.m_at != 0 ? NodeLength(root.Base(), root.m_at) : kNodeHeader;
	ibMemoryBuffer own;
	unsigned char* const at = static_cast<unsigned char*>(own.GetWriteBuf(kPacketHeader + length));
	own.UngetWriteBuf(kPacketHeader + length);
	PacketHeader(at, kPacketHeader + length);
	if (root.m_at != 0) std::memcpy(at + kPacketHeader, root.Base() + root.m_at, length);
	else                EmptyNode(at + kPacketHeader);
	packet = std::move(own);
	return true;
}

bool ibBinaryProvider::Read(const ibMemoryBuffer& packet, ibDataNode& root) const
{
	const unsigned char* const base = static_cast<const unsigned char*>(packet.GetData());
	const std::size_t size = packet.GetDataLen();
	if (base == nullptr || size < kPacketHeader + kNodeHeader || size > 0xFFFFFFF0u)
		ibCoreException::Error(_("ibBinaryProvider: the bytes are no OES-RPC packet: %s"), ibT("too short"));
	if (!IsPacket(base, size))
		ibCoreException::Error(_("ibBinaryProvider: the bytes are no OES-RPC packet: %s"), ibT("no OES1 at their start"));
	if (Rd<std::uint16_t>(base, 4) != kVersion)
		ibCoreException::Error(_("ibBinaryProvider: the packet is OES-RPC version %u, this end reads %u"),
			(unsigned)Rd<std::uint16_t>(base, 4), (unsigned)kVersion);
	if (Rd<std::uint32_t>(base, 8) != size)
		ibCoreException::Error(_("ibBinaryProvider: the bytes are no OES-RPC packet: %s"), ibT("their length is not what they say"));
	const ibPacketCheck check{ base };
	check.Node(kPacketHeader, static_cast<std::uint32_t>(size), 0);
	if (kPacketHeader + NodeLength(base, kPacketHeader) != size)
		ibCoreException::Error(_("ibBinaryProvider: the bytes are no OES-RPC packet: %s"), ibT("bytes past the root"));
	root = ibDataNode(packet, kPacketHeader);
	return true;
}

bool ibBinaryProvider::Write(const ibDataNode& root, ibWriterMemory& writer) const
{
	ibMemoryBuffer packet;
	Write(root, packet);
	writer.w(packet.GetData(), static_cast<std::uint32_t>(packet.GetDataLen()));
	return true;
}

bool ibBinaryProvider::Read(ibReaderMemory& reader, ibDataNode& root) const
{
	// The packet says how long it is; its bytes are taken out of the stream as they are and read where they lie then.
	if (reader.elapsed() < static_cast<int>(kPacketHeader))
		ibCoreException::Error(_("ibBinaryProvider: the bytes are no OES-RPC packet: %s"), ibT("too short"));
	const std::uint32_t length = Rd<std::uint32_t>(static_cast<const unsigned char*>(reader.pointer()), 8);
	if (length < kPacketHeader || static_cast<std::int64_t>(length) > reader.elapsed())
		ibCoreException::Error(_("ibBinaryProvider: the bytes are no OES-RPC packet: %s"), ibT("their length past the stream"));
	const ibMemoryBuffer packet(reader.pointer(), length);
	reader.advance(static_cast<int>(length));
	return Read(packet, root);
}
