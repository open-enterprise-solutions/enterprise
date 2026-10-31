#ifndef __VALUE_XML_H__
#define __VALUE_XML_H__

#include "backend/compiler/value.h"
#include "core/fstring.h"            // ibString - names, values, paths
#include "core/xml/xmlText.h"        // ibXmlParser, ibXmlNodeType - the parser under the reader

#include <deque>
#include <iosfwd>     // std::ifstream, std::ofstream - the files, through the standard library
#include <memory>
#include <string>
#include <vector>


void ibValueXmlNamespaceContext_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ⭐ XMLNamespaceContext - the namespaces in scope at one node. VENDED, never
// created: `New XMLNamespaceContext` has nothing to be the context OF; a reader (and a writer) hands one out.
// It is a SNAPSHOT: the scope it was taken in, whatever the reader has read since.
class BACKEND_API ibValueXmlNamespaceContext : public ibValueStaticMembers<&ibValueXmlNamespaceContext_BindNames> {
	enum Prop {
		enDefaultNamespace,
		enDepth,
	};
	enum Func {
		enLookupNamespaceURI,
		enLookupPrefix,
		enNamespaceURIs,
		enPrefixes,
		enNamespaceMappings,
	};
public:

	ibValueXmlNamespaceContext();               // nothing declared: `xml` alone, depth 0
	ibValueXmlNamespaceContext(std::shared_ptr<const ibXmlParser::ibNamespaceArray> namespaces, int depth);

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// The verbs for a caller in C++ (and the tests). Undefined where nothing is bound.
	ibString DefaultNamespace() const;          // "" when there is none
	int Depth() const { return m_depth; }
	ibValue LookupNamespaceURI(const ibString& prefix) const;
	ibValue LookupPrefix(const ibString& uri) const;
	const ibXmlParser::ibNamespaceArray& Namespaces() const { return *m_namespaces; }

private:
	std::shared_ptr<const ibXmlParser::ibNamespaceArray> m_namespaces;   // shared with the nodes of one scope; never null
	int m_depth = 0;
};

// ⭐⭐ XML, READ BY A SCRIPT - node by node, a pull reader: Read moves to the next node and the properties describe
// it. The parser is the engine's (core/xml/xmlText.h); this is what a script sees of it.
//
//     var reader = New XMLReader();
//     reader.OpenFile(path);
//     while (reader.Read()) {
//         if (reader.NodeType = XMLNodeType.StartElement And reader.Name = "TIN") { reader.Read(); tin = reader.Value; }
//     }
//
// WHAT IS A NODE: `<a/>` is a PAIR - StartElement and EndElement - and so is
// `<a></a>` (IsEmptyElement tells them apart - except inside an entity's replacement text, where
// the parser cannot); a comment and a processing instruction are nodes; CDATA is a node of its own, never merged
// into the text beside it; the XML declaration is NOT a node, XMLVersion / XMLEncoding / Standalone read it. Text
// between elements that is only whitespace is dropped while IgnoreWhitespace is True (the default); False hands it
// over as Whitespace nodes.
//
// THE ENCODING of a file is its byte-order mark or its declaration - UTF-8 without either; windows-1251 and cp866
// are read through the engine's own tables (ibXmlCodePage). A text given with SetString is characters already: an
// `encoding=` in ITS declaration names what the text once was, and is not applied again.
//
// THE ATTRIBUTES are the current element's: by number (from 0) or by name, and by a cursor - after FirstAttribute /
// NextAttribute / ReadAttribute the reader stands on one, NodeType is Attribute and Name / Value are the
// attribute's, until MoveToElement or the next Read. The `xmlns` declarations are attributes
// too - but FIRST, not where they stood: expat does not say where. An attribute
// that is not there is Undefined, not an empty string and not an error.
//
// ⚠ AN EXTERNAL ENTITY IS REFUSED, not read and not quietly left out: `<!ENTITY e SYSTEM "file:///...">` is how a
// document from outside asks the machine that reads it for one of its files.

void ibValueXmlReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueXmlReader : public ibValueStaticMembers<&ibValueXmlReader_BindNames> {
	enum Prop {
		enNodeType,
		enName,
		enLocalName,
		enPrefix,
		enNamespaceURI,
		enValue,
		enHasValue,
		enHasName,
		enIsWhitespace,
		enIsCharacters,
		enXMLVersion,
		enXMLEncoding,
		enInputEncoding,
		enStandalone,
		enIgnoreWhitespace,
		enIsEmptyElement,
		enDepth,
		enLineNumber,
		enLinePosition,
		enNamespaceContext,
	};
	enum Func {
		enOpenFile,
		enSetString,
		enClose,
		enRead,
		enSkip,
		enMoveToContent,
		enAttributeCount,
		enAttributeName,
		enAttributeLocalName,
		enAttributeNamespaceURI,
		enAttributePrefix,
		enAttributeValue,
		enGetAttribute,
		enFirstAttribute,
		enNextAttribute,
		enReadAttribute,
		enMoveToElement,
	};
public:

	typedef ibXmlParser::ibNode ibNode;
	typedef ibXmlParser::ibAttribute ibAttribute;

	ibValueXmlReader();
	virtual ~ibValueXmlReader();

	virtual bool IsEmpty() const override { return !m_open; }

	virtual bool Init() override { return true; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// The verbs for a caller in C++ (and the tests).
	void SetText(const ibString& text);          // the script's SetString: ibValue has a SetString of its own
	void OpenFile(const ibString& path);
	void Close();
	bool Read();                                  // false at the end of the document; raises on XML that is not
	void Skip();                                  // an element's content: stops ON its EndElement
	ibXmlNodeType MoveToContent();                // past whitespace, comments, instructions; the type it stops on
	void SetIgnoreWhitespace(bool ignore) { m_ignoreWhitespace = ignore; }

	// The attributes of the current element - still the element's while the cursor stands on one of them.
	long AttributeCount() const { return static_cast<long>(m_current.m_attributeArray.size()); }
	ibValue AttributeValue(const ibValue& indexOrName, const ibString& uri = ibString()) const;   // Undefined: none
	bool FirstAttribute();
	bool NextAttribute();                         // on the element: the first; past the last: false, and stays
	bool ReadAttribute() { return NextAttribute(); }
	bool MoveToElement();                         // true when it stood on an attribute

	const ibNode& Current() const { return m_current; }   // the element, while on one of its attributes
	ibXmlNodeType NodeType() const { return m_attribute >= 0 ? ibXmlNodeType_Attribute : m_current.m_type; }
	const ibString& Name() const { return m_attribute >= 0 ? OnAttribute().m_name : m_current.m_name; }
	const ibString& Value() const { return m_attribute >= 0 ? OnAttribute().m_value : m_current.m_value; }
	const ibString& LocalName() const { return m_attribute >= 0 ? OnAttribute().m_local : m_current.m_local; }
	const ibString& Prefix() const { return m_attribute >= 0 ? OnAttribute().m_prefix : m_current.m_prefix; }
	const ibString& NamespaceURI() const { return m_attribute >= 0 ? OnAttribute().m_uri : m_current.m_uri; }
	// Of the element, on an attribute too: the attribute has no line of its own here, and one depth for the tag
	// keeps `Depth` what a script compares an element's by.
	bool IsEmptyElement() const { return m_current.m_emptyElement; }
	// ⚠ Not the Depth of its NamespaceContext, on purpose: here an element's two nodes agree, there an EndElement
	// counts one less - see NamespaceContext().
	int Depth() const { return m_current.m_depth; }
	// A snapshot of the current node's scope - the element's, on an attribute; Undefined when nothing is open.
	ibValue NamespaceContext() const;
	bool HasValue() const;
	bool HasName() const;
	bool IsWhitespace() const;
	bool IsCharacters() const;

	// What the declaration said - known once reading has reached past it.
	const ibString& XMLVersion() const { return m_version; }
	const ibString& XMLEncoding() const { return m_encoding; }
	bool Standalone() const { return m_standalone; }

	// How many parsed nodes wait to be read. For the tests: the bound on it is what keeps an entity bomb from
	// being expanded into memory whole.
	size_t QueuedNodes() const { return m_queue.size(); }

private:
	bool Fill();                                  // one more chunk into m_queue; false when the input is over
	void Release();
	const ibAttribute& OnAttribute() const { return m_current.m_attributeArray[static_cast<size_t>(m_attribute)]; }
	const ibValue& TypeValue(ibXmlNodeType type);

	std::unique_ptr<ibXmlParser> m_parser;
	std::unique_ptr<std::ifstream> m_file;        // the source when it is a file...
	std::vector<char>         m_chunk;            // ...read into this, a chunk at a time
	std::string               m_text;             // ...or the text, as UTF-8, when it is a string
	size_t                    m_textAt = 0;
	ibString                  m_fileName;
	ibString                  m_failure;          // a refusal once raised is raised again, not walked past

	std::deque<ibNode>        m_queue;            // parsed, not yet read
	ibNode                    m_current;
	long                      m_attribute = -1;   // the attribute cursor: an index into m_current.m_attributeArray
	ibString                  m_version, m_encoding;
	bool                      m_standalone = false;
	bool                      m_ignoreWhitespace = true;
	bool                      m_open = false;

	// What NodeType answers, made once per kind - a walk IS `while (reader.Read()) { if (reader.NodeType = ...`.
	ibValue m_typeValues[ibXmlNodeType_Notation + 1];
};

// ⭐ ...AND WRITTEN. The writer keeps the document well-formed (core's ibXmlWriter does): an attribute only right
// after its element's start, an end only for what was started, Close() refused while anything is open.
//
//     var writer = New XMLWriter();
//     writer.OpenFile(path, "windows-1251");
//     writer.WriteXMLDeclaration();
//     writer.WriteStartElement("DECLAR");
//     writer.WriteAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
//     writer.WriteStartElement("TIN"); writer.WriteText(tin); writer.WriteEndElement();
//     writer.WriteEndElement();
//     writer.Close();
//
// THE TARGET is a file - OpenFile(path, encoding = "UTF-8", writeBOM = True): UTF-8 gets its byte-order mark unless
// told otherwise, UTF-16 always has one, windows-1251 and cp866 have none - or a string: SetString(encoding), the
// encoding then only the word the declaration says, and Close() hands the text over.
//
// A character the encoding has no byte for is written as a `&#N;` reference in text and in an attribute (legal XML,
// read back as the character); in a name, a comment, an instruction or a CDATA section it is refused.
// Indent (True by default) puts an element on a line of its own, a tab a level - not inside an element that holds
// text, where the line would become part of the text.

void ibValueXmlWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueXmlWriter : public ibValueStaticMembers<&ibValueXmlWriter_BindNames> {
	enum Prop {
		enIndent,
		enNamespaceContext,
	};
	enum Func {
		enOpenFile,
		enSetString,
		enClose,
		enWriteXMLDeclaration,
		enWriteStartElement,
		enWriteEndElement,
		enWriteAttribute,
		enWriteStartAttribute,
		enWriteEndAttribute,
		enWriteText,
		enWriteCDATASection,
		enWriteComment,
		enWriteProcessingInstruction,
		enWriteNamespaceMapping,
		enWriteRaw,
		enWriteCurrent,
		enLookupPrefix,
	};
public:

	ibValueXmlWriter();
	virtual ~ibValueXmlWriter();

	virtual bool Init() override { return true; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// The verbs for a caller in C++ (and the tests); the writing ones are the target's (Target()).
	void OpenFile(const ibString& path, const ibString& encoding, bool writeBom);
	void SetText(const ibString& encoding);       // the script's SetString: ibValue has a SetString of its own
	ibString Close();                             // the text, for a string; "" for a file
	void WriteCurrent(const ibValueXmlReader& reader);
	ibXmlWriter& Target();                        // raises while nothing is open

private:
	void Flush();                                 // what was written, on to the file once enough of it waits
	void Release();                               // the target closed as it stands; a file not written is raised

	std::unique_ptr<ibXmlWriter> m_writer;
	std::unique_ptr<std::ofstream> m_file;        // null: a string
	ibXmlCodePage                m_page;
	ibString                     m_encoding;      // as the declaration names it
	ibString                     m_fileName;
	std::string                  m_bytes;         // encoded, not in the file yet
	bool                         m_indent = true;
};

#endif // !__VALUE_XML_H__
