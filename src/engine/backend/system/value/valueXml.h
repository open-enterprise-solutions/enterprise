#ifndef __VALUE_XML_H__
#define __VALUE_XML_H__

#include "backend/compiler/value.h"

#include <map>
#include <vector>

void ibValueXmlWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);
void ibValueXmlReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// Writes XML into a string. SetString() starts that string. Close() returns it.
// FastInfoset is not this type: there is no Fast Infoset library in the engine.
class BACKEND_API ibValueXmlWriter : public ibValueStaticMembers<&ibValueXmlWriter_BindNames>
{
public:
	ibValueXmlWriter();
	virtual ~ibValueXmlWriter() {}

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	void SetString();
	void WriteXMLDeclaration();
	void WriteStartElement(const wxString& name);
	void WriteEndElement();
	void WriteAttribute(const wxString& name, const wxString& value);
	void WriteText(const wxString& text);
	wxString Close();

private:
	void RequireOpen(const wxString& verb) const;
	void FinishOpenTag();

	struct Frame {
		wxString name;
		bool open = false;
	};

	wxString m_out;
	std::vector<Frame> m_stack;
	bool m_started = false;
	bool m_declaration = false;
};

// Reads the XML a writer produced, one node at a time.
// NodeType is the word StartElement, EndElement or Text. There is no XMLNodeType enumeration.
class BACKEND_API ibValueXmlReader : public ibValueStaticMembers<&ibValueXmlReader_BindNames>
{
public:
	ibValueXmlReader();
	virtual ~ibValueXmlReader() {}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	void LoadString(const wxString& text);
	bool Read();
	wxString GetAttribute(const wxString& name) const;

	struct Token {
		wxString type;
		wxString name;
		wxString value;
		std::map<wxString, wxString> attrs;
	};

private:
	std::vector<Token> m_tokens;
	long m_index = 0;
	Token m_current;
};

#endif
