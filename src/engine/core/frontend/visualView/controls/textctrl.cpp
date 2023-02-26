#include "widgets.h"
#include "frontend/controls/textEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueTextCtrl, IValueWindow)

//****************************************************************************

#include "form.h"
#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

OptionList* CValueTextCtrl::GetChoiceForm(PropertyOption* property)
{
	OptionList* optList = new OptionList;
	optList->AddOption(_("default"), wxNOT_FOUND);

	IMetadata* metaData = GetMetaData();
	if (metaData != NULL) {
		IMetaObjectRecordDataRef* metaObject = NULL;
		if (m_dataSource.isValid()) {
			IMetaObjectWrapperData* metaObjectValue =
				m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				IMetaAttributeObject* metaAttribute = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
				);
				wxASSERT(metaAttribute);
				IMetaTypeObjectValueSingle* so = metaData->GetTypeObject(metaAttribute->GetFirstClsid());
				if (so != NULL) {
					metaObject = wxDynamicCast(so->GetMetaObject(), IMetaObjectRecordDataRef);
				}
			}
		}
		else {
			IMetaTypeObjectValueSingle* so = metaData->GetTypeObject(ITypeControl::GetFirstClsid());
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

ISourceObject* CValueTextCtrl::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

//****************************************************************************
//*                              TextCtrl                                    *
//****************************************************************************

CValueTextCtrl::CValueTextCtrl() :
	IValueWindow(), ITypeControl(), m_textModified(false)
{
}

#include "core/metadata/singleClass.h"

wxObject* CValueTextCtrl::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	wxTextContainerCtrl* textEditor = new wxTextContainerCtrl(wxparent, wxID_ANY,
		m_selValue.GetString(),
		wxDefaultPosition,
		wxDefaultSize);

	if (m_dataSource.isValid()) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != NULL) {
			srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource), m_selValue);
		}
	}
	else {
		m_selValue = ITypeControl::CreateValue();
	}

	return textEditor;
}

void CValueTextCtrl::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstСreated)
{
}

#include "appData.h"

void CValueTextCtrl::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(wxobject);

	if (textEditor) {
		wxString textCaption = wxEmptyString;
		if (m_dataSource.isValid()) {
			IMetaObject* metaObject = GetMetaSource();
			if (metaObject != NULL)
				textCaption = metaObject->GetSynonym() + wxT(":");
		}

		textEditor->SetTextLabel(!m_propertyCaption->IsOk() ?
			textCaption : m_propertyCaption->GetValueAsString());

		if (m_dataSource.isValid()) {
			ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject != NULL) {
				srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource), m_selValue);
			}
		}

		if (!appData->DesignerMode()) {
			textEditor->SetTextValue(m_selValue.GetString());
		}

		textEditor->SetPasswordMode(m_propertyPasswordMode->GetValueAsBoolean());
		textEditor->SetMultilineMode(m_propertyMultilineMode->GetValueAsBoolean());
		textEditor->SetTextEditMode(m_propertyTexteditMode->GetValueAsBoolean());

		if (!appData->DesignerMode()) {
			textEditor->SetButtonSelect(m_propertySelectButton->GetValueAsBoolean());
			textEditor->BindButtonSelect(&CValueTextCtrl::OnSelectButtonPressed, this);
			textEditor->SetButtonOpen(m_propertyOpenButton->GetValueAsBoolean());
			textEditor->BindButtonOpen(&CValueTextCtrl::OnOpenButtonPressed, this);
			textEditor->SetButtonClear(m_propertyClearButton->GetValueAsBoolean());
			textEditor->BindButtonClear(&CValueTextCtrl::OnClearButtonPressed, this);

			textEditor->BindTextEnter(&CValueTextCtrl::OnTextEnter, this);
			textEditor->BindTextUpdated(&CValueTextCtrl::OnTextUpdated, this);

			textEditor->BindKillFocus(&CValueTextCtrl::OnKillFocus, this);
		}
		else {
			textEditor->SetButtonSelect(m_propertySelectButton->GetValueAsBoolean());
			textEditor->SetButtonOpen(m_propertyOpenButton->GetValueAsBoolean());
			textEditor->SetButtonClear(m_propertyClearButton->GetValueAsBoolean());
		}
	}

	UpdateWindow(textEditor);
	UpdateLabelSize(textEditor);
}

