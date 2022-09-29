#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueBoxSizer, IValueSizer)

//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

CValueBoxSizer::CValueBoxSizer() : IValueSizer()
{
}

wxObject* CValueBoxSizer::Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxBoxSizer(m_propertyOrient->GetValueAsInteger());
}

void CValueBoxSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueBoxSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxBoxSizer *m_boxsizer = dynamic_cast<wxBoxSizer *>(wxobject);

	if (m_boxsizer) {
		m_boxsizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
		m_boxsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(m_boxsizer);
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