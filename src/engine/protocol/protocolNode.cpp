#include "protocolNode.h"
#include "protocol/protocol.h"   // ibProtocol::ibNode, ibProtocol::ibRpc::Types

#include <cstdio>
#include <cstring>

#include "core/exception.h"                // ibCoreException — bytes or a text that are no node
#include "core/serialize/provider/jsonProvider.h"   // the JSON view — a node's text, and the text of a value that is not one

namespace {

bool IsWord(const char* name, const char* word) { return std::strcmp(name, word) == 0; }
bool IsWord(const ibString& name, const char* word) { return name == word; }

// A clsid's digits — the name of its field among the types.
struct ibDigits {
	explicit ibDigits(ibClassID clsid) { std::snprintf(m_text, sizeof(m_text), "%llu", static_cast<unsigned long long>(clsid)); }
	char m_text[24];
};

// The name the types give a clsid — empty when they give none.
ibString NameOf(const ibDataNode& types, ibClassID clsid)
{
	const auto named = types.FindField(ibDigits(clsid).m_text);
	return named && named->Kind() == ibDataKind::String ? named->AsString() : ibString();
}

// …and the clsid of a name — 0 when they have none of it.
ibClassID ClsidOf(const ibDataNode& types, const ibString& name)
{
	for (const auto& field : types.Fields()) {
		unsigned long long clsid = 0;
		if (field.second.Kind() == ibDataKind::String && field.second.AsString() == name && field.first.ToULongLong(&clsid))
			return static_cast<ibClassID>(clsid);
	}
	return ibClassID(0);
}

// A value as the JSON view writes it — the text of one that is no text: a number, a flag, a date.
ibString JsonOf(const ibDataValue& value)
{
	static const ibString kName(ibT("v"));
	ibDataNode holder;
	holder.AddField(kName, value);
	ibJsonProvider provider;
	provider.SetCompact(true);
	ibString text;
	provider.Write(holder, text);
	// {"v":…}
	return text.length() > 6 ? text.Mid(5, text.length() - 6) : ibString();
}

ibString StringOf(const std::optional<ibDataValue>& value, const ibString& otherwise)
{
	if (!value)
		return otherwise;
	switch (value->Kind()) {
	case ibDataKind::String:
		return value->AsString();
	case ibDataKind::Number:
	case ibDataKind::Bool:
	case ibDataKind::Date:
	case ibDataKind::Guid: {
		// …where text was asked for — as the wire's JSON writes it; a date and a guid are their strings. Bytes are no
		// text: GetBinary.
		const ibString text = JsonOf(*value);
		return text.length() >= 2 && text.GetChar(0) == ibT('"') ? text.Mid(1, text.length() - 2) : text;
	}
	default:
		return otherwise;
	}
}

long long IntOf(const std::optional<ibDataValue>& value, long long otherwise)
{
	return value && value->Kind() == ibDataKind::Number ? static_cast<long long>(value->AsInt()) : otherwise;
}

bool BoolOf(const std::optional<ibDataValue>& value, bool otherwise)
{
	return value && value->Kind() == ibDataKind::Bool ? value->AsBool() : otherwise;
}

ibMemoryBuffer BinaryOf(const std::optional<ibDataValue>& value)
{
	return value && value->Kind() == ibDataKind::Binary ? value->AsBinary() : ibMemoryBuffer();
}

// A node as a packet of its own — a root is one already, the same bytes; one read out of another's is copied out of
// them. What a tree being made holds at its root, from the start: a walk over its parts (Children, Entries) reads them
// all, then asks a handle for each (ibDataNode::Handle) — copied out as the first handle was given, the root moved
// under the parts read with it, and the next handles fell into the bytes it had left (2026-10-26, the designer's tree).
ibDataNode PacketOf(const ibDataNode& node)
{
	ibMemoryBuffer packet;
	ibBinaryProvider().Write(node, packet);
	ibDataNode root;
	ibBinaryProvider().Read(packet, root);
	return root;
}

// TWO NODES ALIKE — the same type, id and raw block, the same entries in whatever order, the same children in theirs.
bool Alike(const ibDataNode& one, const ibDataNode& other);

bool Alike(const ibDataValue& one, const ibDataValue& other)
{
	if (one.Kind() != other.Kind())
		return false;
	if (one.Kind() == ibDataKind::Child)
		return Alike(*one.AsChild(), *other.AsChild());
	if (one.Kind() == ibDataKind::Array) {
		const auto items = one.AsArray(), others = other.AsArray();
		if (items.size() != others.size())
			return false;
		auto at = others.begin();
		for (const ibDataValue& item : items) {
			if (!Alike(item, *at))
				return false;
			++at;
		}
		return true;
	}
	return one == other;
}

bool Alike(const ibDataNode& one, const ibDataNode& other)
{
	if (one == other)
		return true;   // the same bytes — the common case, not walked
	if (one.GetClsid() != other.GetClsid() || one.GetMetaId() != other.GetMetaId())
		return false;
	const ibMemoryBuffer raw = one.RawData(), otherRaw = other.RawData();
	if (raw.GetDataLen() != otherRaw.GetDataLen()
		|| (raw.GetDataLen() != 0 && std::memcmp(raw.GetData(), otherRaw.GetData(), raw.GetDataLen()) != 0))
		return false;
	const auto fields = one.Fields();
	if (fields.size() != other.Fields().size())
		return false;
	for (const auto& field : fields) {
		const auto found = other.FindField(field.first);
		if (!found || !Alike(field.second, *found))
			return false;
	}
	const auto children = one.Children(), others = other.Children();
	if (children.size() != others.size())
		return false;
	auto at = others.begin();
	for (const ibDataNode& child : children) {
		if (!Alike(child, *at))
			return false;
		++at;
	}
	return true;
}

} // namespace

