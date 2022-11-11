#include "widgets.h"
#include "frontend/controls/textEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueTextCtrl, IValueWindow)

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

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

ISourceObject* CValueTextCtrl::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

//****************************************************************************
//*                              TextCtrl                                    *
//****************************************************************************

CValueTextCtrl::CValueTextCtrl() : IValueWindow(), IAttributeControl()
{
}

#include "metadata/singleMetaTypes.h"

wxObject* CValueTextCtrl::Create(wxObject* parent, IVisualHost* visualHost)
{
	CTextCtrl* textCtrl = new CTextCtrl((wxWindow*)parent, wxID_ANY,
		m_selValue.GetString(),
		wxDefaultPosition,
		wxDefaultSize,
		0);

	if (m_dataSource.isValid()) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject) {
			m_selValue = srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource));
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
		if (m_dataSource.isValid()) {
			IMetaObject* metaObject = GetMetaSource();
			if (metaObject != NULL)
				textCaption = metaObject->GetSynonym() + wxT(":");
		}

		textCtrl->SetTextLabel(!m_propertyCaption->IsOk() ?
			textCaption : m_propertyCaption->GetValueAsString());

		if (m_dataSource.isValid()) {
			ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject != NULL) {
				m_selValue = srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource));
			}
		}

		if (!appData->DesignerMode()) {
			textCtrl->SetTextValue(m_selValue.GetString());
		}

		textCtrl->SetPasswordMode(m_propertyPasswordMode->GetValueAsBoolean());
		textCtrl->SetMultilineMode(m_propertyMultilineMode->GetValueAsBoolean());
		textCtrl->SetTextEditMode(m_propertyTexteditMode->GetValueAsBoolean());

		if (!appData->DesignerMode()) {
			textCtrl->SetButtonSelect(m_propertySelectButton->GetValueAsBoolean());
			textCtrl->BindButtonSelect(&CValueTextCtrl::OnSelectButtonPressed, this);
			textCtrl->SetButtonList(m_propertyListButton->GetValueAsBoolean());
			textCtrl->BindButtonList(&CValueTextCtrl::OnListButtonPressed, this);
			textCtrl->SetButtonClear(m_propertyClearButton->GetValueAsBoolean());
			textCtrl->BindButtonClear(&CValueTextCtrl::OnClearButtonPressed, this);

			textCtrl->BindTextCtrl(&CValueTextCtrl::OnTextEnter, this);
			textCtrl->BindKillFocus(&CValueTextCtrl::OnKillFocus, this);
		}
		else {
			textCtrl->SetButtonSelect(m_propertySelectButton->GetValueAsBoolean());
			textCtrl->SetButtonList(m_propertyListButton->GetValueAsBoolean());
			textCtrl->SetButtonClear(m_propertyClearButton->GetValueAsBoolean());
		}
	}

	UpdateWindow(textCtrl);
	UpdateLabelSize(textCtrl);
}

void CValueTextCtrl::Cleanup(wxObject* wxobject, IVisualHost* visualHost)
{
	CTextCtrl* textCtrl = dynamic_cast<CTextCtrl*>(wxobject);

	if (textCtrl != NULL) {
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

	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcObject = ownerForm->GetSourceObject();
		if (srcObject != NULL) {
			return srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource));
		}
	}

	return m_selValue;
}

void CValueTextCtrl::SetControlValue(CValue& vSelected)
{
	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		IMetaAttributeObject* metaObject = wxDynamicCast(
			GetMetaSource(), IMetaAttributeObject
		);
		wxASSERT(metaObject);
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != NULL) {
			srcObject->SetValueByMetaID(GetIdByGuid(m_dataSource), vSelected);
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
	wxString caption; reader.r_stringZ(caption);
	m_propertyCaption->SetValue(caption);

	m_propertyPasswordMode->SetValue(reader.r_u8());
	m_propertyMultilineMode->SetValue(reader.r_u8());
	m_propertyTexteditMode->SetValue(reader.r_u8());

	m_propertySelectButton->SetValue(reader.r_u8());
	m_propertyListButton->SetValue(reader.r_u8());
	m_propertyClearButton->SetValue(reader.r_u8());

	m_propertyChoiceForm->SetValue(reader.r_s32());

	if (!IAttributeControl::LoadTypeData(reader))
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
	writer.w_u8(m_propertyListButton->GetValueAsBoolean());
	writer.w_u8(m_propertyClearButton->GetValueAsBoolean());

	writer.w_s32(m_propertyChoiceForm->GetValueAsInteger());

	if (!IAttributeControl::SaveTypeData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}