////////////////////////////////////////////////////////////////////////////
//	Description : XML read by a script
////////////////////////////////////////////////////////////////////////////

#include "valueXml.h"

#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"            // ConvertToEnumValue<> is declared in value.h and DEFINED here
#include "backend/system/systemManagerEnum.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueMap.h"

#include "core/fileSystem.h"   // ibPathOf - a path as the standard library takes it
#include "core/diagnostics/journal.h"   // ibJournalError - a file a destroyed writer could not write, with nobody to tell

#include <algorithm>   // std::min, std::none_of
#include <cstddef>     // std::size_t
#include <exception>
#include <filesystem>  // std::filesystem::equivalent - the same file under two spellings
#include <fstream>
#include <string>
#include <utility>     // std::move, std::pair

namespace {

// The attribute a number names, or -1 when it names none: negative, fractional or past the end are all "no such
// attribute". A fraction is not cut to its whole part: 1.5 names no attribute, not the second.
long ibAttributeIndex(const ibNumber& number, size_t count)
{
	long long index = 0;
	if (number.IsSign() || number.Trunc() != number || number.ToInt(index) != 0
		|| static_cast<unsigned long long>(index) >= count)
		return -1;
	return static_cast<long>(index);
}

} // namespace

//////////////////////////////////////////////////////////////////////
// XMLNamespaceContext
//////////////////////////////////////////////////////////////////////

void ibValueXmlNamespaceContext_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("DefaultNamespace"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Depth"), true, false, wxNOT_FOUND);

	helper.AppendFunc(ibT("LookupNamespaceURI"), 1, ibT("LookupNamespaceURI(prefix : string)"));
	helper.AppendFunc(ibT("LookupPrefix"), 1, ibT("LookupPrefix(namespaceURI : string)"));
	helper.AppendFunc(ibT("NamespaceURIs"), ibT("NamespaceURIs()"));
	helper.AppendFunc(ibT("Prefixes"), 1, ibT("Prefixes(namespaceURI : string)"));
	helper.AppendFunc(ibT("NamespaceMappings"), ibT("NamespaceMappings()"));
}

ibValueXmlNamespaceContext::ibValueXmlNamespaceContext()
	: ibValueXmlNamespaceContext(nullptr, 0)
{
}

ibValueXmlNamespaceContext::ibValueXmlNamespaceContext(std::shared_ptr<const ibXmlParser::ibNamespaceArray> namespaces, int depth)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_namespaces(std::move(namespaces)), m_depth(depth)
{
	if (m_namespaces == nullptr)
		m_namespaces = ibXmlParser::Resolve(std::vector<std::pair<ibString, ibString>>());
}

ibString ibValueXmlNamespaceContext::DefaultNamespace() const
{
	const ibValue uri = LookupNamespaceURI(ibString());
	return uri.GetType() == ibValueTypes::TYPE_STRING ? uri.GetString() : ibString();
}

ibValue ibValueXmlNamespaceContext::LookupNamespaceURI(const ibString& prefix) const
{
	for (const std::pair<ibString, ibString>& pair : *m_namespaces)
		if (pair.first == prefix)
			return pair.second;
	return ibValue();
}

// The first prefix bound to the URI; "" for the default namespace.
// With several prefixes on one URI it is the outermost.
ibValue ibValueXmlNamespaceContext::LookupPrefix(const ibString& uri) const
{
	for (const std::pair<ibString, ibString>& pair : *m_namespaces)
		if (pair.second == uri)
			return pair.first;
	return ibValue();
}

bool ibValueXmlNamespaceContext::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enDefaultNamespace: pvarPropVal = DefaultNamespace(); return true;
	case enDepth:            pvarPropVal = m_depth; return true;
	}
	return false;
}

