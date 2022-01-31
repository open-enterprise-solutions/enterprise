
#include "sizers.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueWrapSizer, IValueSizer)

//****************************************************************************
//*                             WrapSizer                                    *
//****************************************************************************

CValueWrapSizer::CValueWrapSizer() : IValueSizer()
{
	PropertyContainer *categoryWrapSizer = IObjectBase::CreatePropertyContainer("WrapSizer");
	categoryWrapSizer->AddProperty("name", PropertyType::PT_WXNAME);
	categoryWrapSizer->AddProperty("orient", PropertyType::PT_OPTION, &CValueWrapSizer::GetOrient);

	m_category->AddCategory(categoryWrapSizer); 
}

wxObject* CValueWrapSizer::Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/)
{
	return new wxWrapSizer(m_orient, wxWRAPSIZER_DEFAULT_FLAGS);
}

void CValueWrapSizer::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueWrapSizer::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxWrapSizer *m_wrapsizer = dynamic_cast<wxWrapSizer *>(wxobject);

	if (m_wrapsizer)
	{
		m_wrapsizer->SetOrientation(m_orient);
		m_wrapsizer->SetMinSize(m_minimum_size);
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
	m_orient = (wxOrientation)reader.r_u16();
	return IValueSizer::LoadData(reader);
}

bool CValueWrapSizer::SaveData(CMemoryWriter &writer)
{
	writer.w_u16(m_orient);
	return IValueSizer::SaveData(writer);
}

//**********************************************************************************
//*                           Property                                             *
//**********************************************************************************

void CValueWrapSizer::ReadProperty()
{
	IValueSizer::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("orient", m_orient, true);
}

void CValueWrapSizer::SaveProperty()
{
	IValueSizer::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("orient", m_orient, true);
}