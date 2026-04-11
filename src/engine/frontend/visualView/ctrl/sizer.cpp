#include "sizer.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibValueSizer, ibValueFrame)

//*******************************************************************
//*                            Control                              *
//*******************************************************************

void ibValueSizer::UpdateSizer(wxSizer* sizer)
{
	if (sizer == nullptr)
		return;

	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		sizer->SetMinSize(m_propertyMinSize->GetValueAsSize());

	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		sizer->Layout();
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueSizer::LoadData(ibReaderMemory& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyMinSize->SetValue(typeConv::StringToSize(propValue));
	reader.r_stringZ(propValue);

	return ibValueFrame::LoadData(reader);
}

bool ibValueSizer::SaveData(ibWriterMemory writer)
{
	writer.w_stringZ(
		m_propertyMinSize->GetValueAsString()
	);

	return ibValueFrame::SaveData(writer);
}