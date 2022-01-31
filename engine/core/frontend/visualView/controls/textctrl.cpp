#include "widgets.h"
#include "frontend/controls/textCtrl.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueTextCtrl, IValueWindow)

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"

OptionList *CValueTextCtrl::GetChoiceForm(Property *property)
{
	OptionList *optList = new OptionList;
	optList->AddOption(_("default"), wxNOT_FOUND);

	IMetadata *metaData = GetMetaData();
	if (metaData) {	
		IMetaObjectRefValue* metaObject = NULL; 

		if (m_source != wxNOT_FOUND) {
			IMetaObjectValue *metaObjectValue = 
				m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				IMetaAttributeObject *metaAttribute = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_source), IMetaAttributeObject
				);
				wxASSERT(metaAttribute);
				metaObject = wxDynamicCast(
					metaData->GetMetaObject(metaAttribute->GetTypeObject()), IMetaObjectRefValue);
			}
		}
		else {
			metaObject = wxDynamicCast(
				metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectRefValue);
		}

		if (metaObject) {
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

//****************************************************************************
//*                              TextCtrl                                    *
//****************************************************************************

CValueTextCtrl::CValueTextCtrl() : IValueWindow(), IAttributeInfo(eValueTypes::TYPE_STRING), 
m_selbutton(true), m_listbutton(false), m_clearbutton(false),
m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
m_source(wxNOT_FOUND), m_choiceForm(wxNOT_FOUND)
{
	PropertyContainer *categoryText = IObjectBase::CreatePropertyContainer("TextControl");

	//property
	categoryText->AddProperty("name", PropertyType::PT_WXNAME);
	categoryText->AddProperty("caption", PropertyType::PT_WXSTRING);

	categoryText->AddProperty("password_mode", PropertyType::PT_BOOL);
	categoryText->AddProperty("multiline_mode", PropertyType::PT_BOOL);
	categoryText->AddProperty("textedit_mode", PropertyType::PT_BOOL);

	//category 
	m_category->AddCategory(categoryText);

	//source from object 
	PropertyContainer *propertySource = IObjectBase::CreatePropertyContainer("Data");
	propertySource->AddProperty("source", PropertyType::PT_SOURCE);
	
	propertySource->AddProperty("type", PropertyType::PT_TYPE_SELECT);
	propertySource->AddProperty("precision", PropertyType::PT_UINT);
	propertySource->AddProperty("scale", PropertyType::PT_UINT);
	propertySource->AddProperty("date_time", PropertyType::PT_OPTION, &CValueTextCtrl::GetDateTimeFormat);
	propertySource->AddProperty("length", PropertyType::PT_UINT);

	propertySource->AddProperty("choice_form", PropertyType::PT_OPTION, &CValueTextCtrl::GetChoiceForm);

	m_category->AddCategory(propertySource);

	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Button");
	categoryButton->AddProperty("button_select", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_list", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_clear", PropertyType::PT_BOOL);

	//category 
	m_category->AddCategory(categoryButton);

	//category 
	PropertyContainer *propertyEvents = IObjectBase::CreatePropertyContainer("Events");

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

wxObject* CValueTextCtrl::Create(wxObject* parent, IVisualHost *visualHost)
{
	CTextCtrl *textCtrl = new CTextCtrl((wxWindow *)parent, wxID_ANY,
		m_selValue.GetString(),
		m_pos,
		m_size,
		m_style | m_window_style);

	if (m_source != wxNOT_FOUND) {
		IDataObjectSource *srcObject = m_formOwner->GetSourceObject();
		if (srcObject) {
			IMetaObjectValue *objMetaValue = srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			IMetaObject *metaObject = objMetaValue->FindMetaObjectByID(m_source);
			wxASSERT(metaObject);
			m_selValue = srcObject->GetValueByMetaID(m_source);
		}
	}
	else {
		IMetadata *metaData = GetMetaData();
		if (metaData) {
			CLASS_ID clsid = 0;
			if (m_typeDescription.GetTypeObject() >= defaultMetaID) {
				IMetaObjectValue *metaObject = wxDynamicCast(
					metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectValue
				);
				wxASSERT(metaObject);

				IMetaTypeObjectValueSingle *clsFactory =
					metaObject->GetTypeObject(eMetaObjectType::enReference);
				wxASSERT(clsFactory);
				clsid = clsFactory->GetTypeID();
			}
			else {
				clsid = CValue::GetIDByVT((const eValueTypes)m_typeDescription.GetTypeObject());
			}
			wxASSERT(clsid > 0);
			wxString className = metaData->GetNameObjectFromID(clsid);
			m_selValue = metaData->CreateObject(className);
		}
	}

	return textCtrl;
}

void CValueTextCtrl::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstСreated)
{
}

#include "wx/valnum.h"

void CValueTextCtrl::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	CTextCtrl *textCtrl = dynamic_cast<CTextCtrl *>(wxobject);

	if (textCtrl) {
		wxString textCaption = wxEmptyString;
		if (m_source != wxNOT_FOUND) {
			IDataObjectSource *srcObject = m_formOwner->GetSourceObject();
			if (srcObject) {
				IMetaObjectValue *objMetaValue = srcObject->GetMetaObject();
				IMetaObject *metaObject = objMetaValue->FindMetaObjectByID(m_source);
				if (metaObject) {
					textCaption = metaObject->GetSynonym() + wxT(":");
				}
			}
		}

		textCtrl->SetTextLabel(m_caption.IsEmpty() ?
			textCaption : m_caption);

		if (m_source != wxNOT_FOUND) {
			IDataObjectSource *srcObject = m_formOwner->GetSourceObject();
			if (srcObject) {
				IMetaObjectValue *objMetaValue = srcObject->GetMetaObject();
				wxASSERT(objMetaValue);
				IMetaObject *metaObject = objMetaValue->FindMetaObjectByID(m_source);
				wxASSERT(metaObject);
				m_selValue = srcObject->GetValueByMetaID(m_source);
			}
		}

		textCtrl->SetTextValue(m_selValue.GetString());

		textCtrl->SetPasswordMode(m_passwordMode);
		textCtrl->SetMultilineMode(m_multilineMode);
		textCtrl->SetTextEditMode(m_textEditMode);

		textCtrl->SetButtonSelect(m_selbutton);
		textCtrl->BindButtonSelect(&CValueTextCtrl::OnSelectButtonPressed, this);
		textCtrl->SetButtonList(m_listbutton);
		textCtrl->BindButtonList(&CValueTextCtrl::OnListButtonPressed, this);
		textCtrl->SetButtonClear(m_clearbutton);
		textCtrl->BindButtonClear(&CValueTextCtrl::OnClearButtonPressed, this);

		textCtrl->BindTextCtrl(&CValueTextCtrl::OnTextEnter, this);
		textCtrl->BindKillFocus(&CValueTextCtrl::OnKillFocus, this);
	}

	UpdateWindow(textCtrl);
	UpdateLabelSize(textCtrl);
}

