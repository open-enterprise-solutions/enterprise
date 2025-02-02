#include "sizers.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueSizer, IValueFrame)

//*******************************************************************
//*                            Control                              *
//*******************************************************************

void IValueSizer::UpdateSizer(wxSizer* sizer)
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



bool IValueSizer::LoadData(CMemoryReader& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyMinSize->SetValue(typeConv::StringToSize(propValue));
	reader.r_stringZ(propValue);

	return IValueFrame::LoadData(reader);
}

bool IValueSizer::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(
		m_propertyMinSize->GetValueAsString()
	);

	return IValueFrame::SaveData(writer);
}