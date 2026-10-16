#include "valueXml.h"

#include "backend/backend_exception.h"

#include <wx/xml/xml.h>
#include <wx/sstream.h>
#include <wx/log.h>

namespace {

wxString EscapeXml(const wxString& text)
{
	wxString out;
	out.Alloc(text.length());
	for (wxString::const_iterator it = text.begin(); it != text.end(); ++it) {
		const wxUniChar c = *it;
		if (c == wxT('&'))
			out += wxT("&amp;");
		else if (c == wxT('<'))
			out += wxT("&lt;");
		else if (c == wxT('>'))
			out += wxT("&gt;");
		else if (c == wxT('"'))
			out += wxT("&quot;");
		else
			out += c;
	}
	return out;
}

} // namespace

enum {
	eWriterSetString,
	eWriterDeclaration,
	eWriterStart,
	eWriterEnd,
	eWriterAttribute,
	eWriterText,
	eWriterClose
};

ibValueXmlWriter::ibValueXmlWriter()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

void ibValueXmlWriter::RequireOpen(const wxString& verb) const
{
	if (!m_started)
		ibBackendCoreException::Error(_("%s: SetString was not called"), verb);
}

void ibValueXmlWriter::FinishOpenTag()
{
	if (!m_stack.empty() && m_stack.back().open) {
		m_out += wxT(">");
		m_stack.back().open = false;
	}
}

void ibValueXmlWriter::SetString()
{
	m_out.clear();
	m_stack.clear();
	m_started = true;
	m_declaration = false;
}

void ibValueXmlWriter::WriteXMLDeclaration()
{
	RequireOpen(wxT("WriteXMLDeclaration"));
	if (m_declaration || !m_out.IsEmpty() || !m_stack.empty())
		ibBackendCoreException::Error(_("WriteXMLDeclaration: the document has already started"));
	m_out = wxT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
	m_declaration = true;
}

void ibValueXmlWriter::WriteStartElement(const wxString& name)
{
	RequireOpen(wxT("WriteStartElement"));
	if (name.IsEmpty())
		ibBackendCoreException::Error(_("WriteStartElement: the name is not given"));
	FinishOpenTag();
	m_out += wxT("<") + name;
	m_stack.push_back(Frame{ name, true });
}

void ibValueXmlWriter::WriteEndElement()
{
	RequireOpen(wxT("WriteEndElement"));
	if (m_stack.empty())
		ibBackendCoreException::Error(_("WriteEndElement: there is no open element"));
	const Frame frame = m_stack.back();
	m_stack.pop_back();
	if (frame.open)
		m_out += wxT("/>");
	else
		m_out += wxT("</") + frame.name + wxT(">");
}

void ibValueXmlWriter::WriteAttribute(const wxString& name, const wxString& value)
{
	RequireOpen(wxT("WriteAttribute"));
	if (m_stack.empty() || !m_stack.back().open)
		ibBackendCoreException::Error(_("WriteAttribute: the element is already closed"));
	if (name.IsEmpty())
		ibBackendCoreException::Error(_("WriteAttribute: the name is not given"));
	m_out += wxT(" ") + name + wxT("=\"") + EscapeXml(value) + wxT("\"");
}

void ibValueXmlWriter::WriteText(const wxString& text)
{
	RequireOpen(wxT("WriteText"));
	if (m_stack.empty())
		ibBackendCoreException::Error(_("WriteText: there is no open element"));
	FinishOpenTag();
	m_out += EscapeXml(text);
}

wxString ibValueXmlWriter::Close()
{
	RequireOpen(wxT("Close"));
	while (!m_stack.empty())
		WriteEndElement();
	m_started = false;
	return m_out;
}

void ibValueXmlWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProc(wxT("SetString"), wxT("SetString()"));
	helper.AppendProc(wxT("WriteXMLDeclaration"), wxT("WriteXMLDeclaration()"));
	helper.AppendProc(wxT("WriteStartElement"), 1, wxT("WriteStartElement(name)"));
	helper.AppendProc(wxT("WriteEndElement"), wxT("WriteEndElement()"));
	helper.AppendProc(wxT("WriteAttribute"), 2, wxT("WriteAttribute(name, value)"));
	helper.AppendProc(wxT("WriteText"), 1, wxT("WriteText(text)"));
	helper.AppendFunc(wxT("Close"), wxT("Close()"));
	helper.AppendConstructor(0, wxT("XMLWriter()"));
}