void CValueTextCtrl::Cleanup(wxObject* wxobject, IVisualHost *visualHost)
{
	CTextCtrl *textCtrl = dynamic_cast<CTextCtrl *>(wxobject);

	if (textCtrl) {
		textCtrl->UnbindButtonSelect(&CValueTextCtrl::OnSelectButtonPressed, this);
		textCtrl->UnbindButtonList(&CValueTextCtrl::OnListButtonPressed, this);
		textCtrl->UnbindButtonClear(&CValueTextCtrl::OnClearButtonPressed, this);

		textCtrl->UnbindTextCtrl(&CValueTextCtrl::OnTextEnter, this);
		textCtrl->UnbindKillFocus(&CValueTextCtrl::OnKillFocus, this);
	}
}

//*******************************************************************
//*                            Data		                            *
//*******************************************************************

bool CValueTextCtrl::LoadData(CMemoryReader &reader)
{
	reader.r_stringZ(m_caption);

	m_passwordMode = reader.r_u8();
	m_multilineMode = reader.r_u8();
	m_textEditMode = reader.r_u8();

	m_selbutton = reader.r_u8();
	m_listbutton = reader.r_u8();
	m_clearbutton = reader.r_u8();

	m_source = reader.r_s32();
	m_choiceForm = reader.r_s32();

	reader.r(&m_typeDescription, sizeof(typeDescription_t));
	return IValueWindow::LoadData(reader);
}

