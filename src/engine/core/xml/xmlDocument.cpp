#include "core/xml/xmlDocument.h"

#include <algorithm>   // std::min
#include <string>
#include <vector>

// One attribute: a node links its own, the first written first - most nodes have none, and pay nothing for it.
struct ibXmlNode::ibAttribute {
	ibString     m_name, m_value;
	ibAttribute* m_next = nullptr;
};

namespace {

// ⭐ KEPT IN BLOCKS: a node never moves once made, and one allocation serves many - a std::deque of a type this size
// allocates one block per element under MSVC. A block is twice the one before, from 16 to 1024: a text of one element
// - a value read back, a workbook's small part - is not made to pay for a thousand.
template <typename T>
class ibBlockArray {
public:
	T& Make()
	{
		if (m_used == m_size) {
			const size_t size = m_size == 0 ? kFirst : std::min(m_size * 2, kLast);
			m_blockArray.push_back(std::make_unique<T[]>(size));   // what may raise, before the counts change
			m_size = size;
			m_used = 0;
		}
		return m_blockArray.back()[m_used++];
	}

private:
	static constexpr size_t kFirst = 16, kLast = 1024;
	std::vector<std::unique_ptr<T[]>> m_blockArray;
	size_t m_size = 0;   // of the last block
	size_t m_used = 0;   // ...and taken of it
};

} // namespace

struct ibXmlDocument::ibStore {
	ibBlockArray<ibXmlNode>              m_nodeArray;
	ibBlockArray<ibXmlNode::ibAttribute> m_attributeArray;
};

ibString ibXmlNode::GetNodeContent() const
{
	for (const ibXmlNode* child = m_children; child != nullptr; child = child->m_next)
		if (child->m_type == ibXmlNodeType_Text || child->m_type == ibXmlNodeType_Whitespace
			|| child->m_type == ibXmlNodeType_CDATASection)
			return child->m_content;
	return ibString();
}

const ibXmlNode::ibAttribute* ibXmlNode::FindAttribute(const ibString& name) const
{
	for (const ibAttribute* attribute = m_firstAttribute; attribute != nullptr; attribute = attribute->m_next)
		if (attribute->m_name == name)
			return attribute;
	return nullptr;
}

bool ibXmlNode::HasAttribute(const ibString& name) const
{
	return FindAttribute(name) != nullptr;
}

ibString ibXmlNode::GetAttribute(const ibString& name, const ibString& absent) const
{
	const ibAttribute* attribute = FindAttribute(name);
	return attribute != nullptr ? attribute->m_value : absent;
}

ibXmlDocument::ibXmlDocument() = default;
ibXmlDocument::~ibXmlDocument() = default;

ibXmlNode& ibXmlDocument::Append(ibXmlNode* parent, ibXmlNodeType type)
{
	ibXmlNode& node = m_store->m_nodeArray.Make();
	node.m_type = type;
	node.m_parent = parent;
	if (parent != nullptr) {
		if (parent->m_lastChild != nullptr)
			parent->m_lastChild->m_next = &node;
		else
			parent->m_children = &node;
		parent->m_lastChild = &node;
	}
	return node;
}

// ⭐ THE NAMES AS WRITTEN (ibXmlParser::ibReceiver): the tree keeps a name and nothing else of it, and a parser that
// resolved namespaces made a URI, a local part and a prefix for every element and attribute — a workbook's cell paid
// for its namespace's URI twice over, at its start and at its end.
void ibXmlDocument::Load(const ibString& text, const ibString& source)
{
	const std::string utf8 = text.ToUtf8();
	Load(ibXmlParser::kCharacters, utf8.data(), utf8.size(), source);
}

void ibXmlDocument::Load(const char* data, size_t size, const ibString& source)
{
	Load(ibXmlParser::kBytes, data, size, source);
}

// ⭐ THE PARSER HANDS EACH EVENT STRAIGHT TO THE TREE (ibXmlParser::ibReceiver): an element opens a level, its end
// closes it, and no node of the parser's is made in between - in a checked build that node, with its strings, its
// attribute list and its scope, cost more than expat's own reading of the text. What stands outside the root (a comment
// before it, the whitespace after it) is not kept.
void ibXmlDocument::Load(ibXmlParser::ibInput input, const char* data, size_t size, const ibString& source)
{
	m_root = nullptr;
	m_store = std::make_unique<ibStore>();

	struct Building : ibXmlParser::ibReceiver {
		ibXmlDocument&          m_document;
		std::vector<ibXmlNode*> m_openArray;   // the elements not ended, innermost last

		explicit Building(ibXmlDocument& document) : m_document(document) {}

		virtual void Start(const char* name, const char* const* attributes, unsigned long long line) override
		{
			ibXmlNode* const parent = m_openArray.empty() ? nullptr : m_openArray.back();
			ibXmlNode& element = m_document.Append(parent, ibXmlNodeType_StartElement);
			element.m_name = ibString::FromUTF8(name);
			element.m_line = line;
			ibXmlNode::ibAttribute** last = &element.m_firstAttribute;
			for (const char* const* at = attributes; at[0] != nullptr; at += 2) {
				ibXmlNode::ibAttribute& attribute = m_document.m_store->m_attributeArray.Make();
				attribute.m_name = ibString::FromUTF8(at[0]);
				attribute.m_value = ibString::FromUTF8(at[1]);
				*last = &attribute;
				last = &attribute.m_next;
			}
			if (parent == nullptr)
				m_document.m_root = &element;   // expat refuses a second root before it gets here
			m_openArray.push_back(&element);
		}

		virtual void End() override
		{
			if (!m_openArray.empty())
				m_openArray.pop_back();
		}

		virtual void Content(ibXmlNodeType type, const char* name, const char* text, size_t size, unsigned long long line) override
		{
			if (m_openArray.empty())
				return;
			ibXmlNode& node = m_document.Append(m_openArray.back(), type);
			if (name != nullptr)
				node.m_name = ibString::FromUTF8(name);
			node.m_content = ibString::FromUTF8(text, size);
			node.m_line = line;
		}
	} building(*this);

	try {
		ibXmlParser parser(ibT("XML"), input, building);
		size_t at = 0;
		do {
			const size_t chunk = std::min(ibXmlParser::kChunk, size - at);
			parser.Feed(data + at, chunk, at + chunk == size, source);
			at += chunk;
		} while (at < size);
	}
	catch (...) {
		m_root = nullptr;
		m_store.reset();
		throw;
	}
}
