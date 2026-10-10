#include "protocolNode.h"
#include "protocol/protocol.h"   // ibProtocolName

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

// The entry under the name, when the node is one and has it.
const json* Entry(const json* node, const char* name)
{
	if (node == nullptr || !node->is_object())
		return nullptr;
	const auto found = node->find(name);
	return found != node->end() ? &*found : nullptr;
}

wxString StringOf(const json* value, const wxString& otherwise)
{
	if (value == nullptr)
		return otherwise;
	if (value->is_string())
		return wxString::FromUTF8(value->get_ref<const std::string&>());
	// A number or a flag where text was asked for — as the wire writes it.
	if (value->is_number() || value->is_boolean())
		return wxString::FromUTF8(value->dump());
	return otherwise;
}

long long IntOf(const json* value, long long otherwise)
{
	return value != nullptr && (value->is_number_integer() || value->is_number_unsigned())
		? value->get<long long>() : otherwise;
}

bool BoolOf(const json* value, bool otherwise)
{
	return value != nullptr && value->is_boolean() ? value->get<bool>() : otherwise;
}

} // namespace

ibProtocolNode::ibProtocolNode()
	: ibProtocolNode(json::object())
{
}

ibProtocolNode::ibProtocolNode(std::shared_ptr<nlohmann::json> root, nlohmann::json* node)
	: m_root(std::move(root)), m_node(node)
{
}

ibProtocolNode::ibProtocolNode(nlohmann::json&& tree)
	: m_root(std::make_shared<json>(std::move(tree)))
{
	m_node = m_root.get();
}

bool ibProtocolNode::Read(const std::string& text, ibProtocolNode& node)
{
	json tree = json::parse(text, nullptr, false);
	if (tree.is_discarded())
		return false;
	node = ibProtocolNode(std::move(tree));
	return true;
}

std::string ibProtocolNode::Write() const
{
	// A person's text that is not valid UTF-8 is replaced, never thrown about.
	return m_node != nullptr ? m_node->dump(-1, ' ', false, json::error_handler_t::replace) : std::string("null");
}

bool ibProtocolNode::IsEmpty() const { return m_node == nullptr || m_node->is_null(); }
bool ibProtocolNode::IsNode() const  { return m_node != nullptr && m_node->is_object(); }
bool ibProtocolNode::IsList() const  { return m_node != nullptr && m_node->is_array(); }
bool ibProtocolNode::IsString() const { return m_node != nullptr && m_node->is_string(); }
bool ibProtocolNode::IsInt() const    { return m_node != nullptr && (m_node->is_number_integer() || m_node->is_number_unsigned()); }
bool ibProtocolNode::IsBool() const   { return m_node != nullptr && m_node->is_boolean(); }

bool ibProtocolNode::Has(const char* name) const
{
	return Entry(m_node, name) != nullptr;
}

wxString ibProtocolNode::GetString(const char* name, const wxString& otherwise) const
{
	return StringOf(Entry(m_node, name), otherwise);
}

long long ibProtocolNode::GetInt(const char* name, long long otherwise) const
{
	return IntOf(Entry(m_node, name), otherwise);
}

bool ibProtocolNode::GetBool(const char* name, bool otherwise) const
{
	return BoolOf(Entry(m_node, name), otherwise);
}

ibProtocolNode ibProtocolNode::FindChild(const char* name) const
{
	const json* const found = Entry(m_node, name);
	return found != nullptr ? ibProtocolNode(m_root, const_cast<json*>(found)) : ibProtocolNode(nullptr, nullptr);
}

std::vector<ibProtocolNode> ibProtocolNode::GetList(const char* name) const
{
	std::vector<ibProtocolNode> items;
	const json* const list = Entry(m_node, name);
	if (list != nullptr && list->is_array()) {
		items.reserve(list->size());
		for (const json& item : *list)
			items.push_back(ibProtocolNode(m_root, const_cast<json*>(&item)));
	}
	return items;
}

