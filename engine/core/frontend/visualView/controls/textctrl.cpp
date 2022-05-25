#include "widgets.h"
#include "frontend/controls/textEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueTextCtrl, IValueWindow)

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

OptionList* CValueTextCtrl::GetChoiceForm(Property* property)
{
	OptionList* optList = new OptionList;
	optList->AddOption(_("default"), wxNOT_FOUND);

	IMetadata* metaData = GetMetaData();
	if (metaData != NULL) {
		IMetaObjectRecordDataRef* metaObject = NULL;
		if (m_dataSource != wxNOT_FOUND) {
			IMetaObjectWrapperData* metaObjectValue =
				m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				IMetaAttributeObject* metaAttribute = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
				);
				wxASSERT(metaAttribute);
				IMetaTypeObjectValueSingle *so = metaData->GetTypeObject(metaAttribute->GetFirstClsid()); 
				if (so != NULL) {
					metaObject = wxDynamicCast(so->GetMetaObject(), IMetaObjectRecordDataRef);
				}
			}
		}
		else {
			IMetaTypeObjectValueSingle* so = metaData->GetTypeObject(IAttributeControl::GetFirstClsid());
			if (so != NULL) {
				metaObject = wxDynamicCast(so->GetMetaObject(), IMetaObjectRecordDataRef);
			}
		}

		if (metaObject != NULL) {
			for (auto form : metaObject->GetObjectForms()) {
				optList->AddOption(
					form->GetSynonym(),
					form->GetMetaID()
				);
			}
		}
	}

	return optList;
}

ISourceDataObject* CValueTextCtrl::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

//****************************************************************************
//*                              TextCtrl                                    *
//****************************************************************************

CValueTextCtrl::CValueTextCtrl() : IValueWindow(), IAttributeControl(),
m_selbutton(true), m_listbutton(false), m_clearbutton(true),
m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
m_choiceForm(wxNOT_FOUND)
{
	PropertyContainer* categoryText = IObjectBase::CreatePropertyContainer("TextControl");

	//property
	categoryText->AddProperty("name", PropertyType::PT_WXNAME);
	categoryText->AddProperty("caption", PropertyType::PT_WXSTRING);

	categoryText->AddProperty("password_mode", PropertyType::PT_BOOL);
	categoryText->AddProperty("multiline_mode", PropertyType::PT_BOOL);
	categoryText->AddProperty("textedit_mode", PropertyType::PT_BOOL);

	//category 
	m_category->AddCategory(categoryText);

	//source from object 
	PropertyContainer* propertySource = IObjectBase::CreatePropertyContainer("Data");
	propertySource->AddProperty("source", PropertyType::PT_SOURCE_DATA);
	propertySource->AddProperty("choice_form", PropertyType::PT_OPTION, &CValueTextCtrl::GetChoiceForm);

	m_category->AddCategory(propertySource);

	PropertyContainer* categoryButton = IObjectBase::CreatePropertyContainer("Button");
	categoryButton->AddProperty("button_select", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_list", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_clear", PropertyType::PT_BOOL);

	//category 
	m_category->AddCategory(categoryButton);

	//category 
	PropertyContainer* propertyEvents = IObjectBase::CreatePropertyContainer("Events");

	//default events
	propertyEvents->AddEvent("onChange", { "control" });
	propertyEvents->AddEvent("startChoice", { "control", "standartProcessing" });
	propertyEvents->AddEvent("startListChoice", { "control", "standartProcessing" });
	propertyEvents->AddEvent("clearing", { "control", "standartProcessing" });
	propertyEvents->AddEvent("opening", { "control", "standartProcessing" });
	propertyEvents->AddEvent("clearing", { "control", "standartProcessing" });
	propertyEvents->AddEvent("choiceProcessing", { "control", "valueSelected", "standartProcessing" });

	m_category->AddCategory(propertyEvents);
}

#include "metadata/singleMetaTypes.h"

wxObject* CValueTextCtrl::Create(wxObject* parent, IVisualHost* visualHost)
{
	CTextCtrl* textCtrl = new CTextCtrl((wxWindow*)parent, wxID_ANY,
		m_selValue.GetString(),
		m_pos,
		m_size,
		m_style | m_window_style);

	if (m_dataSource != wxNOT_FOUND) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject) {
			IMetaObject* metaObject = GetMetaSource();
			wxASSERT(metaObject);
			m_selValue = srcObject->GetValueByMetaID(m_dataSource);
		}
	}
	else {

		m_selValue = IAttributeControl::CreateValue();
	}

	return textCtrl;
}

void CValueTextCtrl::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstСreated)
{
}

#include "appData.h"