bool ibValueXmlNamespaceContext::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	// A prefix and a URI are text: Undefined read as "" would quietly ask about the default namespace.
	const auto text = [&](bool prefix) -> ibString {
		if (lSizeArray >= 1 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING)
			return paParams[0]->GetString();
		const ibString type = lSizeArray < 1 ? ibString(ibT("Undefined")) : paParams[0]->GetClassName();
		if (prefix)
			ibBackendCoreException::Error(_("XMLNamespaceContext: a prefix is a string, not a value of the type '%s'"), type);
		ibBackendCoreException::Error(_("XMLNamespaceContext: a namespace URI is a string, not a value of the type '%s'"), type);
		return ibString();
	};

	switch (lMethodNum) {
	case enLookupNamespaceURI:
		pvarRetValue = LookupNamespaceURI(text(true));
		return true;
	case enLookupPrefix:
		pvarRetValue = LookupPrefix(text(false));
		return true;
	case enNamespaceURIs: {
		ibValuePtr<ibValueArray> uris(new ibValueArray());
		// Each once, where one URI has several prefixes.
		for (auto at = m_namespaces->begin(); at != m_namespaces->end(); ++at) {
			if (std::none_of(m_namespaces->begin(), at,
				[&](const std::pair<ibString, ibString>& pair) { return pair.second == at->second; }))
				uris->Add(at->second);
		}
		pvarRetValue = uris;
		return true;
	}
	case enPrefixes: {
		const ibString uri = text(false);
		ibValuePtr<ibValueArray> prefixes(new ibValueArray());
		for (const std::pair<ibString, ibString>& pair : *m_namespaces)
			if (pair.second == uri)
				prefixes->Add(pair.first);
		pvarRetValue = prefixes;
		return true;
	}
	case enNamespaceMappings: {
		ibValuePtr<ibValueContainer> mappings(new ibValueContainer());
		for (const std::pair<ibString, ibString>& pair : *m_namespaces)
			mappings->Insert(pair.first, pair.second);
		pvarRetValue = mappings;
		return true;
	}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// XMLReader
//////////////////////////////////////////////////////////////////////

// Order MUST match ibValueXmlReader::Prop and ::Func - the number is the index into each table.
void ibValueXmlReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, ibT("XMLReader()"));

	helper.AppendProp(ibT("NodeType"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Name"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("LocalName"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Prefix"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("NamespaceURI"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Value"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("HasValue"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("HasName"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("IsWhitespace"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("IsCharacters"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("XMLVersion"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("XMLEncoding"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("InputEncoding"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Standalone"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("IgnoreWhitespace"), true, true, wxNOT_FOUND);
	helper.AppendProp(ibT("IsEmptyElement"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Depth"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("LineNumber"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("LinePosition"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("NamespaceContext"), true, false, wxNOT_FOUND);

	helper.AppendFunc(ibT("OpenFile"), 1, ibT("OpenFile(path : string)"));
	helper.AppendFunc(ibT("SetString"), 1, ibT("SetString(text : string)"));
	helper.AppendFunc(ibT("Close"), ibT("Close()"));
	helper.AppendFunc(ibT("Read"), ibT("Read()"));
	helper.AppendFunc(ibT("Skip"), ibT("Skip()"));
	helper.AppendFunc(ibT("MoveToContent"), ibT("MoveToContent()"));
	helper.AppendFunc(ibT("AttributeCount"), ibT("AttributeCount()"));
	helper.AppendFunc(ibT("AttributeName"), 1, ibT("AttributeName(index : number)"));
	helper.AppendFunc(ibT("AttributeLocalName"), 1, ibT("AttributeLocalName(index : number)"));
	helper.AppendFunc(ibT("AttributeNamespaceURI"), 1, ibT("AttributeNamespaceURI(index : number)"));
	helper.AppendFunc(ibT("AttributePrefix"), 1, ibT("AttributePrefix(index : number)"));
	helper.AppendFunc(ibT("AttributeValue"), 2, ibT("AttributeValue(indexOrName : number | string, namespaceURI : string)"));
	helper.AppendFunc(ibT("GetAttribute"), 2, ibT("GetAttribute(indexOrName : number | string, namespaceURI : string)"));
	helper.AppendFunc(ibT("FirstAttribute"), ibT("FirstAttribute()"));
	helper.AppendFunc(ibT("NextAttribute"), ibT("NextAttribute()"));
	helper.AppendFunc(ibT("ReadAttribute"), ibT("ReadAttribute()"));
	helper.AppendFunc(ibT("MoveToElement"), ibT("MoveToElement()"));
}

ibValueXmlReader::ibValueXmlReader() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueXmlReader::~ibValueXmlReader() {}

void ibValueXmlReader::SetText(const ibString& text)
{
	Close();

	std::string utf8 = text.ToUtf8();   // a lone surrogate is spelled too - and refused by the parser where it stands
	m_parser = std::make_unique<ibXmlParser>(ibT("XMLReader"), ibXmlParser::kCharacters);
	m_text = std::move(utf8);
	m_textAt = 0;
	m_open = true;
}

void ibValueXmlReader::OpenFile(const ibString& path)
{
	Close();

	auto file = std::make_unique<std::ifstream>(ibPathOf(path), std::ios::binary);
	if (!file->is_open())
		ibBackendCoreException::Error(_("XMLReader: cannot open the file '%s' for reading"), path);

	m_parser = std::make_unique<ibXmlParser>(ibT("XMLReader"), ibXmlParser::kBytes);
	m_file = std::move(file);
	m_fileName = path;
	m_open = true;
}

// The source and the parser, let go of once the input is over.
void ibValueXmlReader::Release()
{
	m_parser.reset();
	m_file.reset();
	std::vector<char>().swap(m_chunk);
	std::string().swap(m_text);
}

void ibValueXmlReader::Close()
{
	Release();
	m_textAt = 0;
	m_fileName.clear();
	m_failure.clear();
	std::deque<ibNode>().swap(m_queue);
	m_current = ibNode();
	m_attribute = -1;
	m_version.clear();
	m_encoding.clear();
	m_standalone = false;
	m_open = false;
}

// One chunk of the input through the parser. A refusal is written down rather than raised here: the nodes the
// chunk made before the fault are handed over first, so where the reading stops does not depend on where a
// chunk happened to end.
bool ibValueXmlReader::Fill()
{
	if (m_parser == nullptr)
		return false;

	try {
		if (m_parser->Paused()) {
			m_parser->Resume(m_fileName);          // what it was given is not parsed to its end yet
		}
		else if (m_file != nullptr) {
			if (m_chunk.empty())
				m_chunk.resize(ibXmlParser::kChunk);            // one buffer for the whole file: expat copies what it keeps
			m_file->read(m_chunk.data(), static_cast<std::streamsize>(ibXmlParser::kChunk));
			const size_t size = static_cast<size_t>(m_file->gcount());
			if (m_file->bad())
				ibBackendCoreException::Error(_("XMLReader: reading the file '%s' failed"), m_fileName);
			m_parser->Feed(m_chunk.data(), size, size < ibXmlParser::kChunk, m_fileName);
		}
		else {
			const size_t length = m_text.length();
			const size_t size = std::min(ibXmlParser::kChunk, length - m_textAt);
			m_parser->Feed(m_text.data() + m_textAt, size, m_textAt + size == length, ibString());
			m_textAt += size;
		}
	}
	catch (const ibCoreException& error) {   // the parser's refusals and the reader's own alike
		m_failure = error.GetErrorDescription();
	}
	catch (const std::exception& error) {
		// Memory, above all: a refusal a script can catch, not an exception it has never heard of.
		m_failure = ibString::Format(_("XMLReader: reading failed: %s"), ibString::FromUTF8(error.what()));
	}
	// What the parser made waits in its queue; the reader's is empty whenever it fills, so the two trade places.
	// Taken so, the reader holds at most what made the parser pause.
	m_queue.swap(m_parser->Queue());
	m_version = m_parser->Version();
	m_encoding = m_parser->Encoding();
	m_standalone = m_parser->Standalone();
	if (!m_failure.empty() || m_parser->Finished())
		Release();
	return true;
}

bool ibValueXmlReader::Read()
{
	if (!m_open)
		ibBackendCoreException::Error(_("XMLReader: no text or file is open"));

	m_attribute = -1;
	for (;;) {
		while (m_queue.empty()) {
			if (!m_failure.empty()) {
				m_current = ibNode();
				ibBackendCoreException::Error(ibT("%s"), m_failure);
			}
			if (!Fill()) {
				m_current = ibNode();
				return false;
			}
		}
		ibNode node = std::move(m_queue.front());
		m_queue.pop_front();
		if (node.m_type == ibXmlNodeType_Whitespace && m_ignoreWhitespace)
			continue;
		m_current = std::move(node);
		return true;
	}
}

// On a StartElement the depth it has is also its EndElement's: reading until that one comes is the whole content,
// and for `<a/>` it is the very next node.
// ⚠ On an attribute it skips the attribute's element and stops on ITS end, like everywhere else here - not past it.
void ibValueXmlReader::Skip()
{
	MoveToElement();
	if (m_current.m_type != ibXmlNodeType_StartElement) {
		Read();
		return;
	}
	const int depth = m_current.m_depth;
	while (Read())
		if (m_current.m_type == ibXmlNodeType_EndElement && m_current.m_depth == depth)
			return;
}

ibXmlNodeType ibValueXmlReader::MoveToContent()
{
	if (MoveToElement())
		return m_current.m_type;   // an attribute's content is its element
	for (;;) {
		switch (m_current.m_type) {
		case ibXmlNodeType_None:   // before the first Read - and at the end, where Read says so
		case ibXmlNodeType_Whitespace:
		case ibXmlNodeType_Comment:
		case ibXmlNodeType_ProcessingInstruction:
		case ibXmlNodeType_XMLDeclaration:
		case ibXmlNodeType_DocumentTypeDefinition:
			if (!Read())
				return ibXmlNodeType_None;
			break;
		default:
			return m_current.m_type;
		}
	}
}

// By number the index; by name the qualified name as written (`d:b`) - or, with a URI, the local name in that
// namespace. An empty URI is none: an attribute without a prefix is in no namespace, so its local name IS its name
// (and ("d:b", "") finds d:b, not a local name `d:b` in no namespace, which no attribute can have).
ibValue ibValueXmlReader::AttributeValue(const ibValue& indexOrName, const ibString& uri) const
{
	const std::vector<ibAttribute>& attributes = m_current.m_attributeArray;
	if (indexOrName.GetType() == ibValueTypes::TYPE_NUMBER) {
		const long index = ibAttributeIndex(indexOrName.GetNumber(), attributes.size());
		return index < 0 ? ibValue() : ibValue(attributes[static_cast<size_t>(index)].m_value);
	}
	if (indexOrName.GetType() != ibValueTypes::TYPE_STRING)
		ibBackendCoreException::Error(_("XMLReader: an attribute is found by its number or its name, not by a value of the type '%s'"),
			indexOrName.GetClassName());
	const ibString name = indexOrName.GetString();
	for (const ibAttribute& attribute : attributes) {
		if (uri.empty() ? attribute.m_name == name : (attribute.m_local == name && attribute.m_uri == uri))
			return attribute.m_value;
	}
	return ibValue();
}

bool ibValueXmlReader::FirstAttribute()
{
	if (m_current.m_attributeArray.empty())
		return false;
	m_attribute = 0;
	return true;
}

// From the element the first attribute, so `while (r.ReadAttribute())` walks them all; past the last the cursor
// stays where it was.
bool ibValueXmlReader::NextAttribute()
{
	if (m_attribute < 0)
		return FirstAttribute();
	if (static_cast<size_t>(m_attribute) + 1 >= m_current.m_attributeArray.size())
		return false;
	++m_attribute;
	return true;
}

bool ibValueXmlReader::MoveToElement()
{
	if (m_attribute < 0)
		return false;
	m_attribute = -1;
	return true;
}

// Its Depth is one more than the reader's Depth, except on an EndElement (an empty element's included), where it is
// the reader's - the root's StartElement 1, the text inside it 2, its EndElement 0. On an attribute it is one more
// than on its element's start - an attribute counts a level below its element - and the scope is the element's.
ibValue ibValueXmlReader::NamespaceContext() const
{
	if (!m_open)
		return ibValue();
	const ibXmlNodeType type = NodeType();
	const int depth = m_current.m_depth
		+ (type == ibXmlNodeType_Attribute ? 2 : type == ibXmlNodeType_EndElement ? 0 : 1);
	ibValuePtr<ibValueXmlNamespaceContext> context(new ibValueXmlNamespaceContext(
		ibXmlParser::Resolve(m_current.m_namespaces.get()), depth));
	return context;
}

const ibValue& ibValueXmlReader::TypeValue(ibXmlNodeType type)
{
	ibValue& made = m_typeValues[type];
	if (made.GetType() == ibValueTypes::TYPE_EMPTY)
		made = ibValue::CreateEnumObject<ibValueEnumXmlNodeType>(type);
	return made;
}

bool ibValueXmlReader::HasValue() const
{
	switch (NodeType()) {
	case ibXmlNodeType_Attribute:
	case ibXmlNodeType_Text:
	case ibXmlNodeType_Whitespace:
	case ibXmlNodeType_CDATASection:
	case ibXmlNodeType_Comment:
	case ibXmlNodeType_ProcessingInstruction:
		return true;
	default:
		return false;
	}
}

bool ibValueXmlReader::HasName() const
{
	const ibXmlNodeType type = NodeType();
	return type == ibXmlNodeType_StartElement || type == ibXmlNodeType_EndElement
		|| type == ibXmlNodeType_ProcessingInstruction || type == ibXmlNodeType_Attribute;
}

// Whitespace, and Text of nothing but whitespace - here the second never exists: such text (`&#32;` included) is made
// a Whitespace node when it is read.
bool ibValueXmlReader::IsWhitespace() const
{
	return NodeType() == ibXmlNodeType_Whitespace;
}

bool ibValueXmlReader::IsCharacters() const
{
	const ibXmlNodeType type = NodeType();
	return type == ibXmlNodeType_Text || type == ibXmlNodeType_CDATASection || type == ibXmlNodeType_Whitespace;
}

bool ibValueXmlReader::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enNodeType:       pvarPropVal = TypeValue(NodeType()); return true;
	case enName:           pvarPropVal = Name(); return true;
	case enLocalName:      pvarPropVal = LocalName(); return true;
	case enPrefix:         pvarPropVal = Prefix(); return true;
	case enNamespaceURI:   pvarPropVal = NamespaceURI(); return true;
	case enValue:          pvarPropVal = Value(); return true;
	case enHasValue:       pvarPropVal = HasValue(); return true;
	case enHasName:        pvarPropVal = HasName(); return true;
	case enIsWhitespace:   pvarPropVal = IsWhitespace(); return true;
	case enIsCharacters:   pvarPropVal = IsCharacters(); return true;
	case enXMLVersion:     pvarPropVal = m_version; return true;
	case enXMLEncoding:    pvarPropVal = m_encoding; return true;
	case enInputEncoding:  pvarPropVal = m_encoding; return true;   // what the declaration says, as XMLEncoding
	case enStandalone:     pvarPropVal = m_standalone; return true;
	case enIgnoreWhitespace: pvarPropVal = m_ignoreWhitespace; return true;
	case enIsEmptyElement: pvarPropVal = IsEmptyElement(); return true;
	case enDepth:          pvarPropVal = Depth(); return true;
	// Numbers: a line or a column is a count.
	case enLineNumber:     pvarPropVal = ibNumber(static_cast<unsigned long long>(m_current.m_line)); return true;
	case enLinePosition:   pvarPropVal = ibNumber(static_cast<unsigned long long>(m_current.m_column)); return true;
	case enNamespaceContext: pvarPropVal = NamespaceContext(); return true;
	}
	return false;
}

bool ibValueXmlReader::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (lPropNum != enIgnoreWhitespace)
		return false;
	m_ignoreWhitespace = varPropVal.GetBoolean();
	return true;
}

