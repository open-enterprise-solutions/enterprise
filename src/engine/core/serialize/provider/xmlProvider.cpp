#include "core/serialize/provider/xmlProvider.h"
#include "core/xml/xmlText.h"                // ibXmlParser / ibXmlWriter — the engine's XML, read and written
#include "core/utils/base64.h"               // ibBase64 — a binary's text and a string XML cannot hold
#include "core/exception.h"                  // ibCoreException — the refusals
#include "core/localization/dictionary.h"    // _() — their words

#include <memory>
#include <string>
#include <vector>

namespace {

const ibChar* const kWho = ibT("ibXmlProvider");

void CheckDepth(int depth, unsigned long long line)
{
	if (depth > ibXmlProvider::kMaxNesting)
		ibCoreException::Error(_("ibXmlProvider: nesting deeper than %d, line %llu"), ibXmlProvider::kMaxNesting, line);
}

// What XML 1.0 holds as a character: a tab, the line breaks, and the rest from U+0020 — not U+FFFE / U+FFFF, not a
// surrogate on its own. A string with anything else goes in base64 (see the header): the writer would refuse it.
bool HoldsXml(const ibString& text)
{
	const ibChar* chars = text.c_str();
	const size_t length = text.Len();
	for (size_t i = 0; i < length; ++i) {
		const char16_t c = static_cast<char16_t>(chars[i]);
		if (c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D)
			return false;
		if (c == 0xFFFE || c == 0xFFFF)
			return false;
		if (c >= 0xD800 && c <= 0xDBFF) {
			if (i + 1 >= length || static_cast<char16_t>(chars[i + 1]) < 0xDC00 || static_cast<char16_t>(chars[i + 1]) > 0xDFFF)
				return false;
			++i;
		}
		else if (c >= 0xDC00 && c <= 0xDFFF)
			return false;
	}
	return true;
}

bool IsBlank(const ibString& text)
{
	const ibChar* chars = text.c_str();
	for (size_t i = 0; i < text.Len(); ++i) {
		if (chars[i] != ibT(' ') && chars[i] != ibT('\t') && chars[i] != ibT('\n') && chars[i] != ibT('\r'))
			return false;
	}
	return true;
}

ibString Base64Of(const void* data, size_t size)
{
	ibString text;
	ibBase64::Append(text, data, size);
	return text;
}

// THE TREE WRITTEN — a node an element, each entry a Field that says its kind (see the header).
class ibXmlTreeWriting {
public:
	ibXmlTreeWriting(ibXmlWriter& writer, const std::function<ibString(ibClassID)>& resolveType)
		: m_writer(writer), m_resolveType(resolveType) {}

