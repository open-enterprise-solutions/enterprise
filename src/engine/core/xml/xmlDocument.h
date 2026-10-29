#ifndef __XML_DOCUMENT_H__
#define __XML_DOCUMENT_H__

#include "core/core.h"
#include "core/fstring.h"          // ibString - names, text
#include "core/xml/xmlText.h"      // ibXmlParser - what the tree is read with; ibXmlNodeType - what a node is

#include <deque>
#include <vector>

// ⭐ A WHOLE DOCUMENT AS A TREE - wxXmlDocument's shape, read with the core's own parser, so that the engine reads
// a document held whole with no wx. A node is an element, a text (whitespace included: `<t xml:space="preserve"> </t>`
// is a space, not nothing), a CDATA section, a comment or an instruction; the document's root is its element.
//
// The nodes live in the document and are linked by pointers - a child, a sibling, a parent - so a sheet of a hundred
// thousand rows is let go of in one pass, not one stack frame a row. A node is valid while its document is.
class CORE_API ibXmlNode {
public:
	ibXmlNode() = default;
	ibXmlNode(const ibXmlNode&) = delete;              // its links point into its own document
	ibXmlNode& operator=(const ibXmlNode&) = delete;

	ibXmlNodeType GetType() const { return m_type; }
	const ibString& GetName() const { return m_name; }          // an element's name as written (`r:id`), a PI's target
	const ibString& GetContent() const { return m_content; }    // a text's, a section's, a comment's, a PI's
	// The text of the first text-like child - what a `<t>Total</t>` holds; empty when there is none.
	ibString GetNodeContent() const;
	// The line it begins on (ibXmlParser::ibNode's) - what a reader of the tree names when it refuses the node.
	unsigned long long GetLine() const { return m_line; }

	ibXmlNode* GetParent() const { return m_parent; }
	ibXmlNode* GetChildren() const { return m_children; }
	ibXmlNode* GetNext() const { return m_next; }

	// An attribute by its name as written, `xmlns` declarations among them.
	bool HasAttribute(const ibString& name) const;
	ibString GetAttribute(const ibString& name, const ibString& absent = ibString()) const;

private:
	friend class ibXmlDocument;

	const ibXmlParser::ibAttribute* FindAttribute(const ibString& name) const;

	ibXmlNodeType m_type = ibXmlNodeType_None;
	ibString      m_name;
	ibString      m_content;
	unsigned long long m_line = 0;
	std::vector<ibXmlParser::ibAttribute> m_attributeArray;
	ibXmlNode*    m_parent = nullptr;
	ibXmlNode*    m_children = nullptr;
	ibXmlNode*    m_lastChild = nullptr;
	ibXmlNode*    m_next = nullptr;
};

class CORE_API ibXmlDocument {
public:
	ibXmlDocument() = default;
	ibXmlDocument(const ibXmlDocument&) = delete;
	ibXmlDocument& operator=(const ibXmlDocument&) = delete;

	// The text read whole — a string's, handed to the parser as UTF-8 (an `encoding=` in its declaration is then not
	// applied); a text that is not well-formed is refused (ibCoreException, "XML: ... at line N, column M", "of 'source'"
	// when one is named — ibXmlParser::Feed's), and the document is then empty.
	void Load(const ibString& text, const ibString& source = ibString());
	// …and bytes as a file holds them, read in the encoding its mark or its declaration names (ibXmlParser's kBytes):
	// no string is made of the whole of it first, and none is turned back into UTF-8 for the parser.
	void Load(const char* data, size_t size, const ibString& source = ibString());
	ibXmlNode* GetRoot() const { return m_root; }

private:
	void Load(ibXmlParser& parser, const char* data, size_t size, const ibString& source);
	ibXmlNode& Append(ibXmlNode* parent, ibXmlNodeType type);

	std::deque<ibXmlNode> m_nodeArray;   // every node; a deque, so that a node never moves once made
	ibXmlNode*            m_root = nullptr;
};

#endif // !__XML_DOCUMENT_H__
