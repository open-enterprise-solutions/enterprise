#include "core/xml/xmlDocument.h"

#include <algorithm>   // std::min
#include <string>
#include <utility>     // std::move

ibString ibXmlNode::GetNodeContent() const
{
	for (const ibXmlNode* child = m_children; child != nullptr; child = child->m_next)
		if (child->m_type == ibXmlNodeType_Text || child->m_type == ibXmlNodeType_Whitespace
			|| child->m_type == ibXmlNodeType_CDATASection)
			return child->m_content;
	return ibString();
}

const ibXmlParser::ibAttribute* ibXmlNode::FindAttribute(const ibString& name) const
{
	for (const ibXmlParser::ibAttribute& attribute : m_attributeArray)
		if (attribute.m_name == name)
			return &attribute;
	return nullptr;
}

bool ibXmlNode::HasAttribute(const ibString& name) const
{
	return FindAttribute(name) != nullptr;
}

ibString ibXmlNode::GetAttribute(const ibString& name, const ibString& absent) const
{
	const ibXmlParser::ibAttribute* attribute = FindAttribute(name);
	return attribute != nullptr ? attribute->m_value : absent;
}

ibXmlNode& ibXmlDocument::Append(ibXmlNode* parent, ibXmlNodeType type)
{
	ibXmlNode& node = m_nodeArray.emplace_back();
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

// ⭐ THE NAMES AS WRITTEN (ibXmlParser::kAsWritten): the tree keeps a name and nothing else of it, and a parser that
// resolved namespaces made a URI, a local part and a prefix for every element and attribute — a workbook's cell paid
// for its namespace's URI twice over, at its start and at its end.
void ibXmlDocument::Load(const ibString& text, const ibString& source)
{
	const std::string utf8 = text.ToUtf8();
	ibXmlParser parser(ibT("XML"), ibXmlParser::kCharacters, ibXmlParser::kAsWritten);
	Load(parser, utf8.data(), utf8.size(), source);
}

void ibXmlDocument::Load(const char* data, size_t size, const ibString& source)
{
	ibXmlParser parser(ibT("XML"), ibXmlParser::kBytes, ibXmlParser::kAsWritten);
	Load(parser, data, size, source);
}

// The parser hands its nodes over a queue at a time; the tree is built as they come - an element opens a level,
// its end closes it. What stands outside the root (a comment before it, the whitespace after it) is not kept.
void ibXmlDocument::Load(ibXmlParser& parser, const char* data, size_t size, const ibString& source)
{
	m_nodeArray.clear();
	m_root = nullptr;

	std::vector<ibXmlNode*> open;   // the elements not ended, innermost last

	const auto take = [&]() {
		std::deque<ibXmlParser::ibNode>& queue = parser.Queue();
		for (ibXmlParser::ibNode& parsed : queue) {
			ibXmlNode* const parent = open.empty() ? nullptr : open.back();
			switch (parsed.m_type) {
			case ibXmlNodeType_StartElement: {
				if (parent == nullptr && m_root != nullptr)
					break;   // expat refuses a second root before it gets here
				ibXmlNode& element = Append(parent, ibXmlNodeType_StartElement);
				element.m_name = std::move(parsed.m_name);
				element.m_line = parsed.m_line;
				element.m_attributeArray = std::move(parsed.m_attributeArray);
				if (parent == nullptr)
					m_root = &element;
				open.push_back(&element);
				break;
			}
			case ibXmlNodeType_EndElement:
				if (!open.empty())
					open.pop_back();
				break;
			case ibXmlNodeType_Text:
			case ibXmlNodeType_Whitespace:
			case ibXmlNodeType_CDATASection:
			case ibXmlNodeType_Comment:
			case ibXmlNodeType_ProcessingInstruction: {
				if (parent == nullptr)
					break;
				ibXmlNode& node = Append(parent, parsed.m_type);
				node.m_name = std::move(parsed.m_name);
				node.m_content = std::move(parsed.m_value);
				node.m_line = parsed.m_line;
				break;
			}
			default:
				break;
			}
		}
		queue.clear();
	};

	try {
		size_t at = 0;
		do {
			const size_t chunk = std::min(ibXmlParser::kChunk, size - at);
			parser.Feed(data + at, chunk, at + chunk == size, source);
			at += chunk;
			take();
			while (parser.Paused()) {
				parser.Resume(source);
				take();
			}
		} while (at < size);
	}
	catch (...) {
		m_nodeArray.clear();
		m_root = nullptr;
		throw;
	}
}