bool ibValueXmlReader::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enOpenFile:
		if (lSizeArray < 1)
			return false;
		OpenFile(paParams[0]->GetString());
		return true;
	case enSetString:
		if (lSizeArray < 1)
			return false;
		SetText(paParams[0]->GetString());
		return true;
	case enClose:
		Close();
		return true;
	case enRead:
		pvarRetValue = Read();
		return true;
	case enSkip:
		Skip();
		return true;
	case enMoveToContent:
		pvarRetValue = TypeValue(MoveToContent());
		return true;
	case enAttributeCount:
		pvarRetValue = ibNumber(static_cast<unsigned long long>(AttributeCount()));
		return true;
	case enAttributeName:
	case enAttributeLocalName:
	case enAttributeNamespaceURI:
	case enAttributePrefix: {
		if (lSizeArray < 1)
			return false;
		if (paParams[0]->GetType() != ibValueTypes::TYPE_NUMBER)
			ibBackendCoreException::Error(_("XMLReader: an attribute is asked for by its number - a value of the type '%s' is not a number"),
				paParams[0]->GetClassName());
		const long index = ibAttributeIndex(paParams[0]->GetNumber(), m_current.m_attributeArray.size());
		if (index < 0) {
			// An empty string: a script compares `AttributeName(i) = ""`. (AttributeValue
			// says Undefined - an attribute can be there and empty.)
			pvarRetValue = ibString();
			return true;
		}
		const ibAttribute& attribute = m_current.m_attributeArray[static_cast<size_t>(index)];
		pvarRetValue = lMethodNum == enAttributeName ? attribute.m_name
			: lMethodNum == enAttributeLocalName ? attribute.m_local
			: lMethodNum == enAttributeNamespaceURI ? attribute.m_uri
			: attribute.m_prefix;
		return true;
	}
	case enAttributeValue:
	case enGetAttribute:
		if (lSizeArray < 1)
			return false;
		pvarRetValue = AttributeValue(*paParams[0], lSizeArray > 1 ? paParams[1]->GetString() : ibString());
		return true;
	case enFirstAttribute:
		pvarRetValue = FirstAttribute();
		return true;
	case enNextAttribute:
	case enReadAttribute:
		pvarRetValue = NextAttribute();
		return true;
	case enMoveToElement:
		pvarRetValue = MoveToElement();
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// XMLWriter
//////////////////////////////////////////////////////////////////////

