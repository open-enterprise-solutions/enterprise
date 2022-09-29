
#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueWrapSizer, IValueSizer)

//****************************************************************************
//*                             WrapSizer                                    *
//****************************************************************************

CValueWrapSizer::CValueWrapSizer() : IValueSizer()
{
}

wxObject* CValueWrapSizer::Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxWrapSizer(m_propertyOrient->GetValueAsInteger(), wxWRAPSIZER_DEFAULT_FLAGS);
}

void CValueWrapSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueWrapSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxWrapSizer *m_wrapsizer = dynamic_cast<wxWrapSizer *>(wxobject);

	if (m_wrapsizer)
	{
		m_wrapsizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
		m_wrapsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(m_wrapsizer);
}

void CValueWrapSizer::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//**********************************************************************************
//*                            Data												   *
//**********************************************************************************

bool CValueWrapSizer::LoadData(CMemoryReader &reader)
{
	m_propertyOrient->SetValue(reader.r_u16());
	return IValueSizer::LoadData(reader);
}

bool CValueWrapSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_u16(m_propertyOrient->GetValueAsInteger());
	return IValueSizer::SaveData(writer);
}