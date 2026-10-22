#include "protocolNode.h"
#include "protocolValue.h"
#include "protocol/protocol.h"   // ibProtocol::ibNode

namespace {

using ibKind = ibProtocolValue::ibKind;

// The entry under the name, when the node is one and has it.
template<class Name>
ibProtocolValue* Entry(ibProtocolBody* node, const Name& name)
{
	return node != nullptr && !node->m_list ? node->Find(name) : nullptr;
}

ibString StringOf(const ibProtocolValue* value, const ibString& otherwise)
{
	if (value == nullptr)
		return otherwise;
	switch (value->GetKind()) {
	case ibKind::Text:
	case ibKind::Number:
		return value->AsText();
	case ibKind::Int:
	case ibKind::Bool: {
		// A whole number or a flag where text was asked for — as the wire writes it.
		ibString text;
		value->Write(text);
		return text;
	}
	default:
		return otherwise;
	}
}

long long IntOf(const ibProtocolValue* value, long long otherwise)
{
	return value != nullptr && value->GetKind() == ibKind::Int ? value->AsInt() : otherwise;
}

bool BoolOf(const ibProtocolValue* value, bool otherwise)
{
	return value != nullptr && value->GetKind() == ibKind::Bool ? value->AsBool() : otherwise;
}

} // namespace

ibProtocolNode::ibProtocolNode()
	: ibProtocolNode(ibProtocolValue::Node())
{
}

ibProtocolNode::ibProtocolNode(std::shared_ptr<ibProtocolValue> root, ibProtocolValue* value)
	: m_root(std::move(root)), m_value(value), m_body(value != nullptr ? value->GetBody() : nullptr)
{
}

ibProtocolNode::ibProtocolNode(ibProtocolValue&& tree)
	: m_root(std::make_shared<ibProtocolValue>(std::move(tree)))
{
	m_value = m_root.get();
	m_body = m_value->GetBody();
}

bool ibProtocolNode::Read(const ibString& text, ibProtocolNode& node)
{
	ibProtocolValue tree;
	if (!ibProtocolValue::Read(text.wc_str(), text.length(), tree))
		return false;
	node = ibProtocolNode(std::move(tree));
	return true;
}

ibString ibProtocolNode::Write() const
{
	ibString text;
	if (m_body != nullptr)
		m_body->Write(text);
	else if (m_value != nullptr)
		m_value->Write(text);
	else
		ibProtocolValue().Write(text);
	return text;
}

bool ibProtocolNode::IsEmpty() const  { return m_body == nullptr && (m_value == nullptr || m_value->GetKind() == ibKind::Null); }
bool ibProtocolNode::IsNode() const   { return m_body != nullptr && !m_body->m_list; }
bool ibProtocolNode::IsList() const   { return m_body != nullptr && m_body->m_list; }
bool ibProtocolNode::IsString() const { return m_body == nullptr && m_value != nullptr && m_value->GetKind() == ibKind::Text; }
bool ibProtocolNode::IsInt() const    { return m_body == nullptr && m_value != nullptr && m_value->GetKind() == ibKind::Int; }
bool ibProtocolNode::IsBool() const   { return m_body == nullptr && m_value != nullptr && m_value->GetKind() == ibKind::Bool; }

bool ibProtocolNode::Has(const char* name) const
{
	return Entry(m_body, name) != nullptr;
}

ibString ibProtocolNode::GetString(const char* name, const ibString& otherwise) const
{
	return StringOf(Entry(m_body, name), otherwise);
}

long long ibProtocolNode::GetInt(const char* name, long long otherwise) const
{
	return IntOf(Entry(m_body, name), otherwise);
}

bool ibProtocolNode::GetBool(const char* name, bool otherwise) const
{
	return BoolOf(Entry(m_body, name), otherwise);
}

ibProtocolNode ibProtocolNode::FindChild(const char* name) const
{
	ibProtocolValue* const found = Entry(m_body, name);
	return found != nullptr ? ibProtocolNode(m_root, found) : ibProtocolNode(nullptr, nullptr);
}

namespace {

// The list under the name, when the node is one and has it.
template<class Name>
ibProtocolBody* ListIn(ibProtocolBody* node, const Name& name)
{
	const ibProtocolValue* const list = Entry(node, name);
	return list != nullptr ? list->GetList() : nullptr;
}

} // namespace

std::vector<ibProtocolNode> ibProtocolNode::GetList(const char* name) const
{
	return Items(ListIn(m_body, name));
}

std::vector<ibProtocolNode> ibProtocolNode::GetList(const ibString& name) const
{
	return Items(ListIn(m_body, name));
}

std::vector<std::pair<ibString, ibProtocolNode>> ibProtocolNode::Entries() const
{
	std::vector<std::pair<ibString, ibProtocolNode>> entries;
	if (IsNode()) {
		entries.reserve(m_body->m_entries.size());
		for (ibProtocolEntry& entry : m_body->m_entries)
			entries.emplace_back(entry.m_name, ibProtocolNode(m_root, &entry.m_value));
	}
	return entries;
}

const ibProtocolBody* ibProtocolNode::GetBody() const
{
	return m_body;
}