	void WriteNode(const ibDataNode& node, int depth)
	{
		CheckDepth(depth, 0);
		m_writer.WriteStartElement(ibT("Node"), ibString());
		if (node.GetClsid() != 0) {
			const ibString name = m_resolveType ? m_resolveType(node.GetClsid()) : ibString();
			m_writer.WriteAttribute(ibT("Type"), ibString(), !name.IsEmpty() ? name
				: ibString::Format(ibT("%llu"), static_cast<unsigned long long>(node.GetClsid())));
		}
		if (node.GetMetaId() != 0)
			m_writer.WriteAttribute(ibT("Id"), ibString(), ibString::Format(ibT("%d"), node.GetMetaId()));
		for (const auto& [name, value] : node.Fields()) {
			m_writer.WriteStartElement(ibT("Field"), ibString());
			m_writer.WriteAttribute(ibT("Name"), ibString(), name);
			WriteValue(value, depth + 1);
			m_writer.WriteEndElement();
		}
		if (node.HasRawData()) {
			const ibMemoryBuffer raw = node.RawData();
			m_writer.WriteStartElement(ibT("Raw"), ibString());
			m_writer.WriteText(Base64Of(raw.GetData(), raw.GetDataLen()));
			m_writer.WriteEndElement();
		}
		if (!node.Children().empty()) {
			m_writer.WriteStartElement(ibT("Children"), ibString());
			for (const ibDataNode& child : node.Children())
				WriteNode(child, depth + 1);
			m_writer.WriteEndElement();
		}
		m_writer.WriteEndElement();
	}

private:
	// A value into the Field or the Item open: its kind, then what it holds.
	void WriteValue(const ibDataValue& value, int depth)
	{
		CheckDepth(depth, 0);
		switch (value.Kind()) {
		case ibDataKind::Empty:
			Kind(ibT("Empty"));
			break;
		case ibDataKind::Bool:
			Kind(ibT("Bool"));
			m_writer.WriteText(value.AsBool() ? ibT("true") : ibT("false"));
			break;
		case ibDataKind::Number:
			Kind(ibT("Number"));
			m_writer.WriteText(value.AsNumber().ToString());
			break;
		case ibDataKind::Date: {
			// To the millisecond, from the parts — four digits of a year whatever it is — read back by
			// ibDateTime::FromString; the empty date is no text at all.
			const ibDateTime date = value.AsDate();
			Kind(ibT("Date"));
			if (!date.IsEmpty()) {
				ibDateTimeParts p;
				date.ToParts(p);
				m_writer.WriteText(ibString::Format(ibT("%04d-%02u-%02u %02u:%02u:%02u.%03u"),
					p.m_year, p.m_month, p.m_day, p.m_hour, p.m_minute, p.m_second, p.m_millisecond));
			}
			break;
		}
		case ibDataKind::String: {
			const ibString text = value.AsString();
			if (HoldsXml(text)) {
				if (!text.IsEmpty())
					m_writer.WriteText(text);
				break;
			}
			const std::string utf8 = text.ToUTF8();
			Kind(ibT("String"));
			m_writer.WriteAttribute(ibT("Encoding"), ibString(), ibT("base64"));
			m_writer.WriteText(Base64Of(utf8.data(), utf8.size()));
			break;
		}
		case ibDataKind::Binary: {
			const ibMemoryBuffer bytes = value.AsBinary();
			Kind(ibT("Binary"));
			m_writer.WriteText(Base64Of(bytes.GetData(), bytes.GetDataLen()));
			break;
		}
		case ibDataKind::Child: {
			const std::optional<ibDataNode> child = value.AsChild();
			if (child)
				WriteNode(*child, depth + 1);
			else
				Kind(ibT("Empty"));
			break;
		}
		case ibDataKind::Array:
			Kind(ibT("Array"));
			for (const ibDataValue& item : value.AsArray()) {
				m_writer.WriteStartElement(ibT("Item"), ibString());
				WriteValue(item, depth + 1);
				m_writer.WriteEndElement();
			}
			break;
		}
	}

	void Kind(const ibChar* kind) { m_writer.WriteAttribute(ibT("Kind"), ibString(), kind); }

	ibXmlWriter& m_writer;
	const std::function<ibString(ibClassID)>& m_resolveType;
};

// …AND READ BACK — pulled from the parser a node at a time; what is not of the layout is refused where it stands.
class ibXmlTreeReading {
public:
	ibXmlTreeReading(ibXmlParser& parser, const std::function<ibClassID(const ibString&)>& lookupType)
		: m_parser(parser), m_lookupType(lookupType) {}

	ibDataNode ReadRoot()
	{
		ibXmlParser::ibNode start;
		do {
			start = Next();
			if (start.m_type == ibXmlNodeType_None)
				ibCoreException::Error(_("ibXmlProvider: the document holds no Node"));
		} while (start.m_type != ibXmlNodeType_StartElement);
		if (start.m_name != ibT("Node"))
			Misplaced(start, ibT("the document"));
		ibDataNode root = ReadNode(start, 1);
		// What follows the root is the parser's to refuse (a second root); the rest — comments, blank lines — is read out.
		while (Next().m_type != ibXmlNodeType_None) {}
		return root;
	}

private:
	// The next node that matters — a comment, an instruction, the declaration passed over; None at the end.
	ibXmlParser::ibNode Next()
	{
		for (;;) {
			std::deque<ibXmlParser::ibNode>& queue = m_parser.Queue();
			if (!queue.empty()) {
				ibXmlParser::ibNode node = std::move(queue.front());
				queue.pop_front();
				switch (node.m_type) {
				case ibXmlNodeType_Comment:
				case ibXmlNodeType_ProcessingInstruction:
				case ibXmlNodeType_XMLDeclaration:
				case ibXmlNodeType_DocumentTypeDefinition:
					continue;
				default:
					return node;
				}
			}
			if (!m_parser.Paused())
				return ibXmlParser::ibNode();
			m_parser.Resume(ibString());
		}
	}

	static bool IsText(const ibXmlParser::ibNode& node)
	{
		return node.m_type == ibXmlNodeType_Text || node.m_type == ibXmlNodeType_Whitespace
			|| node.m_type == ibXmlNodeType_CDATASection;
	}