bool ibValueXmlWriter::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case eWriterSetString: SetString(); return true;
	case eWriterDeclaration: WriteXMLDeclaration(); return true;
	case eWriterStart:
		if (lSizeArray < 1 || paParams == nullptr || paParams[0] == nullptr)
			ibBackendCoreException::Error(_("WriteStartElement: the name is not given"));
		WriteStartElement(paParams[0]->GetString());
		return true;
	case eWriterEnd: WriteEndElement(); return true;
	case eWriterAttribute:
		if (lSizeArray < 2 || paParams == nullptr || paParams[0] == nullptr || paParams[1] == nullptr)
			ibBackendCoreException::Error(_("WriteAttribute: the name and the value are not given"));
		WriteAttribute(paParams[0]->GetString(), paParams[1]->GetString());
		return true;
	case eWriterText:
		if (lSizeArray < 1 || paParams == nullptr || paParams[0] == nullptr)
			ibBackendCoreException::Error(_("WriteText: the text is not given"));
		WriteText(paParams[0]->GetString());
		return true;
	case eWriterClose: Close(); return true;
	}
	return false;
}

bool ibValueXmlWriter::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != eWriterClose)
		return CallAsProc(lMethodNum, paParams, lSizeArray);
	pvarRetValue = Close();
	return true;
}

enum {
	eReaderNodeType,
	eReaderName,
	eReaderValue
};

enum {
	eReaderSetString,
	eReaderRead,
	eReaderGetAttribute
};

namespace {

void Collect(const wxXmlNode* node, std::vector<ibValueXmlReader::Token>& out)
{
	if (node == nullptr)
		return;
	if (node->GetType() == wxXML_ELEMENT_NODE) {
		ibValueXmlReader::Token start;
		start.type = wxT("StartElement");
		start.name = node->GetName();
		for (const wxXmlAttribute* attr = node->GetAttributes(); attr != nullptr; attr = attr->GetNext())
			start.attrs.emplace(attr->GetName(), attr->GetValue());
		out.push_back(std::move(start));
		for (const wxXmlNode* child = node->GetChildren(); child != nullptr; child = child->GetNext())
			Collect(child, out);
		ibValueXmlReader::Token end;
		end.type = wxT("EndElement");
		end.name = node->GetName();
		out.push_back(std::move(end));
		return;
	}
	if (node->GetType() == wxXML_TEXT_NODE || node->GetType() == wxXML_CDATA_SECTION_NODE) {
		if (node->GetContent().IsEmpty())
			return;
		ibValueXmlReader::Token text;
		text.type = wxT("Text");
		text.value = node->GetContent();
		out.push_back(std::move(text));
	}
}

} // namespace

ibValueXmlReader::ibValueXmlReader()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

void ibValueXmlReader::LoadString(const wxString& text)
{
	m_tokens.clear();
	m_index = 0;
	m_current = Token();
	wxStringInputStream in(text);
	wxXmlDocument xml;
	wxLogNull quiet;
	if (!xml.Load(in) || xml.GetRoot() == nullptr)
		ibBackendCoreException::Error(_("XMLReader: the text is not XML"));
	Collect(xml.GetRoot(), m_tokens);
}

bool ibValueXmlReader::Read()
{
	if (m_index >= static_cast<long>(m_tokens.size())) {
		m_current = Token();
		return false;
	}
	m_current = m_tokens[static_cast<size_t>(m_index++)];
	return true;
}

wxString ibValueXmlReader::GetAttribute(const wxString& name) const
{
	const auto found = m_current.attrs.find(name);
	if (found == m_current.attrs.end())
		return wxString();
	return found->second;
}

void ibValueXmlReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("NodeType"));
	helper.AppendProp(wxT("Name"));
	helper.AppendProp(wxT("Value"));
	helper.AppendProc(wxT("SetString"), 1, wxT("SetString(text)"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("GetAttribute"), 1, wxT("GetAttribute(name)"));
	helper.AppendConstructor(0, wxT("XMLReader()"));
}

bool ibValueXmlReader::SetPropVal(const long, const ibValue&)
{
	return false;
}

bool ibValueXmlReader::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case eReaderNodeType: pvarPropVal = m_current.type; return true;
	case eReaderName:     pvarPropVal = m_current.name; return true;
	case eReaderValue:    pvarPropVal = m_current.value; return true;
	}
	return false;
}

bool ibValueXmlReader::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != eReaderSetString)
		return false;
	if (lSizeArray < 1 || paParams == nullptr || paParams[0] == nullptr)
		ibBackendCoreException::Error(_("XMLReader: the text is not given"));
	LoadString(paParams[0]->GetString());
	return true;
}

bool ibValueXmlReader::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum == eReaderRead) {
		pvarRetValue = Read();
		return true;
	}
	if (lMethodNum == eReaderGetAttribute) {
		if (lSizeArray < 1 || paParams == nullptr || paParams[0] == nullptr)
			ibBackendCoreException::Error(_("GetAttribute: the name is not given"));
		pvarRetValue = GetAttribute(paParams[0]->GetString());
		return true;
	}
	return CallAsProc(lMethodNum, paParams, lSizeArray);
}

VALUE_TYPE_REGISTER(ibValueXmlWriter, "XMLWriter", value_to_clsid("VL_XMLW"));
VALUE_TYPE_REGISTER(ibValueXmlReader, "XMLReader", value_to_clsid("VL_XMLR"));