void CValueTextCtrl::Cleanup(wxObject* wxobject, IVisualHost* visualHost)
{
	wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(wxobject);

	if (textEditor != NULL) {
		if (!appData->DesignerMode()) {
			textEditor->UnbindButtonSelect(&CValueTextCtrl::OnSelectButtonPressed, this);
			textEditor->UnbindButtonOpen(&CValueTextCtrl::OnOpenButtonPressed, this);
			textEditor->UnbindButtonClear(&CValueTextCtrl::OnClearButtonPressed, this);

			textEditor->UnbindTextEnter(&CValueTextCtrl::OnTextEnter, this);
			textEditor->UnbindTextUpdated(&CValueTextCtrl::OnTextUpdated, this);

			textEditor->UnbindKillFocus(&CValueTextCtrl::OnKillFocus, this);
		}
	}
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool CValueTextCtrl::GetControlValue(CValue& pvarControlVal) const
{
	CValueForm* ownerForm = GetOwnerForm();
	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcObject = ownerForm->GetSourceObject();
		if (srcObject != NULL) {
			return srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource), pvarControlVal);
		}
	}
	pvarControlVal = m_selValue;
	return true;
}

bool CValueTextCtrl::SetControlValue(const CValue& varControlVal)
{
	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		IMetaAttributeObject* metaObject = wxDynamicCast(
			GetMetaSource(), IMetaAttributeObject
		);
		wxASSERT(metaObject);
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != NULL) {
			srcObject->SetValueByMetaID(GetIdByGuid(m_dataSource), varControlVal);
		}
		m_selValue = metaObject->AdjustValue(varControlVal);
	}
	else {
		m_selValue = ITypeAttribute::AdjustValue(varControlVal);
	}

	wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(GetWxObject());
	if (textEditor != NULL) {
		textEditor->SetTextValue(m_selValue.GetString());
		textEditor->SetInsertionPointEnd();
	}

	return true; 
}

//*******************************************************************
//*                            Data		                            *
//*******************************************************************

bool CValueTextCtrl::LoadData(CMemoryReader& reader)
{
	wxString caption; reader.r_stringZ(caption);
	m_propertyCaption->SetValue(caption);

	m_propertyPasswordMode->SetValue(reader.r_u8());
	m_propertyMultilineMode->SetValue(reader.r_u8());
	m_propertyTexteditMode->SetValue(reader.r_u8());

	m_propertySelectButton->SetValue(reader.r_u8());
	m_propertyOpenButton->SetValue(reader.r_u8());
	m_propertyClearButton->SetValue(reader.r_u8());

	m_propertyChoiceForm->SetValue(reader.r_s32());

	if (!ITypeControl::LoadTypeData(reader))
		return false;

	return IValueWindow::LoadData(reader);
}

bool CValueTextCtrl::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());

	writer.w_u8(m_propertyPasswordMode->GetValueAsBoolean());
	writer.w_u8(m_propertyMultilineMode->GetValueAsBoolean());
	writer.w_u8(m_propertyTexteditMode->GetValueAsBoolean());

	writer.w_u8(m_propertySelectButton->GetValueAsBoolean());
	writer.w_u8(m_propertyOpenButton->GetValueAsBoolean());
	writer.w_u8(m_propertyClearButton->GetValueAsBoolean());

	writer.w_s32(m_propertyChoiceForm->GetValueAsInteger());

	if (!ITypeControl::SaveTypeData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_VALUE_REGISTER(CValueTextCtrl, "textctrl", "widget", TEXT2CLSID("CT_TXTC"));