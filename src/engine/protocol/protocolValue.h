#ifndef __PROTOCOL_VALUE_H__
#define __PROTOCOL_VALUE_H__

#include <memory>
#include <vector>

#include "core/fstring.h"   // ibString — every text of the tree

#include "protocol/protocolApi.h"

struct ibProtocolBody;

// ONE VALUE OF THE WIRE, as the client holds it — what ibProtocolNode is a handle into, and what the library's own
// code (the communicator, the patch) works on: nothing, a flag, a whole number, a text, a number of another kind (its
// digits as the wire wrote them: the client reads no decimal), a node or a list. Every text is the engine's: read from
// the answer's characters where they lie, handed on as one more owner of it, never converted.
//
// A node's entries and a list's items are held apart from the value (ibProtocolBody), and say which of the two they
// are: a value moves as entries are added beside it, and what a handle to a node holds on to stays where it is.
// Exported with its body: the client reads the engine's nodes off this tree where it lies (ibReadProtocolNode).
class PROTOCOL_API ibProtocolValue {
public:

	enum class ibKind : unsigned char { Null, Bool, Int, Text, Number, Node, List };

	ibProtocolValue() noexcept = default;
	explicit ibProtocolValue(bool flag) noexcept : m_kind(ibKind::Bool), m_number(flag ? 1 : 0) {}
	explicit ibProtocolValue(long long number) noexcept : m_kind(ibKind::Int), m_number(number) {}
	explicit ibProtocolValue(const ibString& text) noexcept : m_kind(ibKind::Text), m_text(text) {}
	// A number of another kind, by its digits as written.
	static ibProtocolValue Number(const ibString& digits);
	// An empty node, or list, to fill.
	static ibProtocolValue Node();
	static ibProtocolValue List();

	// A copy is a tree of its own; a move takes the tree. Either may be of a value inside the one it is assigned to:
	// what it holds is taken before what was there goes.
	ibProtocolValue(const ibProtocolValue& other);
	ibProtocolValue& operator=(const ibProtocolValue& other);
	ibProtocolValue(ibProtocolValue&& other) noexcept;
	ibProtocolValue& operator=(ibProtocolValue&& other) noexcept;
	~ibProtocolValue();

	ibKind          GetKind() const;
	bool            AsBool() const { return m_number != 0; }
	long long       AsInt() const { return m_number; }
	const ibString& AsText() const { return m_text; }   // a text, or a number's digits
	// A node's entries, a list's items — null when the value is not one.
	ibProtocolBody* GetNode() const;
	ibProtocolBody* GetList() const;
	ibProtocolBody* GetBody() const { return m_body.get(); }   // either

	// The value a JSON text is — false when it is not JSON — read over the characters themselves; and the text a value
	// is, appended.
	static bool Read(const wchar_t* text, size_t length, ibProtocolValue& value);
	void        Write(ibString& text) const;

	// Two values alike: a node's entries in whatever order, a list's items in theirs.
	bool operator==(const ibProtocolValue& other) const;
	bool operator!=(const ibProtocolValue& other) const { return !(*this == other); }

private:
	// ⚠ THE BODY LAST: a move assigns in this order, so the other's text is taken before the body this one held — which
	// the other may lie in — goes.
	ibKind                          m_kind = ibKind::Null;   // a node and a list: their body says which
	long long                       m_number = 0;            // Bool, Int
	ibString                        m_text;                  // Text, Number
	std::unique_ptr<ibProtocolBody> m_body;                  // Node, List
};

// An entry of a node — its name and its value; an item of a list has no name.
struct ibProtocolEntry {
	ibString        m_name;
	ibProtocolValue m_value;
};

// …and the entries of a node, the items of a list, in the order they were written.
struct PROTOCOL_API ibProtocolBody {
	bool                         m_list = false;
	std::vector<ibProtocolEntry> m_entries;

	// A node's entry by its name — the last written under it, as a JSON reader takes a name said twice; null when there
	// is none.
	ibProtocolValue*       Find(const char* name);
	const ibProtocolValue* Find(const char* name) const;
	ibProtocolValue*       Find(const ibString& name);
	const ibProtocolValue* Find(const ibString& name) const;
	// …made, nothing in it, when there is none — to set.
	ibProtocolValue& Set(const char* name);
	ibProtocolValue& Set(const ibString& name);
	void             Remove(const char* name);
	void             Remove(const ibString& name);
	// A list's item at its end — to set.
	ibProtocolValue& Add();

	// As ibProtocolValue's — what a node or a list is, written and compared.
	void Write(ibString& text) const;
	bool operator==(const ibProtocolBody& other) const;
};

inline ibProtocolValue::ibProtocolValue(ibProtocolValue&& other) noexcept = default;
inline ibProtocolValue& ibProtocolValue::operator=(ibProtocolValue&& other) noexcept = default;
inline ibProtocolValue::~ibProtocolValue() = default;

inline ibProtocolValue::ibKind ibProtocolValue::GetKind() const
{
	return m_body != nullptr ? (m_body->m_list ? ibKind::List : ibKind::Node) : m_kind;
}
inline ibProtocolBody* ibProtocolValue::GetNode() const
{
	return m_body != nullptr && !m_body->m_list ? m_body.get() : nullptr;
}
inline ibProtocolBody* ibProtocolValue::GetList() const
{
	return m_body != nullptr && m_body->m_list ? m_body.get() : nullptr;
}

#endif
