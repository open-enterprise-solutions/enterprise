
#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGridSizer, IValueSizer)

//****************************************************************************
//*                             GridSizer                                    *
//****************************************************************************

CValueGridSizer::CValueGridSizer() : IValueSizer()
{
	PropertyContainer *categoryGridSizer = IObjectBase::CreatePropertyContainer("GridSizer");

	categoryGridSizer->AddProperty("name", PropertyType::PT_WXNAME);
	categoryGridSizer->AddProperty("row", PropertyType::PT_INT);
	categoryGridSizer->AddProperty("cols", PropertyType::PT_INT);

	m_category->AddCategory(categoryGridSizer);
}

wxObject* CValueGridSizer::Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxGridSizer(m_row, m_cols, 0, 0);
}

void CValueGridSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueGridSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxGridSizer *m_gridsizer = dynamic_cast<wxGridSizer *>(wxobject);

	if (m_gridsizer)
	{
		m_gridsizer->SetRows(m_row);
		m_gridsizer->SetCols(m_cols);

		m_gridsizer->SetMinSize(m_minimum_size);
	}

	UpdateSizer(m_gridsizer);
}

void CValueGridSizer::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

bool CValueGridSizer::LoadData(CMemoryReader &reader)
{
	m_row = reader.r_s32();
	m_cols = reader.r_s32();

	return IValueSizer::LoadData(reader);
}

bool CValueGridSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_row);
	writer.w_s32(m_cols);

	return IValueSizer::SaveData(writer);
}

void CValueGridSizer::ReadProperty()
{
	IValueSizer::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("row", m_row);
	IObjectBase::SetPropertyValue("cols", m_cols);
}

void CValueGridSizer::SaveProperty()
{
	IValueSizer::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("row", m_row);
	IObjectBase::GetPropertyValue("cols", m_cols);
}