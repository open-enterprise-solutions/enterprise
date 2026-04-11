#include "sizer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueBoxSizer, ibValueSizer)

//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

ibValueBoxSizer::ibValueBoxSizer() : ibValueSizer()
{
}

wxObject* ibValueBoxSizer::Create(wxWindow* /*parent*/, ibVisualHost* /*visualHost*/)
{
	return new wxBoxSizer(m_propertyOrient->GetValueAsInteger());
}

void ibValueBoxSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstСreated)
{
}

void ibValueBoxSizer::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	wxBoxSizer *boxSizer = dynamic_cast<wxBoxSizer *>(wxobject);

	if (boxSizer != nullptr) {
		boxSizer->SetOrientation(m_propertyOrient->GetValueAsInteger());
		boxSizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(boxSizer);
}

void ibValueBoxSizer::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//*******************************************************************
//*                            Data									*
//*******************************************************************

bool ibValueBoxSizer::LoadData(ibReaderMemory &reader)
{
	m_propertyOrient->SetValue(reader.r_u16());
	return ibValueSizer::LoadData(reader);
}

bool ibValueBoxSizer::SaveData(ibWriterMemory writer)
{
	writer.w_u16(m_propertyOrient->GetValueAsInteger());
	return ibValueSizer::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueBoxSizer, "Boxsizer", "Sizer", string_to_clsid("CT_BSZR"));