std::vector<std::pair<wxString, ibProtocolNode>> ibProtocolNode::Entries() const
{
	std::vector<std::pair<wxString, ibProtocolNode>> entries;
	if (m_node != nullptr && m_node->is_object()) {
		entries.reserve(m_node->size());
		for (auto entry = m_node->begin(); entry != m_node->end(); ++entry)
			entries.emplace_back(wxString::FromUTF8(entry.key()), ibProtocolNode(m_root, &entry.value()));
	}
	return entries;
}

wxString  ibProtocolNode::AsString() const { return StringOf(m_node, wxString()); }
long long ibProtocolNode::AsInt() const    { return IntOf(m_node, 0); }
bool      ibProtocolNode::AsBool() const   { return BoolOf(m_node, false); }

long long ibProtocolNode::GetId() const
{
	return GetInt(ibProtocolName::NodeId);
}

wxString ibProtocolNode::GetType() const
{
	return GetString(ibProtocolName::NodeType);
}

std::vector<ibProtocolNode> ibProtocolNode::Children() const
{
	return GetList(ibProtocolName::NodeChildren);
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const wxString& text)
{
	return SetValue(name, ibProtocolNode(json(std::string(text.utf8_str()))));
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const char* text)
{
	return SetValue(name, ibProtocolNode(json(std::string(text != nullptr ? text : ""))));
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, long long number)
{
	return SetValue(name, ibProtocolNode(json(number)));
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, int number)
{
	return SetValue(name, static_cast<long long>(number));
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, bool flag)
{
	return SetValue(name, ibProtocolNode(json(flag)));
}

ibProtocolNode& ibProtocolNode::SetValue(const char* name, const ibProtocolNode& node)
{
	if (m_node == nullptr)
		return *this;
	if (!m_node->is_object())
		*m_node = json::object();
	(*m_node)[name] = node.m_node != nullptr ? *node.m_node : json();
	return *this;
}

ibProtocolNode ibProtocolNode::Child(const char* name)
{
	if (m_node == nullptr)
		return ibProtocolNode(nullptr, nullptr);
	if (!m_node->is_object())
		*m_node = json::object();
	json& child = (*m_node)[name];
	if (!child.is_object())
		child = json::object();
	return ibProtocolNode(m_root, &child);
}

ibProtocolNode ibProtocolNode::AddChild()
{
	if (m_node == nullptr)
		return ibProtocolNode(nullptr, nullptr);
	if (!m_node->is_object())
		*m_node = json::object();
	json& children = (*m_node)[ibProtocolName::NodeChildren];
	if (!children.is_array())
		children = json::array();
	children.push_back(json::object());
	return ibProtocolNode(m_root, &children.back());
}

ibProtocolNode ibProtocolNode::AddItem(const char* name)
{
	if (m_node == nullptr)
		return ibProtocolNode(nullptr, nullptr);
	if (!m_node->is_object())
		*m_node = json::object();
	json& items = (*m_node)[name];
	if (!items.is_array())
		items = json::array();
	items.push_back(json::object());
	return ibProtocolNode(m_root, &items.back());
}

void ibProtocolNode::AddItem(const char* name, long long number)
{
	if (m_node == nullptr)
		return;
	if (!m_node->is_object())
		*m_node = json::object();
	json& items = (*m_node)[name];
	if (!items.is_array())
		items = json::array();
	items.push_back(number);
}

void ibProtocolNode::AddItem(const char* name, const wxString& text)
{
	if (m_node == nullptr)
		return;
	if (!m_node->is_object())
		*m_node = json::object();
	json& items = (*m_node)[name];
	if (!items.is_array())
		items = json::array();
	items.push_back(std::string(text.utf8_str()));
}

void ibProtocolNode::Remove(const char* name)
{
	if (m_node != nullptr && m_node->is_object())
		m_node->erase(name);
}

ibProtocolNode ibProtocolNode::Clone() const
{
	return m_node != nullptr ? ibProtocolNode(json(*m_node)) : ibProtocolNode(nullptr, nullptr);
}

bool ibProtocolNode::operator==(const ibProtocolNode& other) const
{
	if (m_node == nullptr || other.m_node == nullptr)
		return IsEmpty() && other.IsEmpty();
	return *m_node == *other.m_node;
}