void ibProtocolTree::NameTypes(const ibDataNode& types)
{
	for (const auto& type : types.Fields()) {
		if (!m_types.FindField(type.first))
			m_types.AddField(type.first, type.second);
	}
}

ibProtocolNode::ibProtocolNode()
	: ibProtocolNode(std::make_shared<ibProtocolTree>(), ibShape::Node)
{
}

ibProtocolNode::ibProtocolNode(const ibDataNode& node)
	: ibProtocolNode(std::make_shared<ibProtocolTree>(), ibShape::Node)
{
	m_tree->m_root = node;
	m_tree->m_made = false;
}

ibProtocolNode::ibProtocolNode(std::shared_ptr<ibProtocolTree> tree, ibShape shape)
	: m_tree(std::move(tree)), m_shape(shape)
{
	// A tree's root is its own node, written where it is — a tree read is written at its root too.
	if (m_shape == ibShape::Node && m_tree)
		m_node = &m_tree->m_root;
}

bool ibProtocolNode::Read(const ibMemoryBuffer& packet, ibProtocolNode& node)
{
	auto tree = std::make_shared<ibProtocolTree>();
	try {
		ibBinaryProvider().Read(packet, tree->m_root);
	}
	catch (const ibCoreException&) {
		return false;
	}
	if (const auto types = tree->m_root.FindChild(ibProtocol::ibRpc::Types))
		tree->m_types = *types;
	tree->m_made = false;
	node = ibProtocolNode(std::move(tree), ibShape::Node);
	return true;
}

void ibProtocolNode::Write(ibMemoryBuffer& packet) const
{
	ibBinaryProvider().Write(m_shape == ibShape::Node ? Held() : ibDataNode(), packet);
}

bool ibProtocolNode::Read(const ibString& text, ibProtocolNode& node)
{
	// A type's name stays its word (NodeType), for the server to look up — the client knows none by name.
	auto tree = std::make_shared<ibProtocolTree>();
	try {
		if (!ibJsonProvider().Read(text, tree->m_root))
			return false;
	}
	catch (const ibCoreException&) {
		return false;
	}
	node = ibProtocolNode(std::move(tree), ibShape::Node);
	return true;
}

ibString ibProtocolNode::Write() const
{
	ibJsonProvider provider;
	provider.SetCompact(true);
	const std::shared_ptr<ibProtocolTree> tree = m_tree;
	provider.SetTypeResolver([tree](ibClassID clsid) { return tree ? NameOf(tree->m_types, clsid) : ibString(); });

	ibString text;
	switch (m_shape) {
	case ibShape::Node:
		provider.Write(Held(), text);
		return text;
	case ibShape::Value:
		return JsonOf(m_value);
	case ibShape::Children: {
		std::vector<ibDataValue> children;
		for (const ibDataNode& child : Held().Children())
			children.push_back(ibDataValue::Child(std::make_shared<ibDataNode>(child)));
		return JsonOf(ibDataValue::Array(std::move(children)));
	}
	default:
		return ibString(ibT("null"));
	}
}

