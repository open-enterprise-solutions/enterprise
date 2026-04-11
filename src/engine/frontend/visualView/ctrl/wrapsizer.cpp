
#include "sizer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueWrapSizer, ibValueSizer)

//****************************************************************************
//*                             WrapSizer                                    *
//****************************************************************************

ibValueWrapSizer::ibValueWrapSizer() : ibValueSizer()
{
}

wxObject* ibValueWrapSizer::Create(wxWindow* /*parent*/, ibVisualHost* /*visualHost*/)
{
	return new wxWrapSizer(m_propertyOrient->GetValueAsInteger(), wxWRAPSIZER_DEFAULT_FLAGS);
}

void ibValueWrapSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstСreated)
{
}

void ibValueWrapSizer::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	wxWrapSizer *wrapsizer = dynamic_cast<wxWrapSizer *>(wxobject);

	if (wrapsizer) {
		wrapsizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
		wrapsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(wrapsizer);
}

void ibValueWrapSizer::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//**********************************************************************************
//*                            Data												   *
//**********************************************************************************

bool ibValueWrapSizer::LoadData(ibReaderMemory &reader)
{
	m_propertyOrient->SetValue(reader.r_u16());
	return ibValueSizer::LoadData(reader);
}

bool ibValueWrapSizer::SaveData(ibWriterMemory writer)
{
	writer.w_u16(m_propertyOrient->GetValueAsInteger());
	return ibValueSizer::SaveData(writer);
}