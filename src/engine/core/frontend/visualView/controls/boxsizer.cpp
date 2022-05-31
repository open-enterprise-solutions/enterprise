
#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueBoxSizer, IValueSizer)

//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

CValueBoxSizer::CValueBoxSizer() : IValueSizer(), m_orient(wxVERTICAL), m_flags(0)
{
	PropertyContainer *boxsizerCategory = IObjectBase::CreatePropertyContainer("BoxSizer"); 
	boxsizerCategory->AddProperty("name", PropertyType::PT_WXNAME);
	boxsizerCategory->AddProperty("orient", PropertyType::PT_OPTION, &CValueBoxSizer::GetOrient);

	m_category->AddCategory(boxsizerCategory);
}

wxObject* CValueBoxSizer::Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxBoxSizer(m_orient);
}

void CValueBoxSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueBoxSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxBoxSizer *m_boxsizer = dynamic_cast<wxBoxSizer *>(wxobject);

	if (m_boxsizer) {
		m_boxsizer->SetOrientation(m_orient);
		m_boxsizer->SetMinSize(m_minimum_size);
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
	m_orient = (wxOrientation)reader.r_u16();
	return IValueSizer::LoadData(reader);
}

bool CValueBoxSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_u16(m_orient);
	return IValueSizer::SaveData(writer);
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

void CValueBoxSizer::ReadProperty()
{
	IValueSizer::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("orient", m_orient, true);
}

void CValueBoxSizer::SaveProperty()
{
	IValueSizer::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("orient", m_orient, true);
}