void CValueTextCtrl::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	CTextCtrl* textCtrl = dynamic_cast<CTextCtrl*>(wxobject);

	if (textCtrl) {
		wxString textCaption = wxEmptyString;
		if (m_dataSource != wxNOT_FOUND) {
			IMetaObject* metaObject = GetMetaSource();
			if (metaObject) {
				textCaption = metaObject->GetSynonym() + wxT(":");
			}
		}

		textCtrl->SetTextLabel(m_caption.IsEmpty() ?
			textCaption : m_caption);

		if (m_dataSource != wxNOT_FOUND) {
			ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject) {
				IMetaObject* metaObject = GetMetaSource();
				wxASSERT(metaObject);
				m_selValue = srcObject->GetValueByMetaID(m_dataSource);
			}
		}

		if (!appData->DesignerMode()) {
			textCtrl->SetTextValue(m_selValue.GetString());
		}

		textCtrl->SetPasswordMode(m_passwordMode);
		textCtrl->SetMultilineMode(m_multilineMode);
		textCtrl->SetTextEditMode(m_textEditMode);

		if (!appData->DesignerMode()) {
			textCtrl->SetButtonSelect(m_selbutton);
			textCtrl->BindButtonSelect(&CValueTextCtrl::OnSelectButtonPressed, this);
			textCtrl->SetButtonList(m_listbutton);
			textCtrl->BindButtonList(&CValueTextCtrl::OnListButtonPressed, this);
			textCtrl->SetButtonClear(m_clearbutton);
			textCtrl->BindButtonClear(&CValueTextCtrl::OnClearButtonPressed, this);

			textCtrl->BindTextCtrl(&CValueTextCtrl::OnTextEnter, this);
			textCtrl->BindKillFocus(&CValueTextCtrl::OnKillFocus, this);
		}
		else {
			textCtrl->SetButtonSelect(m_selbutton);
			textCtrl->SetButtonList(m_listbutton);
			textCtrl->SetButtonClear(m_clearbutton);
		}
	}

	UpdateWindow(textCtrl);
	UpdateLabelSize(textCtrl);
}

void CValueTextCtrl::Cleanup(wxObject* wxobject, IVisualHost* visualHost)
{
	CTextCtrl* textCtrl = dynamic_cast<CTextCtrl*>(wxobject);

	if (textCtrl) {
		if (!appData->DesignerMode()) {
			textCtrl->UnbindButtonSelect(&CValueTextCtrl::OnSelectButtonPressed, this);
			textCtrl->UnbindButtonList(&CValueTextCtrl::OnListButtonPressed, this);
			textCtrl->UnbindButtonClear(&CValueTextCtrl::OnClearButtonPressed, this);

			textCtrl->UnbindTextCtrl(&CValueTextCtrl::OnTextEnter, this);
			textCtrl->UnbindKillFocus(&CValueTextCtrl::OnKillFocus, this);
		}
	}
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

CValue CValueTextCtrl::GetControlValue() const
{
	CValueForm* ownerForm = GetOwnerForm();

	if (m_dataSource != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcObject = ownerForm->GetSourceObject();
		if (srcObject != NULL) {
			return srcObject->GetValueByMetaID(m_dataSource);
		}
	}

	return m_selValue;
}

void CValueTextCtrl::SetControlValue(CValue& vSelected)
{
	if (m_dataSource != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IMetaAttributeObject* metaObject = wxDynamicCast(
			GetMetaSource(), IMetaAttributeObject
		);
		wxASSERT(metaObject);
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != NULL) {
			srcObject->SetValueByMetaID(m_dataSource, vSelected);
		}
		m_selValue = metaObject->AdjustValue(vSelected);
	}
	else {
		m_selValue = IAttributeInfo::AdjustValue(vSelected);
	}

	CTextCtrl* textCtrl = dynamic_cast<CTextCtrl*>(GetWxObject());
	if (textCtrl != NULL) {
		textCtrl->SetTextValue(m_selValue.GetString());
	}
}

//*******************************************************************
//*                            Data		                            *
//*******************************************************************

bool CValueTextCtrl::LoadData(CMemoryReader& reader)
{
	reader.r_stringZ(m_caption);

	m_passwordMode = reader.r_u8();
	m_multilineMode = reader.r_u8();
	m_textEditMode = reader.r_u8();

	m_selbutton = reader.r_u8();
	m_listbutton = reader.r_u8();
	m_clearbutton = reader.r_u8();

	m_choiceForm = reader.r_s32();

	if (!IAttributeControl::LoadData(reader))
		return false;

	return IValueWindow::LoadData(reader);
}

bool CValueTextCtrl::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_caption);

	writer.w_u8(m_passwordMode);
	writer.w_u8(m_multilineMode);
	writer.w_u8(m_textEditMode);

	writer.w_u8(m_selbutton);
	writer.w_u8(m_listbutton);
	writer.w_u8(m_clearbutton);

	writer.w_s32(m_choiceForm);

	if (!IAttributeControl::SaveData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}

//*******************************************************************
//*                            Property                             *
//*******************************************************************

void CValueTextCtrl::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("caption", m_caption);

	IObjectBase::SetPropertyValue("password_mode", m_passwordMode);
	IObjectBase::SetPropertyValue("multiline_mode", m_multilineMode);
	IObjectBase::SetPropertyValue("textedit_mode", m_textEditMode);

	IObjectBase::SetPropertyValue("button_select", m_selbutton);
	IObjectBase::SetPropertyValue("button_list", m_listbutton);
	IObjectBase::SetPropertyValue("button_clear", m_clearbutton);

	IObjectBase::SetPropertyValue("choice_form", m_choiceForm);

	SaveToVariant(
		GetPropertyAsVariant("source"), GetMetaData()
	);
}

void CValueTextCtrl::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("caption", m_caption);

	IObjectBase::GetPropertyValue("password_mode", m_passwordMode);
	IObjectBase::GetPropertyValue("multiline_mode", m_multilineMode);
	IObjectBase::GetPropertyValue("textedit_mode", m_textEditMode);

	IObjectBase::GetPropertyValue("button_select", m_selbutton);
	IObjectBase::GetPropertyValue("button_list", m_listbutton);
	IObjectBase::GetPropertyValue("button_clear", m_clearbutton);

	IObjectBase::GetPropertyValue("choice_form", m_choiceForm);

	LoadFromVariant(
		GetPropertyAsVariant("source")
	);
}
