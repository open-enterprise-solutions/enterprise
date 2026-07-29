#include "metaData.h"

wxString ibMetaData::Serialize(const ibValue& cValue)
{
	wxString strSerialize;
	if (cValue.Serialize(strSerialize)) {

		wxString str;

		str << wxT("S: OES Serialize;;;");
		str << wxT("C:") << cValue.GetClassType() << wxT(";;;");
		str << wxT("L:") << strSerialize.Length() << wxT(";;;");
		str << wxT("D:") << strSerialize << wxT(";;;");
		str << wxT("E: OES Serialize;;;");

		return str;
	}

	return wxEmptyString;
}

#include <wx/tokenzr.h>

ibValue ibMetaData::Deserialize(const wxString& strValue)
{
	ibValue createdValue;

	// Parse OES Serialize format:
	// S: OES Serialize;;;C:<classType>;;;L:<length>;;;D:<data>;;;E: OES Serialize;;;

	wxStringTokenizer tokenizer(strValue, wxT(";;;"));

	wxString header;      // "S: OES Serialize"
	wxString classStr;    // "C:<classType>"
	wxString lengthStr;   // "L:<length>"
	wxString dataStr;     // "D:<data>"

	if (tokenizer.HasMoreTokens()) header = tokenizer.GetNextToken();
	if (tokenizer.HasMoreTokens()) classStr = tokenizer.GetNextToken();
	if (tokenizer.HasMoreTokens()) lengthStr = tokenizer.GetNextToken();
	if (tokenizer.HasMoreTokens()) dataStr = tokenizer.GetNextToken();

	// Validate header
	if (header.Trim() != wxT("S: OES Serialize"))
		return wxEmptyValue;

	// Extract class type
	ibClassID classType = 0;
	if (classStr.StartsWith(wxT("C:"))) {
		wxString clsidStr = classStr.Mid(2);
		clsidStr.ToULongLong(&classType);
	}

	// Extract data
	wxString data;
	if (dataStr.StartsWith(wxT("D:")))
		data = dataStr.Mid(2);

	if (classType == 0 && data.IsEmpty())
		return wxEmptyValue;

	// Create value of the appropriate type and deserialize
	try {
		if (classType != 0)
			createdValue = ibMetaData::CreateObject(classType);
		else
			createdValue = ibMetaData::CreateObject(g_valueStringCLSID);
	}
	catch (...)
	{
		return wxEmptyValue;
	}

	if (createdValue.Deserialize(data))
		return createdValue;

	return wxEmptyValue;
}