bool CValueTextCtrl::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_caption);

	writer.w_u8(m_passwordMode);
	writer.w_u8(m_multilineMode);
	writer.w_u8(m_textEditMode);

	writer.w_u8(m_selbutton);
	writer.w_u8(m_listbutton);
	writer.w_u8(m_clearbutton);

	writer.w_s32(m_source);
	writer.w_s32(m_choiceForm);

	writer.w(&m_typeDescription, sizeof(typeDescription_t));
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

	IObjectBase::SetPropertyValue("source", m_source);
	IObjectBase::SetPropertyValue("choice_form", m_choiceForm);

	IObjectBase::SetPropertyValue("type", m_typeDescription.GetTypeObject());

	switch (m_typeDescription.GetTypeObject())
	{
	case eValueTypes::TYPE_NUMBER:
		IObjectBase::SetPropertyValue("precision", m_typeDescription.GetPrecision(), true);
		IObjectBase::SetPropertyValue("scale", m_typeDescription.GetScale(), true);
		break;
	case eValueTypes::TYPE_DATE:
		IObjectBase::SetPropertyValue("date_time", m_typeDescription.GetDateTime(), true);
		break;
	case eValueTypes::TYPE_STRING:
		IObjectBase::SetPropertyValue("length", m_typeDescription.GetLength(), true);
		break;
	}
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

	IObjectBase::GetPropertyValue("source", m_source);
	IObjectBase::GetPropertyValue("choice_form", m_choiceForm);

	meta_identifier_t metaType = 0;
	if (IObjectBase::GetPropertyValue("type", metaType)) {

		if (metaType != m_typeDescription.GetTypeObject()) {
			m_typeDescription.SetDefaultMetatype(metaType);
			switch (m_typeDescription.GetTypeObject())
			{
			case eValueTypes::TYPE_NUMBER:
				IObjectBase::SetPropertyValue("precision", m_typeDescription.GetPrecision(), true);
				IObjectBase::SetPropertyValue("scale", m_typeDescription.GetScale(), true);
				break;
			case eValueTypes::TYPE_DATE:
				IObjectBase::SetPropertyValue("date_time", m_typeDescription.GetDateTime(), true);
				break;
			case eValueTypes::TYPE_STRING:
				IObjectBase::SetPropertyValue("length", m_typeDescription.GetLength(), true);
				break;
			}
		}

		switch (m_typeDescription.GetTypeObject())
		{
		case eValueTypes::TYPE_NUMBER:
		{
			unsigned char precision = 10, scale = 0;
			IObjectBase::GetPropertyValue("precision", precision, true);
			IObjectBase::GetPropertyValue("scale", scale, true);
			m_typeDescription.SetNumber(precision, scale);
			break;
		}
		case eValueTypes::TYPE_DATE:
		{
			eDateFractions dateTime = eDateFractions::eDateTime;
			IObjectBase::GetPropertyValue("date_time", dateTime, true);
			m_typeDescription.SetDate(dateTime);
			break;
		}
		case eValueTypes::TYPE_STRING:
		{
			unsigned short length = 0;
			IObjectBase::GetPropertyValue("length", length, true);
			m_typeDescription.SetString(length);
			break;
		}
		}
	}
}