bool ibProtocolNode::IsEmpty() const  { return m_shape == ibShape::Empty || (m_shape == ibShape::Value && m_value.IsEmpty()); }
bool ibProtocolNode::IsNode() const   { return m_shape == ibShape::Node; }
bool ibProtocolNode::IsList() const   { return m_shape == ibShape::Children || (m_shape == ibShape::Value && m_value.Kind() == ibDataKind::Array); }
bool ibProtocolNode::IsString() const { return m_shape == ibShape::Value && m_value.Kind() == ibDataKind::String; }
bool ibProtocolNode::IsInt() const    { return m_shape == ibShape::Value && m_value.Kind() == ibDataKind::Number; }
bool ibProtocolNode::IsBool() const   { return m_shape == ibShape::Value && m_value.Kind() == ibDataKind::Bool; }
bool ibProtocolNode::IsBinary() const { return m_shape == ibShape::Value && m_value.Kind() == ibDataKind::Binary; }

bool ibProtocolNode::Has(const char* name) const
{
	if (m_shape != ibShape::Node)
		return false;
	const ibDataNode& node = Held();
	if (IsWord(name, ibProtocol::ibNode::Type) && node.GetClsid() != ibClassID(0))
		return true;
	if (IsWord(name, ibProtocol::ibNode::Id) && node.GetMetaId() != 0)
		return true;
	if (IsWord(name, ibProtocol::ibNode::Children))
		return !node.Children().empty();
	return node.Has(name);
}

ibString ibProtocolNode::GetString(const char* name, const ibString& otherwise) const
{
	if (m_shape != ibShape::Node)
		return otherwise;
	if (IsWord(name, ibProtocol::ibNode::Type) && Held().GetClsid() != ibClassID(0))
		return GetType();
	if (IsWord(name, ibProtocol::ibNode::Id) && Held().GetMetaId() != 0)
		return JsonOf(ibDataValue::Int(Held().GetMetaId()));
	return StringOf(Held().FindField(name), otherwise);
}

long long ibProtocolNode::GetInt(const char* name, long long otherwise) const
{
	if (m_shape != ibShape::Node)
		return otherwise;
	if (IsWord(name, ibProtocol::ibNode::Id))
		return Held().GetMetaId() != 0 ? Held().GetMetaId() : otherwise;
	return IntOf(Held().FindField(name), otherwise);
}

bool ibProtocolNode::GetBool(const char* name, bool otherwise) const
{
	return m_shape == ibShape::Node ? BoolOf(Held().FindField(name), otherwise) : otherwise;
}

ibMemoryBuffer ibProtocolNode::GetBinary(const char* name) const
{
	return m_shape == ibShape::Node ? BinaryOf(Held().FindField(name)) : ibMemoryBuffer();
}

ibProtocolNode ibProtocolNode::FindChild(const char* name) const
{
	if (m_shape != ibShape::Node)
		return ibProtocolNode(m_tree, ibShape::Empty);
	if (IsWord(name, ibProtocol::ibNode::Children)) {
		if (Held().Children().empty())
			return ibProtocolNode(m_tree, ibShape::Empty);
		ibProtocolNode children = *this;
		children.m_shape = ibShape::Children;
		return children;
	}
	const auto found = Held().FindField(name);
	return found ? Item(*found) : ibProtocolNode(m_tree, ibShape::Empty);
}

std::vector<ibProtocolNode> ibProtocolNode::GetList(const char* name) const
{
	if (m_shape != ibShape::Node)
		return {};
	if (IsWord(name, ibProtocol::ibNode::Children))
		return Children();
	const auto found = Held().FindField(name);
	return found ? Items(*found) : std::vector<ibProtocolNode>();
}

std::vector<ibProtocolNode> ibProtocolNode::GetList(const ibString& name) const
{
	if (m_shape != ibShape::Node)
		return {};
	if (IsWord(name, ibProtocol::ibNode::Children))
		return Children();
	const auto found = Held().FindField(name);
	return found ? Items(*found) : std::vector<ibProtocolNode>();
}