	static bool AttributeOf(const ibXmlParser::ibNode& node, const ibChar* name, ibString& value)
	{
		for (const ibXmlParser::ibAttribute& attribute : node.m_attributeArray) {
			if (attribute.m_name == name) {
				value = attribute.m_value;
				return true;
			}
		}
		return false;
	}

	[[noreturn]] static void Misplaced(const ibXmlParser::ibNode& node, const ibString& where)
	{
		ibCoreException::Error(_("ibXmlProvider: '%s' does not belong in %s, line %llu"),
			node.m_type == ibXmlNodeType_StartElement ? node.m_name : node.m_value, where, node.m_line);
	}

	// The end of an element whose content was not finished: the document ends inside it.
	[[noreturn]] static void Unfinished(const ibXmlParser::ibNode& start)
	{
		ibCoreException::Error(_("ibXmlProvider: the '%s' of line %llu is not closed"), start.m_name, start.m_line);
	}

	ibDataNode ReadNode(const ibXmlParser::ibNode& start, int depth)
	{
		CheckDepth(depth, start.m_line);
		ibClassID clsid = 0;
		ibMetaID metaId = 0;
		ibString type, id;
		const bool typed = AttributeOf(start, ibT("Type"), type);
		if (typed) {
			unsigned long long number = 0;
			if (type.ToULongLong(&number))
				clsid = static_cast<ibClassID>(number);
			else if (m_lookupType)
				clsid = m_lookupType(type);
		}
		if (AttributeOf(start, ibT("Id"), id)) {
			long long number = 0;
			if (!id.ToLongLong(&number))
				ibCoreException::Error(_("ibXmlProvider: Id '%s' is no number, line %llu"), id, start.m_line);
			metaId = static_cast<ibMetaID>(number);
		}
		ibDataNode node(clsid, metaId);
		// A type nobody here knows by its name stays a word, for whoever can look it up (the JSON view's way).
		if (typed && clsid == 0)
			node.AddField(ibT("NodeType"), ibDataValue::String(type));

		bool children = false;
		for (;;) {
			const ibXmlParser::ibNode item = Next();
			if (item.m_type == ibXmlNodeType_EndElement)
				break;
			if (item.m_type == ibXmlNodeType_None)
				Unfinished(start);
			if (IsText(item)) {
				if (!IsBlank(item.m_value))
					Misplaced(item, ibT("a Node"));
				continue;
			}
			if (item.m_name == ibT("Field")) {
				ibString name;
				if (!AttributeOf(item, ibT("Name"), name))
					ibCoreException::Error(_("ibXmlProvider: a Field with no Name, line %llu"), item.m_line);
				node.AddField(name, ReadValue(item, depth + 1));
			}
			else if (item.m_name == ibT("Raw")) {
				ibMemoryBuffer raw;
				if (!ibBase64::Read(ReadText(item), raw))
					ibCoreException::Error(_("ibXmlProvider: Raw is no base64, line %llu"), item.m_line);
				node.SetRawData(raw);
			}
			else if (item.m_name == ibT("Children") && !children) {
				children = true;
				ReadChildren(item, node, depth);
			}
			else
				Misplaced(item, ibT("a Node"));
		}
		return node;
	}

	void ReadChildren(const ibXmlParser::ibNode& start, ibDataNode& node, int depth)
	{
		for (;;) {
			const ibXmlParser::ibNode item = Next();
			if (item.m_type == ibXmlNodeType_EndElement)
				return;
			if (item.m_type == ibXmlNodeType_None)
				Unfinished(start);
			if (IsText(item) && IsBlank(item.m_value))
				continue;
			if (item.m_type != ibXmlNodeType_StartElement || item.m_name != ibT("Node"))
				Misplaced(item, ibT("Children"));
			node.AddChild(ReadNode(item, depth + 1));
		}
	}

	// An element that holds text alone.
	ibString ReadText(const ibXmlParser::ibNode& start)
	{
		ibString text;
		for (;;) {
			const ibXmlParser::ibNode item = Next();
			if (item.m_type == ibXmlNodeType_EndElement)
				return text;
			if (item.m_type == ibXmlNodeType_None)
				Unfinished(start);
			if (!IsText(item))
				Misplaced(item, start.m_name);
			text += item.m_value;
		}
	}