namespace {

// A string argument, or a refusal that names the argument: Undefined read as "" would write an empty name, a
// number read as text would write it in whatever form the conversion chose.
ibString ibXmlStringParam(ibValue** paParams, const long lSizeArray, long at, const ibString& what)
{
	if (lSizeArray > at && paParams[at]->GetType() == ibValueTypes::TYPE_STRING)
		return paParams[at]->GetString();
	const ibString type = lSizeArray > at ? paParams[at]->GetClassName() : ibString(ibT("Undefined"));
	ibBackendCoreException::Error(_("XMLWriter: %s is a string, not a value of the type '%s'"), what, type);
	return ibString();
}

// ...and an optional one: absent or Undefined is "".
ibString ibXmlOptionalString(ibValue** paParams, const long lSizeArray, long at, const ibString& what)
{
	if (lSizeArray <= at || paParams[at]->GetType() == ibValueTypes::TYPE_EMPTY)
		return ibString();
	return ibXmlStringParam(paParams, lSizeArray, at, what);
}

} // namespace

// Order MUST match ibValueXmlWriter::Prop and ::Func - the number is the index into each table.
void ibValueXmlWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, ibT("XMLWriter()"));

	helper.AppendProp(ibT("Indent"), true, true, wxNOT_FOUND);
	helper.AppendProp(ibT("NamespaceContext"), true, false, wxNOT_FOUND);

	helper.AppendFunc(ibT("OpenFile"), 3, ibT("OpenFile(path : string, encoding : string, writeBOM : boolean)"));
	helper.AppendFunc(ibT("SetString"), 1, ibT("SetString(encoding : string)"));
	helper.AppendFunc(ibT("Close"), ibT("Close()"));
	helper.AppendFunc(ibT("WriteXMLDeclaration"), ibT("WriteXMLDeclaration()"));
	helper.AppendFunc(ibT("WriteStartElement"), 2, ibT("WriteStartElement(name : string, namespaceURI : string)"));
	helper.AppendFunc(ibT("WriteEndElement"), ibT("WriteEndElement()"));
	helper.AppendFunc(ibT("WriteAttribute"), 3, ibT("WriteAttribute(name : string, namespaceURI : string, value : string)"));
	helper.AppendFunc(ibT("WriteStartAttribute"), 2, ibT("WriteStartAttribute(name : string, namespaceURI : string)"));
	helper.AppendFunc(ibT("WriteEndAttribute"), ibT("WriteEndAttribute()"));
	helper.AppendFunc(ibT("WriteText"), 1, ibT("WriteText(text : string)"));
	helper.AppendFunc(ibT("WriteCDATASection"), 1, ibT("WriteCDATASection(text : string)"));
	helper.AppendFunc(ibT("WriteComment"), 1, ibT("WriteComment(text : string)"));
	helper.AppendFunc(ibT("WriteProcessingInstruction"), 2, ibT("WriteProcessingInstruction(name : string, text : string)"));
	helper.AppendFunc(ibT("WriteNamespaceMapping"), 2, ibT("WriteNamespaceMapping(prefix : string, namespaceURI : string)"));
	helper.AppendFunc(ibT("WriteRaw"), 1, ibT("WriteRaw(text : string)"));
	helper.AppendFunc(ibT("WriteCurrent"), 1, ibT("WriteCurrent(reader : XMLReader)"));
	helper.AppendFunc(ibT("LookupPrefix"), 1, ibT("LookupPrefix(namespaceURI : string)"));
}