std::vector<std::pair<ibString, ibProtocolNode>> ibProtocolNode::Entries() const
{
	std::vector<std::pair<ibString, ibProtocolNode>> entries;
	if (m_shape != ibShape::Node)
		return entries;
	const ibDataNode& node = Held();

	// As the JSON view has them: its type and id first, its fields, its children last. A type is its name where the
	// tree names it, else its number — either is what SetValue takes back.
	const auto value = [this](const ibDataValue& said) {
		ibProtocolNode entry(m_tree, ibShape::Value);
		entry.m_value = said;
		return entry;
	};
	if (node.GetClsid() != ibClassID(0)) {
		const ibString name = NameOf(m_tree->m_types, node.GetClsid());
		entries.emplace_back(ibProtocol::ibNode::Type,
			value(name.IsEmpty() ? ibDataValue::UInt(node.GetClsid()) : ibDataValue::String(name)));
	}
	if (node.GetMetaId() != 0)
		entries.emplace_back(ibProtocol::ibNode::Id, value(ibDataValue::Int(node.GetMetaId())));
	for (const auto& field : node.Fields())
		entries.emplace_back(field.first, Item(field.second));
	if (node.HasRawData())
		entries.emplace_back(ibProtocol::ibNode::Raw, value(ibDataValue::Binary(node.RawData())));
	if (!node.Children().empty()) {
		ibProtocolNode children = *this;
		children.m_shape = ibShape::Children;
		entries.emplace_back(ibProtocol::ibNode::Children, std::move(children));
	}
	return entries;
}

const ibDataNode* ibProtocolNode::GetBody() const
{
	return m_shape == ibShape::Node ? &Held() : nullptr;
}

ibString       ibProtocolNode::AsString() const { return m_shape == ibShape::Value ? StringOf(m_value, ibString()) : ibString(); }
long long      ibProtocolNode::AsInt() const    { return m_shape == ibShape::Value ? IntOf(m_value, 0) : 0; }
bool           ibProtocolNode::AsBool() const   { return m_shape == ibShape::Value ? BoolOf(m_value, false) : false; }
ibMemoryBuffer ibProtocolNode::AsBinary() const { return m_shape == ibShape::Value ? BinaryOf(m_value) : ibMemoryBuffer(); }

long long ibProtocolNode::GetId() const
{
	return m_shape == ibShape::Node ? Held().GetMetaId() : 0;
}

ibString ibProtocolNode::GetType() const
{
	if (m_shape != ibShape::Node)
		return ibString();
	// Its clsid by the name the tree gives it, else by its number — as the JSON view writes it; none: the word kept.
	const ibClassID clsid = Held().GetClsid();
	if (clsid != ibClassID(0)) {
		const ibString name = NameOf(m_tree->m_types, clsid);
		return !name.IsEmpty() ? name : ibString(ibDigits(clsid).m_text);
	}
	return StringOf(Held().FindField(ibProtocol::ibNode::Type), ibString());
}

std::vector<ibProtocolNode> ibProtocolNode::Children() const
{
	std::vector<ibProtocolNode> children;
	if (m_shape != ibShape::Node && m_shape != ibShape::Children)
		return children;
	const auto range = Held().Children();
	children.reserve(range.size());
	for (const ibDataNode& child : range)
		children.push_back(Part(child));
	return children;
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const ibString& text)
{
	Set(name, ibDataValue::String(text));
	return *this;
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, long long number)
{
	Set(name, ibDataValue::Int(number));
	return *this;
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, int number)
{
	return SetValue(name, static_cast<long long>(number));
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const ibMemoryBuffer& data)
{
	Set(name, ibDataValue::Binary(data));
	return *this;
}

ibProtocolNode& ibProtocolNode::SetFlag(const char* name, bool flag)
{
	Set(name, ibDataValue::Bool(flag));
	return *this;
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const ibProtocolNode& node)
{
	return SetValue(ibString(name), node);
}

