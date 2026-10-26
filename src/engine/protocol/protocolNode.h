#ifndef __PROTOCOL_NODE_H__
#define __PROTOCOL_NODE_H__

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <wx/string.h>

#include "core/fstring.h"                 // ibString — every text of the node, and the text it is read from and written to
#include "core/memory/memoryBuffer.h"     // ibMemoryBuffer — the packet it is read from and written to
#include "core/serialize/dataBuilder.h"   // ibDataNode — what it is

#include "protocol/protocolApi.h"

struct ibProtocolTree;   // below

// A NODE OF THE WIRE — what the protocol carries, as this client reads and fills it: a node's entries by name (the
// server's fields and properties in one set, as the wire has them), its children (`NodeChildren`), its id (`NodeId`)
// and its type's NAME (`NodeType`). The values are what a thin client meets: text, whole numbers, flags, bytes (a
// picture's PNG, a file's part), lists and nodes — no decimals, no rich strings: a value travels as text (`Text`,
// `Value.v`) and the server formats it.
//
// ⭐ IT IS THE PACKET'S ibDataNode (OES-RPC 1.0, core/serialize/dataBuilder.h) — read where it lies, nothing parsed: an
// answer is the bytes that came, one more owner of them. The names are the JSON view's (ibJsonProvider): `NodeType`
// is the node's clsid, named by the packet's `types`; `NodeId` its metaId; `NodeChildren` its children.
//
// It is a HANDLE: a node made here owns its tree, and a node read from it shares that tree — cheap to pass, alive while
// any handle to it is. In a tree made here (a new node, a Clone) a handle to a node is good while entries are added
// beside it and changes it writes are the tree's; a handle to a text, a number or a flag is read before its node
// changes. A tree read from a packet is read where it lies: a node read out of it and written to becomes a tree of
// its own — its root writes in place. A handle into the communicator's frame is read between calls: the next answer
// is another frame.
class PROTOCOL_API ibProtocolNode {
public:

	// A new node with no entries — to fill (a call's parameters).
	ibProtocolNode();
	// A tree made of an engine's node — its bytes, one more owner of them.
	explicit ibProtocolNode(const ibDataNode& node);

	// The node a packet is — false when the bytes are not one; and the packet a node is.
	static bool Read(const ibMemoryBuffer& packet, ibProtocolNode& node);
	void        Write(ibMemoryBuffer& packet) const;
	// …and the node a JSON text is — false when the text is not JSON; and the text a node is.
	static bool Read(const ibString& text, ibProtocolNode& node);
	ibString    Write() const;

	bool IsEmpty() const;   // asked for a name the node does not have — or a null
	bool IsNode() const;    // a node with entries
	bool IsList() const;
	// …and as a value: text, a whole number, a flag, bytes.
	bool IsString() const;
	bool IsInt() const;
	bool IsBool() const;
	bool IsBinary() const;

	// — its entries, by name: `otherwise` when the node does not have one, or has it of another kind —
	bool           Has(const char* name) const;
	ibString       GetString(const char* name, const ibString& otherwise = ibString()) const;
	long long      GetInt(const char* name, long long otherwise = 0) const;
	bool           GetBool(const char* name, bool otherwise = false) const;
	ibMemoryBuffer GetBinary(const char* name) const;   // none — no bytes
	// A sub-node — an empty one (IsEmpty) when there is none; a list's items.
	ibProtocolNode              FindChild(const char* name) const;
	std::vector<ibProtocolNode> GetList(const char* name) const;
	std::vector<ibProtocolNode> GetList(const ibString& name) const;
	// All of its entries, by name as the wire has it — what SetValue takes back.
	std::vector<std::pair<ibString, ibProtocolNode>> Entries() const;
	// …and the engine's node it is — what the client's readers take as it lies (ibReadProtocolNode, ibProtocolPicture).
	// Null when it is none.
	const ibDataNode* GetBody() const;
	// The node itself as a value — a list's item, an entry.
	ibString       AsString() const;
	long long      AsInt() const;
	bool           AsBool() const;
	ibMemoryBuffer AsBinary() const;

	// — as the protocol writes a node —
	long long                   GetId() const;     // NodeId; 0 — none
	ibString                    GetType() const;   // NodeType — its type's name
	std::vector<ibProtocolNode> Children() const;  // NodeChildren

	// — filling —
	ibProtocolNode& SetValue(const char* name, const ibString& text);
	ibProtocolNode& SetValue(const char* name, long long number);
	ibProtocolNode& SetValue(const char* name, int number);
	ibProtocolNode& SetValue(const char* name, const ibMemoryBuffer& data);
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
	// client-protocol.md, The frame) — the same a browser applies (framePatch.cpp). The frame is made anew of the two —
	// what the patch leaves is copied as it lies — and this handle is to it.
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

	// WHAT A HANDLE IS TO.
	enum class ibShape : unsigned char {
		Empty,      // nothing — a name the node does not have
		Node,       // a node: m_node, a handle into its tree being made; else m_read, read out where it lies
		Value,      // a text, a number, a flag, a list: m_value
		Children,   // a node's children as a list (NodeChildren, an entry of Entries): m_node or m_read
	};

	ibProtocolNode(std::shared_ptr<ibProtocolTree> tree, ibShape shape);

	// The node it is, to read — the handle's, or the one read out.
	const ibDataNode& Held() const;
	// …and to write: a tree being made writes where the handle is; a node read out becomes a tree of its own first.
	// Null — it is no node.
	ibDataNode* Writable();
	// A part read out of this node — a handle where it lies in a tree being made, else the node as it was read; and a
	// value of it — a node is a part, anything else a value.
	ibProtocolNode Part(const ibDataNode& part) const;
	ibProtocolNode Item(const ibDataValue& item) const;
	// A list's items, as handles of this tree.
	std::vector<ibProtocolNode> Items(const ibDataValue& list) const;
	// An entry set — by the JSON view's names: NodeType the type, NodeId the id, NodeChildren the children.
	void Set(const ibString& name, const ibDataValue& value);
	ibProtocolNode& SetFlag(const char* name, bool flag);

	std::shared_ptr<ibProtocolTree> m_tree;
	ibShape                         m_shape = ibShape::Empty;
	ibDataNode*                     m_node = nullptr;   // a node of a tree being made, or a tree's root
	ibDataNode                      m_read;             // …else a node read out where it lies
	ibDataValue                     m_value;
};

// THE TREE A HANDLE IS INTO — its root, and the names of its types: a packet's `types` (ibProtocol::ibRpc::Types), a
// field per clsid — its digits the name, the type's name the value.
struct ibProtocolTree {
	// The names of those types taken in beside its own — its own copy of them, not the packet they lie in.
	void NameTypes(const ibDataNode& types);

	ibDataNode m_root;
	ibDataNode m_types;
	// Made here — a new node, a Clone, a text read — its handles write where they are; else read from a packet, read
	// where it lies.
	bool       m_made = true;
};

#endif
