#include "core/serialize/provider/xmlProvider.h"
#include "core/core.h"                       // kValueFieldClsid / kValueFieldData, the primitives' class ids
#include "core/xml/xmlText.h"                // ibXmlWriter / ibXmlCodePage — the engine's XML, written
#include "core/xml/xmlDocument.h"            // ibXmlDocument — …and read, whole
#include "core/utils/base64.h"               // ibBase64 — a binary's text and a string XML cannot hold
#include "core/exception.h"                  // ibCoreException — the refusals
#include "core/localization/dictionary.h"    // _() — their words

#include <memory>
#include <optional>
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

// The namespaces a value's type is said in.
const ibChar* const kXsiUri = ibT("http://www.w3.org/2001/XMLSchema-instance");
const ibChar* const kXsdUri = ibT("http://www.w3.org/2001/XMLSchema");

// ⭐ A VALUE'S NODE, told by what a value writes first (ibValue::Serialize): its type, field `t`, as the digits of a
// class id. Anything else with a field of that name - a number, a word, digits written another way, not first - is
// not a value's node, and is written field by field like any other.
ibClassID ValueTypeOf(const ibDataNode& node)
{
	const ibDataRange<std::pair<ibString, ibDataValue>> fields = node.Fields();
	if (fields.size() == 0)
		return 0;
	const std::pair<ibString, ibDataValue> first = fields.front();
	if (first.first != kValueFieldClsid || first.second.Kind() != ibDataKind::String)
		return 0;
	const ibString digits = first.second.AsString();
	unsigned long long clsid = 0;
	if (!digits.ToULongLong(&clsid) || clsid == 0 || ibString::Format(ibT("%llu"), clsid) != digits)
		return 0;
	return static_cast<ibClassID>(clsid);
}

// The four kinds XML Schema names itself, and the primitive each is.
struct ibSchemaType {
	ibClassID     m_clsid;
	ibDataKind    m_kind;
	const ibChar* m_local;
};
const ibSchemaType kSchemaTypeArray[] = {
	{ g_valueNumberCLSID,  ibDataKind::Number, ibT("decimal") },
	{ g_valueStringCLSID,  ibDataKind::String, ibT("string") },
	{ g_valueBooleanCLSID, ibDataKind::Bool,   ibT("boolean") },
	{ g_valueDateCLSID,    ibDataKind::Date,   ibT("dateTime") },
};

const ibSchemaType* SchemaTypeOf(ibClassID clsid)
{
	for (const ibSchemaType& type : kSchemaTypeArray)
		if (type.m_clsid == clsid)
			return &type;
	return nullptr;
}

// `yyyy-mm-ddThh:mm:ss`, a fraction of one to three digits after it, and nothing else: the form an xs:dateTime is
// written in here, and the only one read - no zone, no other order, no words for a date.
bool IsSchemaDateTime(const ibString& text)
{
	const ibChar* chars = text.c_str();
	const size_t length = text.Len();
	const char* const form = "dddd-dd-ddTdd:dd:dd";
	const size_t formLength = 19;
	if (length < formLength)
		return false;
	for (size_t i = 0; i < formLength; ++i) {
		const bool digit = chars[i] >= ibT('0') && chars[i] <= ibT('9');
		if (form[i] == 'd' ? !digit : chars[i] != static_cast<ibChar>(form[i]))
			return false;
	}
	if (length == formLength)
		return true;
	if (chars[formLength] != ibT('.') || length < formLength + 2 || length > formLength + 4)
		return false;
	for (size_t i = formLength + 1; i < length; ++i)
		if (chars[i] < ibT('0') || chars[i] > ibT('9'))
			return false;
	return true;
}

// A date's parts, for a text that is read back: four digits of a year, so the years 1 to 9999 and no others.
ibDateTimeParts PartsOfDate(const ibDateTime& date)
{
	ibDateTimeParts p;
	date.ToParts(p);
	if (p.m_year < 1 || p.m_year > 9999)
		ibCoreException::Error(_("ibXmlProvider: a date in the year %d has no text that reads back"), p.m_year);
	return p;
}