ibProtocolNode& ibProtocolNode::SetValue(const ibString& name, const ibProtocolNode& node)
{
	switch (node.m_shape) {
	case ibShape::Empty:
		Set(name, ibDataValue());
		break;
	case ibShape::Value:
		Set(name, node.m_value);
		break;
	case ibShape::Children:
	case ibShape::Node: {
		// Held first: it may lie in this very tree, and the bytes a write moves.
		const ibDataNode source = node.Held();
		ibDataNode* const target = Writable();
		if (target == nullptr)
			break;
		if (node.m_shape == ibShape::Children || IsWord(name, ibProtocol::ibNode::Children)) {
			// The children given anew.
			target->RemoveChildren();
			for (const ibDataNode& child : source.Children())
				target->AddChild(child);
		}
		else {
			// A node under the name in place of what was there — its bytes as they are.
			if (const auto was = target->FindField(name); was && was->Kind() != ibDataKind::Child)
				target->RemoveField(name);
			target->Child(name) = source;
		}
		// …and the names of its types with it, where it comes from another tree.
		if (node.m_tree && node.m_tree != m_tree)
			m_tree->NameTypes(node.m_tree->m_types);
		break;
	}
	}
	return *this;
}

ibProtocolNode ibProtocolNode::Child(const char* name)
{
	ibDataNode* const node = Writable();
	if (node == nullptr)
		return ibProtocolNode(m_tree, ibShape::Empty);
	if (const auto was = node->FindField(name); was && was->Kind() != ibDataKind::Child)
		node->RemoveField(name);
	ibProtocolNode child(m_tree, ibShape::Node);
	child.m_node = &node->Child(name);
	return child;
}

ibProtocolNode ibProtocolNode::AddChild()
{
	ibDataNode* const node = Writable();
	if (node == nullptr)
		return ibProtocolNode(m_tree, ibShape::Empty);
	ibProtocolNode child(m_tree, ibShape::Node);
	child.m_node = &node->AddChild(0, 0);
	return child;
}

ibProtocolNode ibProtocolNode::AddItem(const char* name)
{
	if (IsWord(name, ibProtocol::ibNode::Children))
		return AddChild();
	ibDataNode* const node = Writable();
	if (node == nullptr)
		return ibProtocolNode(m_tree, ibShape::Empty);
	if (const auto was = node->FindField(name); was && was->Kind() != ibDataKind::Array)
		node->RemoveField(name);
	ibProtocolNode item(m_tree, ibShape::Node);
	item.m_node = &node->AddItem(name);
	return item;
}

void ibProtocolNode::AddItem(const char* name, long long number)
{
	ibDataNode* const node = Writable();
	if (node == nullptr)
		return;
	if (const auto was = node->FindField(name); was && was->Kind() != ibDataKind::Array)
		node->RemoveField(name);
	node->AddItem(name, ibDataValue::Int(number));
}

void ibProtocolNode::AddItem(const char* name, const ibString& text)
{
	ibDataNode* const node = Writable();
	if (node == nullptr)
		return;
	if (const auto was = node->FindField(name); was && was->Kind() != ibDataKind::Array)
		node->RemoveField(name);
	node->AddItem(name, ibDataValue::String(text));
}

void ibProtocolNode::Remove(const char* name)
{
	ibDataNode* const node = Writable();
	if (node == nullptr)
		return;
	if (IsWord(name, ibProtocol::ibNode::Children))
		node->RemoveChildren();
	else if (IsWord(name, ibProtocol::ibNode::Id))
		node->SetMetaId(0);
	else {
		if (IsWord(name, ibProtocol::ibNode::Type))
			node->SetClsid(0);
		node->RemoveField(name);
	}
}

ibProtocolNode ibProtocolNode::Clone() const
{
	auto tree = std::make_shared<ibProtocolTree>();
	if (m_tree)
		tree->NameTypes(m_tree->m_types);
	// A node a packet of its own (PacketOf) — the same bytes when it is one; a value is only read.
	switch (m_shape) {
	case ibShape::Node:
	case ibShape::Children: {
		tree->m_root = PacketOf(Held());
		ibProtocolNode copy(std::move(tree), ibShape::Node);
		copy.m_shape = m_shape;
		return copy;
	}
	case ibShape::Value: {
		ibProtocolNode copy(std::move(tree), ibShape::Value);
		copy.m_value = m_value;
		return copy;
	}
	default:
		return ibProtocolNode(std::move(tree), ibShape::Empty);
	}
}