ibValueXmlWriter::ibValueXmlWriter() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueXmlWriter::~ibValueXmlWriter()
{
	// Nobody to raise it to: a file that could not be written is said in the journal.
	try {
		Release();
	}
	catch (const ibCoreException& error) {
		ibJournalError(ibT("xml"), ibT("%s"), error.GetErrorDescription());
	}
}

ibXmlWriter& ibValueXmlWriter::Target()
{
	if (m_writer == nullptr)
		ibBackendCoreException::Error(_("XMLWriter: no file or string is open - OpenFile or SetString first"));
	return *m_writer;
}

namespace {

ibXmlCodePage ibXmlPageNamed(const ibString& encoding)
{
	const ibXmlCodePage page = ibXmlCodePage::Find(encoding.IsEmpty() ? ibString(ibT("UTF-8")) : encoding);
	if (page.Kind() == ibXmlCodePage::kUnknown)
		ibBackendCoreException::Error(_("XMLWriter: the encoding '%s' is not supported; supported: %s"),
			encoding, ibXmlCodePage::Supported());
	return page;
}

} // namespace

// A target still open is released first, as Close releases it - what was written of it reaches its file, even
// unfinished - but only once the new file is open: a path that cannot be written leaves the writer as it was. The
// old file failing to write is raised, and leaves the writer with no target.
void ibValueXmlWriter::OpenFile(const ibString& path, const ibString& encoding, bool writeBom)
{
	const ibXmlCodePage page = ibXmlPageNamed(encoding);
	// The same file again: released first, or two handles would write one file over each other.
	std::error_code differs;
	if (m_file != nullptr && std::filesystem::equivalent(ibPathOf(m_fileName), ibPathOf(path), differs))
		Release();

	auto file = std::make_unique<std::ofstream>(ibPathOf(path), std::ios::binary | std::ios::trunc);
	if (!file->is_open())
		ibBackendCoreException::Error(_("XMLWriter: cannot open the file '%s' for writing"), path);

	Release();
	m_page = page;
	m_encoding = page.Name();
	m_fileName = path;
	// UTF-16 cannot be read without its mark; UTF-8 has one unless a script says not; a single-byte page has none.
	if (page.Kind() == ibXmlCodePage::kUtf16)
		m_bytes = "\xFF\xFE";
	else if (page.Kind() == ibXmlCodePage::kUtf8 && writeBom)
		m_bytes = "\xEF\xBB\xBF";
	m_file = std::move(file);
	m_writer = std::make_unique<ibXmlWriter>(ibT("XMLWriter"), page);
	m_writer->SetIndent(m_indent);
}

