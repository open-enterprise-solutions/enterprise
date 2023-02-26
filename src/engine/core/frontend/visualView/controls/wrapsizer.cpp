
#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueWrapSizer, IValueSizer)

//****************************************************************************
//*                             WrapSizer                                    *
//****************************************************************************

CValueWrapSizer::CValueWrapSizer() : IValueSizer()
{
}

wxObject* CValueWrapSizer::Create(wxWindow* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxWrapSizer(m_propertyOrient->GetValueAsInteger(), wxWRAPSIZER_DEFAULT_FLAGS);
}

void CValueWrapSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueWrapSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxWrapSizer *wrapsizer = dynamic_cast<wxWrapSizer *>(wxobject);

	if (wrapsizer) {
		wrapsizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
		wrapsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(wrapsizer);
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