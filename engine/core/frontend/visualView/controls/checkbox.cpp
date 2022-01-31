#include "widgets.h"
#include "frontend/controls/checkBoxCtrl.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueCheckbox, IValueWindow)

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"

//****************************************************************************
//*                              Checkbox                                    *
//****************************************************************************

CValueCheckbox::CValueCheckbox() : IValueWindow(),
m_titleLocation(2), m_source(wxNOT_FOUND)
{
	PropertyContainer *categoryCheckBox = IObjectBase::CreatePropertyContainer("Checkbox");
	//property
	categoryCheckBox->AddProperty("name", PropertyType::PT_WXNAME);
	categoryCheckBox->AddProperty("caption", PropertyType::PT_WXSTRING);
	categoryCheckBox->AddProperty("title_location", PropertyType::PT_OPTION, &CValueCheckbox::GetTitleLocation);

	//category 
	m_category->AddCategory(categoryCheckBox);

	PropertyContainer *propertySource = IObjectBase::CreatePropertyContainer("Data");
	propertySource->AddProperty("source", PropertyType::PT_SOURCE);
	//category 
	m_category->AddCategory(propertySource);

	//event
	PropertyContainer *categoryEvent = IObjectBase::CreatePropertyContainer("Events");
	categoryEvent->AddEvent("onCheckboxClicked", { { "control" } });
	m_category->AddCategory(categoryEvent);

	//default value 
	m_selValue = false; 
}

wxObject* CValueCheckbox::Create(wxObject* parent, IVisualHost *visualHost)
{
	CCheckBox *checkbox = new CCheckBox((wxWindow *)parent, wxID_ANY,
		m_caption,
		m_pos,
		m_size);

	checkbox->BindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);

	return checkbox;
}

void CValueCheckbox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
}

void CValueCheckbox::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	CCheckBox *checkbox = dynamic_cast<CCheckBox *>(wxobject);

	if (checkbox) {

		wxString textCaption = wxEmptyString;

		if (m_source != wxNOT_FOUND) {
			IDataObjectSource *srcObject = m_formOwner->GetSourceObject();
			if (srcObject) {
				IMetaObjectValue *objMetaValue = srcObject->GetMetaObject();
				IMetaObject *metaObject = objMetaValue->FindMetaObjectByID(m_source);
				if (metaObject) {
					textCaption = metaObject->GetSynonym() + wxT(":");
				}
				m_selValue = srcObject->GetValueByMetaID(m_source);
			}
		}

		checkbox->SetCheckBoxLabel(m_caption.IsEmpty() ?
			textCaption : m_caption);

		checkbox->SetCheckBoxValue(m_selValue.GetBoolean());

		checkbox->SetWindowStyle(
			m_titleLocation == 1 ? wxALIGN_LEFT :
			wxALIGN_RIGHT
		);

		checkbox->BindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);
	}

	UpdateWindow(checkbox);
	UpdateLabelSize(checkbox);
}

void CValueCheckbox::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
	CCheckBox *checkbox = dynamic_cast<CCheckBox *>(obj);
	if (checkbox) {
		checkbox->UnbindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);
	}
}

//*******************************************************************
//*							 Data	                                *
//*******************************************************************

bool CValueCheckbox::LoadData(CMemoryReader &reader)
{
	reader.r_stringZ(m_caption);
	m_titleLocation = reader.r_s32();
	m_source = reader.r_s32();
	return IValueWindow::LoadData(reader);
}

bool CValueCheckbox::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_caption);
	writer.w_s32(m_titleLocation);
	writer.w_s32(m_source);
	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*							 Property                               *
//*******************************************************************

void CValueCheckbox::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("caption", m_caption);
	IObjectBase::SetPropertyValue("title_location", m_titleLocation);

	IObjectBase::SetPropertyValue("source", m_source);
}

void CValueCheckbox::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("caption", m_caption);
	IObjectBase::GetPropertyValue("title_location", m_titleLocation);

	IObjectBase::GetPropertyValue("source", m_source);
}