void ibValueXmlWriter::SetText(const ibString& encoding)
{
	// Only the word the declaration says - the name of the page it means, as OpenFile writes it, so that a reader
	// of the text saved to a file is not handed an alias it refuses. A word nothing could read is refused here too.
	const ibXmlCodePage named = ibXmlPageNamed(encoding);
	Release();
	m_page = ibXmlCodePage::Find(ibT("UTF-8"));   // a string holds every character: none becomes a reference
	m_encoding = named.Name();
	m_writer = std::make_unique<ibXmlWriter>(ibT("XMLWriter"), m_page);
	m_writer->SetIndent(m_indent);
}

void ibValueXmlWriter::Flush()
{
	if (m_file == nullptr)
		return;
	m_page.Encode(m_writer->Take(), ibT("XMLWriter"), m_bytes);
	if (m_bytes.size() < ibXmlParser::kChunk)
		return;
	if (!m_file->write(m_bytes.data(), static_cast<std::streamsize>(m_bytes.size())))
		ibBackendCoreException::Error(_("XMLWriter: writing to the file '%s' failed"), m_fileName);
	m_bytes.clear();
}

// What is written reaches the file and the file is closed - by Close, by a writer opened again, by one destroyed: a
// file cut at its last full piece is worse than one that holds all that was written. The writer is empty after it
// whatever happens; a file that could not be written is raised - the script that opened it is told (the destructor,
// with nobody to tell, journals it).
void ibValueXmlWriter::Release()
{
	const std::unique_ptr<std::ofstream> file = std::move(m_file);
	const std::unique_ptr<ibXmlWriter> writer = std::move(m_writer);
	std::string bytes = std::move(m_bytes);
	const ibString fileName = m_fileName;
	m_bytes.clear();
	m_fileName.clear();
	if (file == nullptr || writer == nullptr)
		return;
	m_page.Encode(writer->Take(), ibT("XMLWriter"), bytes);
	// ⚠ ON DISK WHEN THE FILE CLOSED, not when the last Write answered: a full disk says so at the close.
	const bool written = static_cast<bool>(file->write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
	file->close();
	if (file->fail() || !written)
		ibBackendCoreException::Error(_("XMLWriter: writing to the file '%s' failed"), fileName);
}

ibString ibValueXmlWriter::Close()
{
	if (m_writer == nullptr)
		return ibString();
	m_writer->Finish();
	if (m_file == nullptr) {
		const ibString text = m_writer->Take();
		m_writer.reset();
		return text;
	}
	Release();
	return ibString();
}

// The reader's CURRENT node, not its subtree: an element's start with its attributes (its declarations among them),
// an end, a text - so a loop over a reader copies a document, and one that skips a node leaves it out.
void ibValueXmlWriter::WriteCurrent(const ibValueXmlReader& reader)
{
	ibXmlWriter& writer = Target();
	const ibValueXmlReader::ibNode& node = reader.Current();
	switch (reader.NodeType()) {
	case ibXmlNodeType_StartElement:
		writer.WriteStartElement(node.m_name, node.m_uri, node.m_attributeArray);   // one write: all of it, or none
		if (node.m_emptyElement)
			writer.WriteEndElement();   // `<a/>`: the end the reader hands over next is written by this start
		break;
	case ibXmlNodeType_EndElement:
		if (!node.m_emptyElement)
			writer.WriteEndElement();
		break;
	case ibXmlNodeType_Attribute:
		writer.WriteAttribute(reader.Name(), reader.NamespaceURI(), reader.Value());
		break;
	case ibXmlNodeType_Text:
	case ibXmlNodeType_Whitespace:
		writer.WriteText(node.m_value);
		break;
	case ibXmlNodeType_CDATASection:
		writer.WriteCDATASection(node.m_value);
		break;
	case ibXmlNodeType_Comment:
		writer.WriteComment(node.m_value);
		break;
	case ibXmlNodeType_ProcessingInstruction:
		writer.WriteProcessingInstruction(node.m_name, node.m_value);
		break;
	default:
		break;   // nothing read yet, or the end: nothing to write
	}
}

bool ibValueXmlWriter::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enIndent:
		pvarPropVal = m_indent;
		return true;
	case enNamespaceContext: {
		if (m_writer == nullptr) {
			pvarPropVal = ibValue();
			return true;
		}
		ibValuePtr<ibValueXmlNamespaceContext> context(new ibValueXmlNamespaceContext(
			ibXmlParser::Resolve(m_writer->Declared()), m_writer->Depth()));
		pvarPropVal = context;
		return true;
	}
	}
	return false;
}

