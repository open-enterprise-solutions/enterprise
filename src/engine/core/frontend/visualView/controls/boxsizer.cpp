#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueBoxSizer, IValueSizer)

//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

CValueBoxSizer::CValueBoxSizer() : IValueSizer()
{
}

wxObject* CValueBoxSizer::Create(wxWindow* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxBoxSizer(m_propertyOrient->GetValueAsInteger());
}

void CValueBoxSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueBoxSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxBoxSizer *boxSizer = dynamic_cast<wxBoxSizer *>(wxobject);

	if (boxSizer != NULL) {
		boxSizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
		boxSizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(boxSizer);
}

void CValueBoxSizer::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//*******************************************************************
//*                            Data									*
//*******************************************************************

bool CValueBoxSizer::LoadData(CMemoryReader &reader)
{
	m_propertyOrient->SetValue(reader.r_u16());
	return IValueSizer::LoadData(reader);
}

bool CValueBoxSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_u16(m_propertyOrient->GetValueAsInteger());
	return IValueSizer::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_VALUE_REGISTER(CValueBoxSizer, "boxsizer", "sizer", TEXT2CLSID("CT_BSZR"));