ibString  ibProtocolNode::AsString() const { return StringOf(m_body == nullptr ? m_value : nullptr, ibString()); }
long long ibProtocolNode::AsInt() const    { return IntOf(m_body == nullptr ? m_value : nullptr, 0); }
bool      ibProtocolNode::AsBool() const   { return BoolOf(m_body == nullptr ? m_value : nullptr, false); }

long long ibProtocolNode::GetId() const
{
	return GetInt(ibProtocol::ibNode::Id);
}

ibString ibProtocolNode::GetType() const
{
	return GetString(ibProtocol::ibNode::Type);
}

std::vector<ibProtocolNode> ibProtocolNode::Children() const
{
	return GetList(ibProtocol::ibNode::Children);
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const ibString& text)
{
	if (ibProtocolBody* const node = Node())
		node->Set(name) = ibProtocolValue(text);
	return *this;
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, long long number)
{
	if (ibProtocolBody* const node = Node())
		node->Set(name) = ibProtocolValue(number);
	return *this;
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, int number)
{
	return SetValue(name, static_cast<long long>(number));
}

ibProtocolNode& ibProtocolNode::SetFlag(const char* name, bool flag)
{
	if (ibProtocolBody* const node = Node())
		node->Set(name) = ibProtocolValue(flag);
	return *this;
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const ibProtocolNode& node)
{
	if (ibProtocolBody* const target = Node()) {
		ibProtocolValue copy = node.Copy();   // first: the node may lie in this one, and its place move
		target->Set(name) = std::move(copy);
	}
	return *this;
}

ibProtocolNode& ibProtocolNode::SetValue(const ibString& name, const ibProtocolNode& node)
{
	if (ibProtocolBody* const target = Node()) {
		ibProtocolValue copy = node.Copy();
		target->Set(name) = std::move(copy);
	}
	return *this;
}

ibProtocolNode ibProtocolNode::Child(const char* name)
{
	ibProtocolBody* const node = Node();
	if (node == nullptr)
		return ibProtocolNode(nullptr, nullptr);
	ibProtocolValue& child = node->Set(name);
	if (child.GetNode() == nullptr)
		child = ibProtocolValue::Node();
	return ibProtocolNode(m_root, &child);
}

ibProtocolNode ibProtocolNode::AddChild()
{
	return AddItem(ibProtocol::ibNode::Children);
}

ibProtocolNode ibProtocolNode::AddItem(const char* name)
{
	ibProtocolBody* const list = ListOf(name);
	if (list == nullptr)
		return ibProtocolNode(nullptr, nullptr);
	ibProtocolValue& item = list->Add();
	item = ibProtocolValue::Node();
	return ibProtocolNode(m_root, &item);
}

void ibProtocolNode::AddItem(const char* name, long long number)
{
	if (ibProtocolBody* const list = ListOf(name))
		list->Add() = ibProtocolValue(number);
}

void ibProtocolNode::AddItem(const char* name, const ibString& text)
{
	if (ibProtocolBody* const list = ListOf(name))
		list->Add() = ibProtocolValue(text);
}

void ibProtocolNode::Remove(const char* name)
{
	if (IsNode())
		m_body->Remove(name);
}

ibProtocolNode ibProtocolNode::Clone() const
{
	return m_body != nullptr || m_value != nullptr ? ibProtocolNode(Copy()) : ibProtocolNode(nullptr, nullptr);
}

bool ibProtocolNode::operator==(const ibProtocolNode& other) const
{
	// The same node, by two handles — alike, not walked.
	if (m_body != nullptr && m_body == other.m_body)
		return true;
	if (m_body != nullptr || other.m_body != nullptr)
		return m_body != nullptr && other.m_body != nullptr && *m_body == *other.m_body;
	if (m_value == nullptr || other.m_value == nullptr)
		return IsEmpty() && other.IsEmpty();
	return *m_value == *other.m_value;
}

std::vector<ibProtocolNode> ibProtocolNode::Items(ibProtocolBody* list) const
{
	std::vector<ibProtocolNode> items;
	if (list != nullptr) {
		items.reserve(list->m_entries.size());
		for (ibProtocolEntry& item : list->m_entries)
			items.push_back(ibProtocolNode(m_root, &item.m_value));
	}
	return items;
}

ibProtocolValue ibProtocolNode::Copy() const
{
	if (m_body != nullptr) {
		ibProtocolValue copy = m_body->m_list ? ibProtocolValue::List() : ibProtocolValue::Node();
		*copy.GetBody() = *m_body;
		return copy;
	}
	return m_value != nullptr ? *m_value : ibProtocolValue();
}

ibProtocolBody* ibProtocolNode::Node()
{
	if (m_body != nullptr)
		return m_body->m_list ? nullptr : m_body;
	if (m_value == nullptr)
		return nullptr;
	*m_value = ibProtocolValue::Node();
	m_body = m_value->GetBody();
	return m_body;
}

ibProtocolBody* ibProtocolNode::ListOf(const char* name)
{
	ibProtocolBody* const node = Node();
	if (node == nullptr)
		return nullptr;
	ibProtocolValue& items = node->Set(name);
	if (items.GetList() == nullptr)
		items = ibProtocolValue::List();
	return items.GetList();
}
