#include "sizers.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueSizer, IValueFrame)

//*******************************************************************
//*                            Control                              *
//*******************************************************************

void IValueSizer::UpdateSizer(wxSizer* sizer)
{
	if (!sizer) return;

	if (m_minimum_size != wxDefaultSize) sizer->SetMinSize(m_minimum_size);
	if (m_minimum_size != wxDefaultSize) sizer->Layout();
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

#include "utils/typeconv.h"

bool IValueSizer::LoadData(CMemoryReader &reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_minimum_size = TypeConv::StringToSize(propValue);
	reader.r_stringZ(propValue);

	return IValueFrame::LoadData(reader);
}

bool IValueSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(
		TypeConv::SizeToString(m_minimum_size)
	);

	return IValueFrame::SaveData(writer);
}

//*******************************************************************
//*                             Property                            *
//*******************************************************************

void IValueSizer::ReadProperty()
{
	IValueFrame::ReadProperty();
	IObjectBase::SetPropertyValue("minimum_size", m_minimum_size);

	//if we have sizerItem then call him readpropery 
	IValueFrame *m_sizeritem = GetParent();
	if (m_sizeritem && m_sizeritem->GetClassName() == wxT("sizerItem"))
	{
		m_sizeritem->ReadProperty();
	}
}

void IValueSizer::SaveProperty()
{
	IValueFrame::SaveProperty();
	IObjectBase::GetPropertyValue("minimum_size", m_minimum_size);

	//if we have sizerItem then call him savepropery 
	IValueFrame *m_sizeritem = GetParent();
	if (m_sizeritem && m_sizeritem->GetClassName() == wxT("sizerItem"))
	{
		m_sizeritem->SaveProperty();
	}
}