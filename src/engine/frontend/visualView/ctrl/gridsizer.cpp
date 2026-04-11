
#include "sizer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueGridSizer, ibValueSizer)

//****************************************************************************
//*                             GridSizer                                    *
//****************************************************************************

ibValueGridSizer::ibValueGridSizer() : ibValueSizer()
{
}

wxObject* ibValueGridSizer::Create(wxWindow* /*parent*/, ibVisualHost* /*visualHost*/)
{
	return new wxGridSizer(m_propertyRows->GetValueAsUInteger(), m_propertyCols->GetValueAsUInteger(), 0, 0);
}

void ibValueGridSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost *visualHost, bool firstСreated)
{
}

void ibValueGridSizer::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
	wxGridSizer *gridsizer = dynamic_cast<wxGridSizer *>(wxobject);

	if (gridsizer != nullptr) {
		gridsizer->SetRows(m_propertyRows->GetValueAsUInteger());
		gridsizer->SetCols(m_propertyCols->GetValueAsUInteger());

		gridsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(gridsizer);
}

void ibValueGridSizer::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

bool ibValueGridSizer::LoadData(ibReaderMemory &reader)
{
	m_propertyRows->SetValue(reader.r_s32());
	m_propertyCols->SetValue(reader.r_s32());

	return ibValueSizer::LoadData(reader);
}

bool ibValueGridSizer::SaveData(ibWriterMemory writer)
{
	writer.w_s32(m_propertyRows->GetValueAsUInteger());
	writer.w_s32(m_propertyCols->GetValueAsUInteger());

	return ibValueSizer::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridSizer, "Gridsizer", "Sizer", string_to_clsid("CT_GSZR"));