bool ibProtocolNode::operator==(const ibProtocolNode& other) const
{
	if (IsEmpty() || other.IsEmpty())
		return IsEmpty() && other.IsEmpty();
	if (m_shape != other.m_shape)
		return false;
	if (m_shape == ibShape::Value)
		return Alike(m_value, other.m_value);
	if (m_shape == ibShape::Node)
		return Alike(Held(), other.Held());
	// Children: the same, in the same order.
	const auto children = Held().Children(), others = other.Held().Children();
	if (children.size() != others.size())
		return false;
	auto at = others.begin();
	for (const ibDataNode& child : children) {
		if (!Alike(child, *at))
			return false;
		++at;
	}
	return true;
}

const ibDataNode& ibProtocolNode::Held() const
{
	return m_node != nullptr ? *m_node : m_read;
}

ibDataNode* ibProtocolNode::Writable()
{
	if (m_shape != ibShape::Node)
		return nullptr;
	if (m_node == nullptr) {
		// A node read out of a tree read: a tree of its own now — its bytes copied out of the ones it was read from
		// (PacketOf), the names of its types with it.
		auto tree = std::make_shared<ibProtocolTree>();
		tree->m_root = PacketOf(m_read);
		tree->NameTypes(m_tree->m_types);
		m_tree = std::move(tree);
		m_node = &m_tree->m_root;
		m_read = ibDataNode();
	}
	return m_node;
}

ibProtocolNode ibProtocolNode::Part(const ibDataNode& part) const
{
	// In a tree being made, a handle where it lies — its changes are the tree's; else the node as it was read.
	ibProtocolNode node(m_tree, ibShape::Node);
	if (m_tree->m_made && m_node != nullptr)
		node.m_node = &m_node->Handle(part);
	else {
		node.m_node = nullptr;
		node.m_read = part;
	}
	return node;
}

ibProtocolNode ibProtocolNode::Item(const ibDataValue& item) const
{
	if (item.Kind() == ibDataKind::Child)
		return Part(*item.AsChild());
	ibProtocolNode value(m_tree, ibShape::Value);
	value.m_value = item;
	return value;
}

std::vector<ibProtocolNode> ibProtocolNode::Items(const ibDataValue& list) const
{
	std::vector<ibProtocolNode> items;
	if (list.Kind() != ibDataKind::Array)
		return items;
	const auto range = list.AsArray();
	items.reserve(range.size());
	for (const ibDataValue& item : range)
		items.push_back(Item(item));
	return items;
}

void ibProtocolNode::Set(const ibString& name, const ibDataValue& value)
{
	ibDataNode* const node = Writable();
	if (node == nullptr)
		return;

	// THE JSON VIEW'S NAMES, as its reader takes them (ibJsonProvider::ReadEntry): a type is the clsid — of a number,
	// or of a name the tree knows; a name it does not stays its word, for the server to look up.
	if (IsWord(name, ibProtocol::ibNode::Type)) {
		const ibClassID clsid = value.Kind() == ibDataKind::Number ? static_cast<ibClassID>(value.AsUInt())
			: value.Kind() == ibDataKind::String ? ClsidOf(m_tree->m_types, value.AsString()) : ibClassID(0);
		node->SetClsid(clsid);
		if (clsid != ibClassID(0) || value.Kind() != ibDataKind::String)
			node->RemoveField(name);
		else
			node->SetField(name, value);
		return;
	}
	if (IsWord(name, ibProtocol::ibNode::Id)) {
		node->SetMetaId(value.Kind() == ibDataKind::Number ? static_cast<ibMetaID>(value.AsInt()) : 0);
		return;
	}
	if (IsWord(name, ibProtocol::ibNode::Raw)) {
		node->SetRawData(value.Kind() == ibDataKind::Binary ? value.AsBinary() : ibMemoryBuffer());
		return;
	}
	if (IsWord(name, ibProtocol::ibNode::Children)) {
		node->RemoveChildren();
		if (value.Kind() == ibDataKind::Array) {
			for (const ibDataValue& item : value.AsArray()) {
				if (item.Kind() == ibDataKind::Child)
					node->AddChild(*item.AsChild());
			}
		}
		return;
	}
	node->SetField(name, value);
}
