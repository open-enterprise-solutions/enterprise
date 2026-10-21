#ifndef __PROTOCOL_NODE_H__
#define __PROTOCOL_NODE_H__

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <wx/string.h>

#include "core/fstring.h"   // ibString — every text of the node, and the text it is read from and written to

#include "protocol/protocolApi.h"

class ibProtocolValue;
struct ibProtocolBody;

// A NODE OF THE WIRE — what the protocol carries, as this client reads and fills it: a node's entries by name (the
// server's fields and properties in one set, as the wire has them), its children (`NodeChildren`), its id (`NodeId`)
// and its type's NAME (`NodeType`). The values are what a thin client meets: text, whole numbers, flags, lists and
// nodes — no decimals, no rich strings: a value travels as text (`Text`, `Value.v`) and the server formats it. Every
// text is the engine's (ibString), read where the answer's characters lie and handed out as one more owner of it.
//
// The verbs are the server's ibDataNode's; the ownership is not. It is a HANDLE: a node made here owns its tree, and
// a node read from it shares that tree — cheap to pass, alive while any handle to it is. A handle to a node stays good
// while entries are added beside it; a handle to a text, a number or a flag is read before its node changes. A handle
// into the communicator's frame is read between calls: the next answer changes the frame under it.
class PROTOCOL_API ibProtocolNode {
public:

	// A new node with no entries — to fill (a call's parameters).
	ibProtocolNode();

	// The node a JSON text is — false when the text is not JSON; and the text a node is.
	static bool Read(const ibString& text, ibProtocolNode& node);
	ibString Write() const;

	bool IsEmpty() const;   // asked for a name the node does not have — or a null
	bool IsNode() const;    // a node with entries
	bool IsList() const;
	// …and as a value: text, a whole number, a flag.
	bool IsString() const;
	bool IsInt() const;
	bool IsBool() const;

	// — its entries, by name: `otherwise` when the node does not have one, or has it of another kind —
	bool      Has(const char* name) const;
	ibString  GetString(const char* name, const ibString& otherwise = ibString()) const;
	long long GetInt(const char* name, long long otherwise = 0) const;
	bool      GetBool(const char* name, bool otherwise = false) const;
	// A sub-node — an empty one (IsEmpty) when there is none; a list's items.
	ibProtocolNode              FindChild(const char* name) const;
	std::vector<ibProtocolNode> GetList(const char* name) const;
	std::vector<ibProtocolNode> GetList(const ibString& name) const;
	// All of its entries, by name as the wire has it — what SetValue takes back — a node read whole (ibDataValue::Read).
	std::vector<std::pair<ibString, ibProtocolNode>> Entries() const;
	// …and where they stay — a node's entries, a list's items (ibProtocolValue::GetBody): what a reader of the whole tree
	// walks without a handle per entry (the client's ibReadProtocolNode). Null when it is neither.
	const ibProtocolBody* GetBody() const;
	// The node itself as a value — a list's item.
	ibString  AsString() const;
	long long AsInt() const;
	bool      AsBool() const;

	// — as the protocol writes a node —
	long long                   GetId() const;     // NodeId; 0 — none
	ibString                    GetType() const;   // NodeType — its type's name
	std::vector<ibProtocolNode> Children() const;  // NodeChildren

	// — filling —
	ibProtocolNode& SetValue(const char* name, const ibString& text);
	ibProtocolNode& SetValue(const char* name, long long number);
	ibProtocolNode& SetValue(const char* name, int number);
	// A flag is a bool itself, never what converts to one — a literal, a pointer, a wxString's text (wx converts it to
	// one): each of those is a text.
	template<class Flag, std::enable_if_t<std::is_same_v<Flag, bool>, int> = 0>
	ibProtocolNode& SetValue(const char* name, Flag flag) { return SetFlag(name, flag); }
	ibProtocolNode& SetValue(const char* name, const ibProtocolNode& node);   // a copy of it
	ibProtocolNode& SetValue(const ibString& name, const ibProtocolNode& node);
	// The sub-node under the name — made when there is none — to fill.
	ibProtocolNode  Child(const char* name);
	// A child at the end of NodeChildren, to fill.
	ibProtocolNode  AddChild();
	// An item at the end of the list under the name — made when there is none: a node to fill, a number, or text.
	ibProtocolNode  AddItem(const char* name);
	void            AddItem(const char* name, long long number);
	void            AddItem(const char* name, const ibString& text);
	void            Remove(const char* name);

	// A PATCH APPLIED — this frame, as the client holds it, made into the frame drawn now: the other half of the server's
	// ibClientFramePatch (frmserver/client/clientPatch.h), by the rules the protocol states (docs/public/
	// client-protocol.md, The frame) — the same a browser applies (framePatch.cpp).
	void Apply(const ibProtocolNode& patch);
	// …and of a patch's node, how many entries of its node it sets or removes — those besides its type, its id and its
	// children. 0: it only leads to its children.
	std::size_t GetChangeCount() const;

	// A copy with a tree of its own.
	ibProtocolNode Clone() const;

	// Two nodes alike: the same entries — in whatever order — and the same children in the same order.
	bool operator==(const ibProtocolNode& other) const;
	bool operator!=(const ibProtocolNode& other) const { return !(*this == other); }

private:

	friend class ibCommunicator;

	ibProtocolNode(std::shared_ptr<ibProtocolValue> root, ibProtocolValue* value);
	// Owning the tree, moved in.
	explicit ibProtocolNode(ibProtocolValue&& tree);

	ibProtocolNode& SetFlag(const char* name, bool flag);
	// A list's items, as handles of this tree.
	std::vector<ibProtocolNode> Items(ibProtocolBody* list) const;
	// What it is, as a value of its own — what SetValue and Clone take.
	ibProtocolValue Copy() const;
	// The entries it fills — itself made a node when it is a text, a number or a flag.
	ibProtocolBody* Node();
	// …and the list under the name, made when there is none.
	ibProtocolBody* ListOf(const char* name);

	// WHAT IT IS: a node's entries or a list's items where they stay (m_body); a text, a number or a flag where it lies
	// (m_value — where the value lay when the handle was made). Neither — an empty handle.
	std::shared_ptr<ibProtocolValue> m_root;
	ibProtocolValue*                 m_value = nullptr;
	ibProtocolBody*                  m_body = nullptr;
};

#endif