bool ibValueXmlWriter::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (lPropNum != enIndent)
		return false;
	m_indent = varPropVal.GetBoolean();
	if (m_writer != nullptr)
		m_writer->SetIndent(m_indent);
	return true;
}

bool ibValueXmlWriter::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enOpenFile:
		OpenFile(ibXmlStringParam(paParams, lSizeArray, 0, _("the path")),
			ibXmlOptionalString(paParams, lSizeArray, 1, _("the encoding")),
			lSizeArray < 3 || paParams[2]->GetType() == ibValueTypes::TYPE_EMPTY || paParams[2]->GetBoolean());
		return true;
	case enSetString:
		SetText(ibXmlOptionalString(paParams, lSizeArray, 0, _("the encoding")));
		return true;
	case enClose:
		pvarRetValue = Close();
		return true;
	case enWriteXMLDeclaration:
		Target().WriteXmlDeclaration(m_encoding);
		break;
	case enWriteStartElement:
		Target().WriteStartElement(ibXmlStringParam(paParams, lSizeArray, 0, _("the name")),
			ibXmlOptionalString(paParams, lSizeArray, 1, _("the namespace URI")));
		break;
	case enWriteEndElement:
		Target().WriteEndElement();
		break;
	case enWriteAttribute:
		// (name, value) or (name, namespaceURI, value).
		if (lSizeArray >= 3 && paParams[2]->GetType() != ibValueTypes::TYPE_EMPTY)
			Target().WriteAttribute(ibXmlStringParam(paParams, lSizeArray, 0, _("the name")),
				ibXmlOptionalString(paParams, lSizeArray, 1, _("the namespace URI")),
				ibXmlStringParam(paParams, lSizeArray, 2, _("the value")));
		else
			Target().WriteAttribute(ibXmlStringParam(paParams, lSizeArray, 0, _("the name")), ibString(),
				ibXmlStringParam(paParams, lSizeArray, 1, _("the value")));
		break;
	case enWriteStartAttribute:
		Target().WriteStartAttribute(ibXmlStringParam(paParams, lSizeArray, 0, _("the name")),
			ibXmlOptionalString(paParams, lSizeArray, 1, _("the namespace URI")));
		break;
	case enWriteEndAttribute:
		Target().WriteEndAttribute();
		break;
	case enWriteText:
		Target().WriteText(ibXmlStringParam(paParams, lSizeArray, 0, _("the text")));
		break;
	case enWriteCDATASection:
		Target().WriteCDATASection(ibXmlStringParam(paParams, lSizeArray, 0, _("the text")));
		break;
	case enWriteComment:
		Target().WriteComment(ibXmlStringParam(paParams, lSizeArray, 0, _("the text")));
		break;
	case enWriteProcessingInstruction:
		Target().WriteProcessingInstruction(ibXmlStringParam(paParams, lSizeArray, 0, _("the name")),
			ibXmlOptionalString(paParams, lSizeArray, 1, _("the text")));
		break;
	case enWriteNamespaceMapping:
		Target().WriteNamespaceMapping(ibXmlOptionalString(paParams, lSizeArray, 0, _("the prefix")),
			ibXmlStringParam(paParams, lSizeArray, 1, _("the namespace URI")));
		break;
	case enWriteRaw:
		Target().WriteRaw(ibXmlStringParam(paParams, lSizeArray, 0, _("the text")));
		break;
	case enWriteCurrent: {
		const ibValueXmlReader* reader = lSizeArray > 0 ? dynamic_cast<const ibValueXmlReader*>(paParams[0]->GetRef()) : nullptr;
		if (reader == nullptr)
			ibBackendCoreException::Error(_("XMLWriter: WriteCurrent writes what an XMLReader stands on - not a value of the type '%s'"),
				lSizeArray > 0 ? paParams[0]->GetClassName() : ibString(ibT("Undefined")));
		WriteCurrent(*reader);
		break;
	}
	case enLookupPrefix: {
		const ibString uri = ibXmlStringParam(paParams, lSizeArray, 0, _("the namespace URI"));
		ibString prefix;
		if (m_writer != nullptr && m_writer->LookupPrefix(uri, prefix))
			pvarRetValue = prefix;
		else
			pvarRetValue = ibValue();
		return true;
	}
	default:
		return false;
	}
	Flush();
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueXmlReader, "XMLReader", value_to_clsid("VL_XMRD"));
VALUE_TYPE_REGISTER(ibValueXmlWriter, "XMLWriter", value_to_clsid("VL_XMWR"));
// SYSTEM, not VALUE: a reader or a writer hands one out, and a script cannot write `New` for it.
SYSTEM_TYPE_REGISTER(ibValueXmlNamespaceContext, "XMLNamespaceContext", system_to_clsid("SY_XMNC"));