// A date as XML Schema writes one: `T` between the day and the time, the milliseconds when there are any. The empty
// date is the first day of the calendar, which is what it is.
ibString SchemaDateOf(const ibDateTime& date)
{
	const ibDateTimeParts p = PartsOfDate(date);
	ibString text = ibString::Format(ibT("%04d-%02u-%02uT%02u:%02u:%02u"), p.m_year, p.m_month, p.m_day, p.m_hour,
		p.m_minute, p.m_second);
	if (p.m_millisecond != 0)
		text += ibString::Format(ibT(".%03u"), p.m_millisecond);
	return text;
}

// THE TREE WRITTEN — a node an element, each entry a Field that says its kind (see the header).
class ibXmlTreeWriting {
public:
	ibXmlTreeWriting(ibXmlWriter& writer, const std::function<ibString(ibClassID)>& resolveType,
		const std::function<ibClassID(const ibString&)>& lookupType)
		: m_writer(writer), m_resolveType(resolveType), m_lookupType(lookupType) {}

	void WriteNode(const ibDataNode& node, int depth)
	{
		CheckDepth(depth, 0);
		m_writer.WriteStartElement(ibT("Node"), ibString());
		const ibClassID valueType = ValueTypeOf(node);
		// The node's own type: the value's (an element of a list) is said once, by xsi:type; none, on a value's node
		// (the root a value is packed into), is an empty Type - read without it, the node is of the value's type.
		if (valueType != 0 && node.GetClsid() == 0)
			m_writer.WriteAttribute(ibT("Type"), ibString(), ibString());
		else if (node.GetClsid() != 0 && node.GetClsid() != valueType)
			m_writer.WriteAttribute(ibT("Type"), ibString(), TypeName(node.GetClsid()));
		if (node.GetMetaId() != 0)
			m_writer.WriteAttribute(ibT("Id"), ibString(), ibString::Format(ibT("%d"), node.GetMetaId()));
		if (valueType != 0 && WriteValueType(node, valueType)) {
			m_writer.WriteEndElement();
			return;
		}
		bool skipType = valueType != 0;   // `t` is said by the attribute: the first field, not written again
		for (const auto& [name, value] : node.Fields()) {
			if (skipType) {
				skipType = false;
				continue;
			}
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
	// A type by its name, or by its number where the resolver names none.
	ibString TypeName(ibClassID clsid) const
	{
		const ibString name = m_resolveType ? m_resolveType(clsid) : ibString();
		return !name.IsEmpty() ? name : ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid));
	}

	// ⭐ A VALUE SAYS ITS TYPE BY XML'S OWN MEANS: `xsi:type` - XML Schema's word for a primitive (xs:decimal,
	// xs:string, xs:boolean, xs:dateTime), the type's name for the rest - and a Null `xsi:nil`. A primitive with
	// nothing but its payload is that payload, as the element's text: true when the node is written whole here.
	bool WriteValueType(const ibDataNode& node, ibClassID valueType)
	{
		// Both namespaces on the outermost value, which everything inside it then shares - xs whether or not anything
		// in it turns out a primitive: declared where one first needed it, every element of a list would carry its own.
		ibString prefix;
		if (!m_writer.LookupPrefix(kXsdUri, prefix)) {
			prefix = ibT("xs");
			m_writer.WriteNamespaceMapping(prefix, kXsdUri);
		}
		const ibDataRange<std::pair<ibString, ibDataValue>> fields = node.Fields();
		const bool alone = node.GetMetaId() == 0 && !node.HasRawData() && node.Children().empty()
			&& (node.GetClsid() == 0 || node.GetClsid() == valueType);
		if (valueType == g_valueNullCLSID && alone && fields.size() == 1) {
			m_writer.WriteAttribute(ibT("xsi:nil"), kXsiUri, ibT("true"));
			return true;
		}
		// XML Schema's word goes with the text alone: a primitive whose payload is not its text - none, of another
		// kind, beside other fields, a string XML cannot hold - is said by the type's name and written field by field.
		const ibSchemaType* const schema = SchemaTypeOf(valueType);
		const std::optional<ibString> text =
			schema != nullptr && alone && fields.size() == 2 ? SchemaTextOf(fields[1], *schema) : std::nullopt;
		if (!text) {
			m_writer.WriteAttribute(ibT("xsi:type"), kXsiUri, ValueTypeName(valueType));
			return false;
		}
		m_writer.WriteAttribute(ibT("xsi:type"), kXsiUri, prefix + ibT(":") + schema->m_local);
		if (!text->IsEmpty())
			m_writer.WriteText(*text);
		return true;
	}

