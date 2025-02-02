#include "widgets.h"
#include "frontend/win/ctrls/textEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueTextCtrl, IValueWindow)

//****************************************************************************

#include "form.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

OptionList* CValueTextCtrl::GetChoiceForm(PropertyOption* property)
{
	OptionList* optList = new OptionList;
	optList->AddOption(_("default"), wxNOT_FOUND);

	IMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		IMetaObjectRecordDataRef* metaObject = nullptr;
		if (m_dataSource.isValid()) {
			IMetaObjectGenericData* metaObjectValue =
				m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				IMetaObjectAttribute* metaAttribute = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaObjectAttribute
				);
				wxASSERT(metaAttribute);
				IMetaValueTypeCtor* so = metaData->GetTypeCtor(metaAttribute->GetFirstClsid());
				if (so != nullptr) {
					metaObject = wxDynamicCast(so->GetMetaObject(), IMetaObjectRecordDataRef);
				}
			}
		}
		else {
			IMetaValueTypeCtor* so = metaData->GetTypeCtor(ITypeControlAttribute::GetFirstClsid());
			if (so != nullptr) {
				metaObject = wxDynamicCast(so->GetMetaObject(), IMetaObjectRecordDataRef);
			}
		}

		if (metaObject != nullptr) {
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
		: nullptr;
}

//****************************************************************************
//*                              TextCtrl                                    *
//****************************************************************************

CValueTextCtrl::CValueTextCtrl() :
	IValueWindow(), ITypeControlAttribute(), m_textModified(false)
{
}

IMetaData* CValueTextCtrl::GetMetaData() const
{
	return m_formOwner ?
		m_formOwner->GetMetaData() : nullptr;
}

wxObject* CValueTextCtrl::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	wxTextContainerCtrl* textEditor = new wxTextContainerCtrl(wxparent, wxID_ANY,
		m_selValue.GetString(),
		wxDefaultPosition,
		wxDefaultSize);

	if (m_dataSource.isValid()) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr) {
			srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource), m_selValue);
		}
	}
	else {
		m_selValue = ITypeControlAttribute::CreateValue();
	}

	return textEditor;
}

void CValueTextCtrl::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstСreated)
{
}

#include "backend/appData.h"

void CValueTextCtrl::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(wxobject);

	if (textEditor != nullptr) {
		wxString textCaption = wxEmptyString;
		if (m_dataSource.isValid()) {
			IMetaObject* metaObject = GetMetaSource();
			if (metaObject != nullptr)
				textCaption = metaObject->GetSynonym() + wxT(":");
		}

		textEditor->SetTextLabel(!m_propertyCaption->IsOk() ?
			textCaption : m_propertyCaption->GetValueAsString());

		if (m_dataSource.isValid()) {
			ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject != nullptr) {
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

	if (textEditor != nullptr) {
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
		if (srcObject != nullptr) {
			return srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource), pvarControlVal);
		}
	}
	pvarControlVal = m_selValue;
	return true;
}

bool CValueTextCtrl::SetControlValue(const CValue& varControlVal)
{
	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		IMetaObjectAttribute* metaObject = wxDynamicCast(
			GetMetaSource(), IMetaObjectAttribute
		);
		wxASSERT(metaObject);
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr) {
			srcObject->SetValueByMetaID(GetIdByGuid(m_dataSource), varControlVal);
		}
		m_selValue = metaObject->AdjustValue(varControlVal);
	}
	else {
		m_selValue = ITypeAttribute::AdjustValue(varControlVal);
	}

	wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(GetWxObject());
	if (textEditor != nullptr) {
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

	if (!ITypeControlAttribute::LoadTypeData(reader))
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

	if (!ITypeControlAttribute::SaveTypeData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(CValueTextCtrl, "textctrl", "widget", string_to_clsid("CT_TXTC"));