	// A Field's or an Item's value, by its kind; no kind — a string, or the sub-node it holds.
	ibDataValue ReadValue(const ibXmlParser::ibNode& start, int depth)
	{
		CheckDepth(depth, start.m_line);
		ibString kind, encoding;
		AttributeOf(start, ibT("Kind"), kind);
		AttributeOf(start, ibT("Encoding"), encoding);
		const bool list = kind == ibT("Array");

		ibString text;
		std::vector<ibDataValue> items;
		std::shared_ptr<ibDataNode> child;
		for (;;) {
			const ibXmlParser::ibNode item = Next();
			if (item.m_type == ibXmlNodeType_EndElement)
				break;
			if (item.m_type == ibXmlNodeType_None)
				Unfinished(start);
			if (IsText(item)) {
				text += item.m_value;
				continue;
			}
			if (list && item.m_name == ibT("Item"))
				items.push_back(ReadValue(item, depth + 1));
			else if (kind.IsEmpty() && child == nullptr && item.m_name == ibT("Node"))
				child = std::make_shared<ibDataNode>(ReadNode(item, depth + 1));
			else
				Misplaced(item, start.m_name);
		}

		const auto refuse = [&](const ibString& what) {
			ibCoreException::Error(_("ibXmlProvider: '%s' is no %s, line %llu"), text, what, start.m_line);
		};
		if (list || child != nullptr) {
			if (!IsBlank(text))
				refuse(list ? ibT("list") : ibT("node"));
			return list ? ibDataValue::Array(std::move(items)) : ibDataValue::Child(child);
		}
		if (kind.IsEmpty())
			return ibDataValue::String(text);
		if (kind == ibT("String")) {
			if (encoding != ibT("base64"))
				return ibDataValue::String(text);
			ibMemoryBuffer utf8;
			if (!ibBase64::Read(text, utf8))
				refuse(ibT("base64"));
			return ibDataValue::String(ibString::FromUTF8(static_cast<const char*>(utf8.GetData()), utf8.GetDataLen()));
		}
		if (kind == ibT("Empty"))
			return ibDataValue();
		if (kind == ibT("Bool")) {
			if (text != ibT("true") && text != ibT("false"))
				refuse(ibT("Bool"));
			return ibDataValue::Bool(text == ibT("true"));
		}
		if (kind == ibT("Number")) {
			ibNumber number;
			if (!number.FromString(text))
				refuse(ibT("Number"));
			return ibDataValue::Number(number);
		}
		if (kind == ibT("Date")) {
			ibDateTime date;
			if (!text.IsEmpty() && !date.FromString(text))
				refuse(ibT("Date"));
			return ibDataValue::Date(date);
		}
		if (kind == ibT("Binary")) {
			ibMemoryBuffer bytes;
			if (!ibBase64::Read(text, bytes))
				refuse(ibT("base64"));
			return ibDataValue::Binary(bytes);
		}
		ibCoreException::Error(_("ibXmlProvider: no kind is called '%s', line %llu"), kind, start.m_line);
	}

	ibXmlParser& m_parser;
	const std::function<ibClassID(const ibString&)>& m_lookupType;
};

} // namespace

bool ibXmlProvider::Write(const ibDataNode& root, ibString& text) const
{
	ibXmlWriter writer(kWho, ibXmlCodePage::Find(ibT("UTF-8")));
	writer.WriteXmlDeclaration(ibT("UTF-8"));
	ibXmlTreeWriting(writer, m_resolveType).WriteNode(root, 1);
	writer.Finish();
	text = writer.Take();
	text += ibT("\n");
	return true;
}

bool ibXmlProvider::Write(const ibDataNode& root, ibWriterMemory& writer) const
{
	ibString text;
	Write(root, text);
	std::string bytes;
	ibXmlCodePage::Find(ibT("UTF-8")).Encode(text, kWho, bytes);
	writer.w(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
	return true;
}

bool ibXmlProvider::Read(const ibString& text, ibDataNode& root) const
{
	const std::string utf8 = text.ToUTF8();
	ibXmlParser parser(kWho, ibXmlParser::kCharacters);
	parser.Feed(utf8.data(), utf8.size(), true, ibString());
	root = ibXmlTreeReading(parser, m_lookupType).ReadRoot();
	return true;
}

bool ibXmlProvider::Read(ibReaderMemory& reader, ibDataNode& root) const
{
	const int size = reader.elapsed();
	if (size <= 0)
		return false;
	ibXmlParser parser(kWho, ibXmlParser::kBytes);
	parser.Feed(static_cast<const char*>(reader.pointer()), static_cast<size_t>(size), true, ibString());
	root = ibXmlTreeReading(parser, m_lookupType).ReadRoot();
	reader.advance(size);
	return true;
}
