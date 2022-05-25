#include "widgets.h"
#include "frontend/controls/checkBox.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueCheckbox, IValueWindow)

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"

ISourceDataObject* CValueCheckbox::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

//****************************************************************************
//*                              Checkbox                                    *
//****************************************************************************

CValueCheckbox::CValueCheckbox() : IValueWindow(), IAttributeControl(),
m_titleLocation(2)
{
	PropertyContainer* categoryCheckBox = IObjectBase::CreatePropertyContainer("Checkbox");
	//property
	categoryCheckBox->AddProperty("name", PropertyType::PT_WXNAME);
	categoryCheckBox->AddProperty("caption", PropertyType::PT_WXSTRING);
	categoryCheckBox->AddProperty("title_location", PropertyType::PT_OPTION, &CValueCheckbox::GetTitleLocation);

	//category 
	m_category->AddCategory(categoryCheckBox);

	PropertyContainer* propertySource = IObjectBase::CreatePropertyContainer("Data");
	propertySource->AddProperty("source", PropertyType::PT_SOURCE_DATA);
	//category 
	m_category->AddCategory(propertySource);

	//event
	PropertyContainer* categoryEvent = IObjectBase::CreatePropertyContainer("Events");
	categoryEvent->AddEvent("onCheckboxClicked", { { "control" } });
	m_category->AddCategory(categoryEvent);

	//default value 
	m_selValue = false;
}

wxObject* CValueCheckbox::Create(wxObject* parent, IVisualHost* visualHost)
{
	CCheckBox* checkbox = new CCheckBox((wxWindow*)parent, wxID_ANY,
		m_caption,
		m_pos,
		m_size);

	checkbox->BindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);

	return checkbox;
}

void CValueCheckbox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueCheckbox::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	CCheckBox* checkbox = dynamic_cast<CCheckBox*>(wxobject);

	if (checkbox) {

		wxString textCaption = wxEmptyString;

		if (m_dataSource != wxNOT_FOUND) {
			ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject) {
				IMetaObjectWrapperData* objMetaValue = srcObject->GetMetaObject();
				IMetaObject* metaObject = objMetaValue->FindMetaObjectByID(m_dataSource);
				if (metaObject) {
					textCaption = metaObject->GetSynonym() + wxT(":");
				}
				m_selValue = srcObject->GetValueByMetaID(m_dataSource);
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

void CValueCheckbox::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
	CCheckBox* checkbox = dynamic_cast<CCheckBox*>(obj);
	if (checkbox) {
		checkbox->UnbindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);
	}
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

CValue CValueCheckbox::GetControlValue() const
{
	CValueForm* ownerForm = GetOwnerForm();

	if (m_dataSource != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcObject = ownerForm->GetSourceObject();
		if (srcObject) {
			return srcObject->GetValueByMetaID(m_dataSource);
		}
	}

	return m_selValue;
}

#include "compiler/valueTypeDescription.h"
#include "frontend/controls/checkBox.h"

void CValueCheckbox::SetControlValue(CValue& vSelected)
{
	if (m_dataSource != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject) {
			srcObject->SetValueByMetaID(m_dataSource, vSelected);
		}
	}

	m_selValue = vSelected.GetBoolean();

	CCheckBox* checkboxCtrl = dynamic_cast<CCheckBox*>(GetWxObject());
	if (checkboxCtrl) {
		checkboxCtrl->SetCheckBoxValue(vSelected.GetBoolean());
	}
}

//*******************************************************************
//*							 Data	                                *
//*******************************************************************

bool CValueCheckbox::LoadData(CMemoryReader& reader)
{
	reader.r_stringZ(m_caption);
	m_titleLocation = reader.r_s32();

	if (!IAttributeControl::LoadData(reader))
		return false;

	return IValueWindow::LoadData(reader);
}

bool CValueCheckbox::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_caption);
	writer.w_s32(m_titleLocation);

	if (!IAttributeControl::SaveData(writer))
		return false;

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

	SaveToVariant(
		GetPropertyAsVariant("source"), GetMetaData()
	);
}

void CValueCheckbox::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("caption", m_caption);
	IObjectBase::GetPropertyValue("title_location", m_titleLocation);

	LoadFromVariant(
		GetPropertyAsVariant("source")
	);
}