	// A primitive's payload as XML Schema writes it; nothing - not one it can write as text.
	static std::optional<ibString> SchemaTextOf(const std::pair<ibString, ibDataValue>& payload, const ibSchemaType& schema)
	{
		const ibDataValue& value = payload.second;
		if (payload.first != kValueFieldData || value.Kind() != schema.m_kind)
			return std::nullopt;
		switch (value.Kind()) {
		case ibDataKind::Number:
			return value.AsNumber().ToString();
		case ibDataKind::Bool:
			return ibString(value.AsBool() ? ibT("true") : ibT("false"));
		case ibDataKind::Date:
			return SchemaDateOf(value.AsDate());
		case ibDataKind::String: {
			const ibString text = value.AsString();
			if (!HoldsXml(text))
				return std::nullopt;
			return text;
		}
		default:
			return std::nullopt;
		}
	}

	// A value's type by its name where that name reads back as the same type, by its number otherwise: a value's type
	// is its identity, and two types answering to one name would read back as the wrong one.
	ibString ValueTypeName(ibClassID clsid) const
	{
		const ibString name = m_resolveType ? m_resolveType(clsid) : ibString();
		if (!name.IsEmpty() && (!m_lookupType || NameReadsBackAs(name, clsid)))
			return name;
		return ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid));
	}

	// The question asked of the lookup is "is this the name of that type": one that refuses the name answers no, and
	// the type is written by its number - nothing is lost, the number is the type.
	bool NameReadsBackAs(const ibString& name, ibClassID clsid) const
	{
		try {
			return m_lookupType(name) == clsid;
		}
		catch (const ibCoreException&) {
			return false;
		}
	}

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
				const ibDateTimeParts p = PartsOfDate(date);
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
		case ibDataKind::Guid:
			Kind(ibT("Guid"));
			m_writer.WriteText(value.AsGuid().str());
			break;
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
	const std::function<ibClassID(const ibString&)>& m_lookupType;   // a value's type by name only where it reads back
};

// …AND READ BACK — walked over the document the core's parser read whole (ibXmlDocument: one road from XML text to a
// tree, the workbook reader's too); what is not of the layout is refused where it stands, by its line. An element left
// open is the parser's to refuse — the tree holds none.
class ibXmlTreeReading {
public:
	explicit ibXmlTreeReading(const std::function<ibClassID(const ibString&)>& lookupType) : m_lookupType(lookupType) {}

	ibDataNode ReadRoot(const ibXmlDocument& xml)
	{
		const ibXmlNode* const start = xml.GetRoot();
		if (start == nullptr)
			ibCoreException::Error(_("ibXmlProvider: the document holds no Node"));
		if (start->GetName() != ibT("Node"))
			Misplaced(*start, ibT("the document"));
		return ReadNode(*start, 1);
	}

private:
	// The node that matters from this one on — a comment, an instruction passed over; null at the end.
	static const ibXmlNode* Next(const ibXmlNode* node)
	{
		while (node != nullptr && (node->GetType() == ibXmlNodeType_Comment
			|| node->GetType() == ibXmlNodeType_ProcessingInstruction))
			node = node->GetNext();
		return node;
	}

	static bool IsText(const ibXmlNode& node)
	{
		return node.GetType() == ibXmlNodeType_Text || node.GetType() == ibXmlNodeType_Whitespace
			|| node.GetType() == ibXmlNodeType_CDATASection;
	}

	static bool AttributeOf(const ibXmlNode& node, const ibString& name, ibString& value)
	{
		if (!node.HasAttribute(name))
			return false;
		value = node.GetAttribute(name);
		return true;
	}

