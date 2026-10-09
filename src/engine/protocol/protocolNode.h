#ifndef __PROTOCOL_NODE_H__
#define __PROTOCOL_NODE_H__

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>
#include <wx/string.h>

#include "protocol/protocolApi.h"

// A NODE OF THE WIRE — what the protocol carries, as this client reads and fills it: a node's entries by name (the
// server's fields and properties in one set, as the wire has them), its children (`NodeChildren`), its id (`NodeId`)
// and its type's NAME (`NodeType`). The values are what a thin client meets: text, whole numbers, flags, lists and
// nodes — no decimals, no rich strings: a value travels as text (`Text`, `Value.v`) and the server formats it.
//
// The verbs are the server's ibDataNode's; the ownership is not. It is a HANDLE: a node made here owns its tree, and
// a node read from it shares that tree — cheap to pass, alive while any handle to it is. A handle into the
// communicator's frame is read between calls: the next answer changes the frame under it.
class PROTOCOL_API ibProtocolNode {
public:

	// A new node with no entries — to fill (a call's parameters).
	ibProtocolNode();

	// The node a JSON text is — false when the text is not JSON; and the text a node is.
	static bool Read(const std::string& text, ibProtocolNode& node);
	std::string Write() const;

	bool IsEmpty() const;   // asked for a name the node does not have — or a null
	bool IsNode() const;    // a node with entries
	bool IsList() const;
	// …and as a value: text, a whole number, a flag.
	bool IsString() const;
	bool IsInt() const;
	bool IsBool() const;

	// — its entries, by name: `otherwise` when the node does not have one, or has it of another kind —
	bool      Has(const char* name) const;
	wxString  GetString(const char* name, const wxString& otherwise = wxString()) const;
	long long GetInt(const char* name, long long otherwise = 0) const;
	bool      GetBool(const char* name, bool otherwise = false) const;
	// A sub-node — an empty one (IsEmpty) when there is none; a list's items.
	ibProtocolNode              FindChild(const char* name) const;
	std::vector<ibProtocolNode> GetList(const char* name) const;
	// All of its entries, by name — a node read whole (ibDataValue::Read).
	std::vector<std::pair<wxString, ibProtocolNode>> Entries() const;
	// The node itself as a value — a list's item.
	wxString  AsString() const;
	long long AsInt() const;
	bool      AsBool() const;

	// — as the protocol writes a node —
	long long                   GetId() const;     // NodeId; 0 — none
	wxString                    GetType() const;   // NodeType — its type's name
	std::vector<ibProtocolNode> Children() const;  // NodeChildren

	// — filling —
	ibProtocolNode& SetValue(const char* name, const wxString& text);
	ibProtocolNode& SetValue(const char* name, const char* text);
	ibProtocolNode& SetValue(const char* name, long long number);
	ibProtocolNode& SetValue(const char* name, int number);
	ibProtocolNode& SetValue(const char* name, bool flag);
	ibProtocolNode& SetValue(const char* name, const ibProtocolNode& node);   // a copy of it
	// The sub-node under the name — made when there is none — to fill.
	ibProtocolNode  Child(const char* name);
	// A child at the end of NodeChildren, to fill.
	ibProtocolNode  AddChild();
	// An item at the end of the list under the name — made when there is none: a node to fill, or a number.
	ibProtocolNode  AddItem(const char* name);
	void            AddItem(const char* name, long long number);
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

	ibProtocolNode(std::shared_ptr<nlohmann::json> root, nlohmann::json* node);
	// Owning the tree, moved in.
	explicit ibProtocolNode(nlohmann::json&& tree);

	nlohmann::json*       Json() { return m_node; }
	const nlohmann::json* Json() const { return m_node; }

	std::shared_ptr<nlohmann::json> m_root;
	nlohmann::json*                 m_node = nullptr;
};

#endif
