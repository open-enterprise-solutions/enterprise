
#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGridSizer, IValueSizer)

//****************************************************************************
//*                             GridSizer                                    *
//****************************************************************************

CValueGridSizer::CValueGridSizer() : IValueSizer()
{
}

wxObject* CValueGridSizer::Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxGridSizer(m_propertyRows->GetValueAsInteger(), m_propertyCols->GetValueAsInteger(), 0, 0);
}

void CValueGridSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueGridSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxGridSizer *gridsizer = dynamic_cast<wxGridSizer *>(wxobject);

	if (gridsizer != NULL) {
		gridsizer->SetRows(m_propertyRows->GetValueAsInteger());
		gridsizer->SetCols(m_propertyCols->GetValueAsInteger());

		gridsizer->SetMinSize(m_propertyMinSize->GetValueAsSize());
	}

	UpdateSizer(gridsizer);
}

void CValueGridSizer::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

bool CValueGridSizer::LoadData(CMemoryReader &reader)
{
	m_propertyRows->SetValue(reader.r_s32());
	m_propertyCols->SetValue(reader.r_s32());

	return IValueSizer::LoadData(reader);
}

bool CValueGridSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_propertyRows->GetValueAsInteger());
	writer.w_s32(m_propertyCols->GetValueAsInteger());

	return IValueSizer::SaveData(writer);
}