	[[noreturn]] static void Misplaced(const ibXmlNode& node, const ibString& where)
	{
		ibCoreException::Error(_("ibXmlProvider: '%s' does not belong in %s, line %llu"),
			node.GetType() == ibXmlNodeType_StartElement ? node.GetName() : node.GetContent(), where, node.GetLine());
	}

	ibDataNode ReadNode(const ibXmlNode& start, int depth)
	{
		CheckDepth(depth, start.GetLine());
		ibClassID clsid = 0;
		ibMetaID metaId = 0;
		ibString type, id;
		const bool typed = AttributeOf(start, m_typeName, type);
		if (typed && !type.IsEmpty()) {   // empty: no type of its own
			unsigned long long number = 0;
			if (type.ToULongLong(&number))
				clsid = static_cast<ibClassID>(number);
			else
				clsid = LookUp(type, start.GetLine());
		}
		if (AttributeOf(start, m_idName, id)) {
			long long number = 0;
			if (!id.ToLongLong(&number))
				ibCoreException::Error(_("ibXmlProvider: Id '%s' is no number, line %llu"), id, start.GetLine());
			metaId = static_cast<ibMetaID>(number);
		}

		// A value's type, said by xsi:type or xsi:nil, becomes its first field again, as the value writes it; with no
		// Type of its own the node is of the value's type.
		const ibSchemaType* schema = nullptr;
		const ibClassID valueType = ReadValueType(start, schema);
		ibDataNode node(typed ? clsid : valueType, metaId);
		if (valueType != 0)
			node.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(valueType)));
		// A type nobody here knows by its name stays a word, for whoever can look it up (the JSON view's way).
		if (typed && clsid == 0 && !type.IsEmpty())
			node.AddField(ibT("NodeType"), ibDataValue::String(type));

		// A primitive with nothing in it but text is that text.
		if (schema != nullptr && !HoldsElements(start)) {
			node.AddField(kValueFieldData, ReadSchemaText(start, *schema));
			return node;
		}

		bool children = false;
		for (const ibXmlNode* item = Next(start.GetChildren()); item != nullptr; item = Next(item->GetNext())) {
			if (IsText(*item)) {
				if (!IsBlank(item->GetContent()))
					Misplaced(*item, ibT("a Node"));
				continue;
			}
			if (item->GetName() == ibT("Field")) {
				ibString name;
				if (!AttributeOf(*item, m_fieldName, name))
					ibCoreException::Error(_("ibXmlProvider: a Field with no Name, line %llu"), item->GetLine());
				node.AddField(name, ReadValue(*item, depth + 1));
			}
			else if (item->GetName() == ibT("Raw")) {
				ibMemoryBuffer raw;
				if (!ibBase64::Read(ReadText(*item), raw))
					ibCoreException::Error(_("ibXmlProvider: Raw is no base64, line %llu"), item->GetLine());
				node.SetRawData(raw);
			}
			else if (item->GetName() == ibT("Children") && !children) {
				children = true;
				ReadChildren(*item, node, depth);
			}
			else
				Misplaced(*item, ibT("a Node"));
		}
		return node;
	}

	// xsi:nil="true" - a Null; xsi:type - XML Schema's primitive (`schema` then says which), or a type by its name or
	// its number. 0: neither is there.
	ibClassID ReadValueType(const ibXmlNode& start, const ibSchemaType*& schema) const
	{
		ibString word;
		if (AttributeOf(start, m_xsiNilName, word)) {
			CheckXsi(start);
			if (word == ibT("true") || word == ibT("1")) {
				// A Null is its type alone: nothing else said of it, nothing inside it.
				if (start.HasAttribute(m_xsiTypeName) || HoldsElements(start) || !IsBlank(ReadText(start)))
					ibCoreException::Error(_("ibXmlProvider: a node that is xsi:nil holds nothing and has no xsi:type, line %llu"),
						start.GetLine());
				return g_valueNullCLSID;
			}
			if (word != ibT("false") && word != ibT("0"))
				ibCoreException::Error(_("ibXmlProvider: xsi:nil '%s' is no boolean, line %llu"), word, start.GetLine());
		}
		if (!AttributeOf(start, m_xsiTypeName, word))
			return 0;
		CheckXsi(start);
		const int colon = word.Find(ibT(':'));
		if (colon >= 0) {
			const ibString prefix = word.Left(colon), local = word.Mid(colon + 1);
			if (NamespaceOf(start, prefix) != kXsdUri)
				ibCoreException::Error(_("ibXmlProvider: the prefix of the type '%s' does not stand for XML Schema, line %llu"),
					word, start.GetLine());
			for (const ibSchemaType& type : kSchemaTypeArray) {
				if (local == type.m_local) {
					schema = &type;
					return type.m_clsid;
				}
			}
			ibCoreException::Error(_("ibXmlProvider: the XML Schema type '%s' is not one a value is read as, line %llu"),
				word, start.GetLine());
		}
		// A number as the writer writes one - its digits and nothing else - or a name.
		unsigned long long number = 0;
		if (word.ToULongLong(&number) && number != 0
			&& ibString::Format(ibT("%llu"), number) == word)
			return static_cast<ibClassID>(number);
		const ibClassID clsid = LookUp(word, start.GetLine());
		if (clsid == 0)
			ibCoreException::Error(_("ibXmlProvider: no type is called '%s', line %llu"), word, start.GetLine());
		return clsid;
	}

	// A type by its name; a lookup that refuses the name in its own words is refused here, at the text's line.
	ibClassID LookUp(const ibString& name, unsigned long long line) const
	{
		if (!m_lookupType)
			return 0;
		try {
			return m_lookupType(name);
		}
		catch (const ibCoreException& error) {
			ibCoreException::Error(_("ibXmlProvider: %s, line %llu"), error.GetErrorDescription(), line);
		}
	}

	// The prefix xsi is read as written: it has to stand for XML Schema instance where it is used.
	void CheckXsi(const ibXmlNode& start) const
	{
		if (NamespaceOf(start, ibT("xsi")) != kXsiUri)
			ibCoreException::Error(_("ibXmlProvider: the prefix 'xsi' does not stand for XML Schema instance, line %llu"),
				start.GetLine());
	}

	// The namespace a prefix stands for at a node: its own declaration, or the nearest one around it.
	static ibString NamespaceOf(const ibXmlNode& start, const ibString& prefix)
	{
		const ibString declaration = ibString(ibT("xmlns:")) + prefix;
		for (const ibXmlNode* at = &start; at != nullptr; at = at->GetParent()) {
			ibString uri;
			if (AttributeOf(*at, declaration, uri))
				return uri;
		}
		return ibString();
	}

	static bool HoldsElements(const ibXmlNode& start)
	{
		for (const ibXmlNode* item = start.GetChildren(); item != nullptr; item = item->GetNext())
			if (item->GetType() == ibXmlNodeType_StartElement)
				return true;
		return false;
	}

	// A primitive's text, read as XML Schema writes it.
	ibDataValue ReadSchemaText(const ibXmlNode& start, const ibSchemaType& schema)
	{
		const ibString text = ReadText(start);
		const auto refuse = [&]() {
			ibCoreException::Error(_("ibXmlProvider: '%s' is no %s, line %llu"), text,
				ibString(ibT("xs:")) + schema.m_local, start.GetLine());
		};
		switch (schema.m_kind) {
		case ibDataKind::Number: {
			ibNumber number;
			if (!number.FromString(text))
				refuse();
			return ibDataValue::Number(number);
		}
		case ibDataKind::Bool:
			if (text == ibT("true") || text == ibT("1"))
				return ibDataValue::Bool(true);
			if (text != ibT("false") && text != ibT("0"))
				refuse();
			return ibDataValue::Bool(false);
		case ibDataKind::Date: {
			ibDateTime date;
			if (!IsSchemaDateTime(text) || !date.FromString(text))
				refuse();
			return ibDataValue::Date(date);
		}
		default:
			return ibDataValue::String(text);
		}
	}

	void ReadChildren(const ibXmlNode& start, ibDataNode& node, int depth)
	{
		for (const ibXmlNode* item = Next(start.GetChildren()); item != nullptr; item = Next(item->GetNext())) {
			if (IsText(*item) && IsBlank(item->GetContent()))
				continue;
			if (item->GetType() != ibXmlNodeType_StartElement || item->GetName() != ibT("Node"))
				Misplaced(*item, ibT("Children"));
			node.AddChild(ReadNode(*item, depth + 1));
		}
	}

	// An element that holds text alone.
	ibString ReadText(const ibXmlNode& start) const
	{
		ibString text;
		for (const ibXmlNode* item = Next(start.GetChildren()); item != nullptr; item = Next(item->GetNext())) {
			if (!IsText(*item))
				Misplaced(*item, start.GetName());
			text += item->GetContent();
		}
		return text;
	}

	// A Field's or an Item's value, by its kind; no kind — a string, or the sub-node it holds.
	ibDataValue ReadValue(const ibXmlNode& start, int depth)
	{
		CheckDepth(depth, start.GetLine());
		ibString kind, encoding;
		AttributeOf(start, m_kindName, kind);
		AttributeOf(start, m_encodingName, encoding);
		const bool list = kind == ibT("Array");

		ibString text;
		std::optional<std::vector<ibDataValue>> items;   // made for a list alone: an empty vector is not free
		if (list)
			items.emplace();
		std::shared_ptr<ibDataNode> child;
		for (const ibXmlNode* item = Next(start.GetChildren()); item != nullptr; item = Next(item->GetNext())) {
			if (IsText(*item)) {
				if (text.IsEmpty())
					text = item->GetContent();   // the one text a value holds is shared, not copied
				else
					text += item->GetContent();
				continue;
			}
			if (list && item->GetName() == ibT("Item"))
				items->push_back(ReadValue(*item, depth + 1));
			else if (kind.IsEmpty() && child == nullptr && item->GetName() == ibT("Node"))
				child = std::make_shared<ibDataNode>(ReadNode(*item, depth + 1));
			else
				Misplaced(*item, start.GetName());
		}

		const auto refuse = [&](const ibString& what) {
			ibCoreException::Error(_("ibXmlProvider: '%s' is no %s, line %llu"), text, what, start.GetLine());
		};
		if (list || child != nullptr) {
			if (!IsBlank(text))
				refuse(list ? ibT("list") : ibT("node"));
			return list ? ibDataValue::Array(std::move(*items)) : ibDataValue::Child(child);
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
		if (kind == ibT("Guid")) {
			ibGuid guid;
			if (!ibGuid::parse(text, guid))
				refuse(ibT("Guid"));
			return ibDataValue::Guid(guid);
		}
		if (kind == ibT("Binary")) {
			ibMemoryBuffer bytes;
			if (!ibBase64::Read(text, bytes))
				refuse(ibT("base64"));
			return ibDataValue::Binary(bytes);
		}
		ibCoreException::Error(_("ibXmlProvider: no kind is called '%s', line %llu"), kind, start.GetLine());
	}

	const std::function<ibClassID(const ibString&)>& m_lookupType;
	// The attributes' names, made once for the walk rather than for every lookup.
	const ibString m_typeName = ibT("Type"), m_idName = ibT("Id"), m_fieldName = ibT("Name"),
		m_kindName = ibT("Kind"), m_encodingName = ibT("Encoding"), m_xsiTypeName = ibT("xsi:type"),
		m_xsiNilName = ibT("xsi:nil");
};

} // namespace

bool ibXmlProvider::Write(const ibDataNode& root, ibString& text) const
{
	ibXmlWriter writer(kWho, ibXmlCodePage::Find(ibT("UTF-8")));
	writer.WriteXmlDeclaration(ibT("UTF-8"));
	ibXmlTreeWriting(writer, m_resolveType, m_lookupType).WriteNode(root, 1);
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
	ibXmlDocument xml;
	xml.Load(text);
	root = ibXmlTreeReading(m_lookupType).ReadRoot(xml);
	return true;
}

bool ibXmlProvider::Read(ibReaderMemory& reader, ibDataNode& root) const
{
	const int size = reader.elapsed();
	if (size <= 0)
		return false;
	ibXmlDocument xml;
	xml.Load(static_cast<const char*>(reader.pointer()), static_cast<size_t>(size));
	root = ibXmlTreeReading(m_lookupType).ReadRoot(xml);
	reader.advance(size);
	return true;
}
