#include "widgets.h"
#include "frontend/win/ctrls/controlTextEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueTextCtrl, ibValueWindow)

//****************************************************************************

#include "form.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueTextCtrl::GetChoiceForm(ibPropertyList* property)
{
	const ibMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		ibValueMetaObjectRecordDataRef* metaObject = nullptr;
		if (!m_propertySource->IsEmptyProperty()) {
			const ibValueMetaObjectGenericData* metaObjectValue =
				m_formOwner->GetMetaObject();
			if (metaObjectValue != nullptr) {
				const ibValueMetaObjectAttributeBase* attribute = wxDynamicCast(metaObjectValue->FindAnyObjectByFilter(m_propertySource->GetValueAsSource()), ibValueMetaObjectAttributeBase);
				wxASSERT(attribute);
				const ibCtorMetaValueType* so = metaData->GetTypeCtor(attribute->GetFirstClsid());
				if (so != nullptr) {
					metaObject = wxDynamicCast(so->GetMetaObject(), ibValueMetaObjectRecordDataRef);
				}
			}
		}
		else {
			const ibCtorMetaValueType* so = metaData->GetTypeCtor(ibTypeControlFactory::GetFirstClsid());
			if (so != nullptr) {
				metaObject = wxDynamicCast(so->GetMetaObject(), ibValueMetaObjectRecordDataRef);
			}
		}

		if (metaObject != nullptr) {
			for (auto form : metaObject->GetFormArrayObject()) {
				property->AppendItem(
					form->GetSynonym(),
					form->GetMetaID(),
					form->GetIcon(), 
					form
				);
			}
		}
	}

	return true;
}

ibSourceObject* ibValueTextCtrl::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: nullptr;
}

//****************************************************************************
//*                              TextCtrl                                    *
//****************************************************************************

enum prop {
	eControlValue,
};

ibValueTextCtrl::ibValueTextCtrl() :
	ibValueWindow(), ibTypeControlFactory(), m_textModified(false)
{
	//set default params
	m_propertyBG->SetValue(wxColour(255, 255, 255));
}

ibMetaData* ibValueTextCtrl::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueTextCtrl::PrepareNames() const
{
	ibValueFrame::PrepareNames();

	m_methodHelper->AppendProp(wxT("Value"), eControlValue, eControl);
}

bool ibValueTextCtrl::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			SetControlValue(varPropVal);
		}
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueTextCtrl::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			return GetControlValue(pvarPropVal);
		}
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

wxString ibValueTextCtrl::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty()) {
		return m_propertyTitle->GetValueAsTranslateString();
	}
	else if (!m_propertySource->IsEmptyProperty()) {
		const ibValueMetaObject* metaObject = m_propertySource->GetSourceAttributeObject();
		wxASSERT(metaObject);
		return metaObject->GetSynonym();
	}
	return wxEmptyString;
}

wxObject* ibValueTextCtrl::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibControlTextEditor* textEditor = new ibControlNavigationTextEditor(wxparent, wxID_ANY,
		wxEmptyString,
		wxDefaultPosition,
		wxDefaultSize);

	if (!m_propertySource->IsEmptyProperty()) {
		ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr) {
			srcObject->GetValueByMetaID(m_propertySource->GetValueAsSource(), m_selValue);
		}
	}
	else {
		m_selValue = ibTypeControlFactory::AdjustValue(m_selValue);
	}

	return textEditor;
}

void ibValueTextCtrl::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

#include "backend/appData.h"

void ibValueTextCtrl::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibControlTextEditor* textEditor = dynamic_cast<ibControlTextEditor*>(wxobject);

	if (textEditor != nullptr) {

		if (!m_propertySource->IsEmptyProperty()) {
			ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject != nullptr) {
				srcObject->GetValueByMetaID(m_propertySource->GetValueAsSource(), m_selValue);
			}
		}

		textEditor->SetLabel(GetControlTitle());

		if (!appData->DesignerMode()) {
			textEditor->SetValue(m_selValue.GetString());
		}

		textEditor->SetPasswordMode(m_propertyPasswordMode->GetValueAsBoolean());
		textEditor->SetMultilineMode(m_propertyMultilineMode->GetValueAsBoolean());
		textEditor->SetTextEditMode(m_propertyTexteditMode->GetValueAsBoolean());

		if (!appData->DesignerMode()) {

			textEditor->ShowSelectButton(m_propertySelectButton->GetValueAsBoolean());
			textEditor->ShowOpenButton(m_propertyOpenButton->GetValueAsBoolean());
			textEditor->ShowClearButton(m_propertyClearButton->GetValueAsBoolean());

			textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibValueTextCtrl::OnSelectButtonPressed, this);
			textEditor->Bind(wxEVT_CONTROL_BUTTON_OPEN, &ibValueTextCtrl::OnOpenButtonPressed, this);
			textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibValueTextCtrl::OnClearButtonPressed, this);

			textEditor->Bind(wxEVT_CONTROL_TEXT_ENTER, &ibValueTextCtrl::OnTextEnter, this);
			textEditor->Bind(wxEVT_CONTROL_TEXT_INPUT, &ibValueTextCtrl::OnTextUpdated, this);
			textEditor->Bind(wxEVT_CONTROL_TEXT_CLEAR, &ibValueTextCtrl::OnTextUpdated, this);

			textEditor->Bind(wxEVT_KILL_FOCUS, &ibValueTextCtrl::OnKillFocus, this);
		}
		else {
			textEditor->ShowSelectButton(m_propertySelectButton->GetValueAsBoolean());
			textEditor->ShowOpenButton(m_propertyOpenButton->GetValueAsBoolean());
			textEditor->ShowClearButton(m_propertyClearButton->GetValueAsBoolean());
		}
	}

	UpdateWindow(textEditor);
}

void ibValueTextCtrl::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibControlTextEditor* textEditor = dynamic_cast<ibControlTextEditor*>(wxobject);

	if (textEditor != nullptr) {
		if (!appData->DesignerMode()) {

			textEditor->Unbind(wxEVT_CONTROL_BUTTON_SELECT, &ibValueTextCtrl::OnSelectButtonPressed, this);
			textEditor->Unbind(wxEVT_CONTROL_BUTTON_OPEN, &ibValueTextCtrl::OnOpenButtonPressed, this);
			textEditor->Unbind(wxEVT_CONTROL_BUTTON_CLEAR, &ibValueTextCtrl::OnClearButtonPressed, this);

			textEditor->Unbind(wxEVT_CONTROL_TEXT_ENTER, &ibValueTextCtrl::OnTextEnter, this);
			textEditor->Unbind(wxEVT_CONTROL_TEXT_INPUT, &ibValueTextCtrl::OnTextUpdated, this);
			textEditor->Unbind(wxEVT_CONTROL_TEXT_CLEAR, &ibValueTextCtrl::OnTextUpdated, this);

			textEditor->Unbind(wxEVT_KILL_FOCUS, &ibValueTextCtrl::OnKillFocus, this);

		}
	}
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool ibValueTextCtrl::GetControlValue(ibValue& pvarControlVal) const
{
	const ibSourceDataObject* sourceObject = m_formOwner->GetSourceObject();
	if (!m_propertySource->IsEmptyProperty() && sourceObject != nullptr) {
		return sourceObject->GetValueByMetaID(m_propertySource->GetValueAsSource(), pvarControlVal);
	}

	pvarControlVal = ibTypeControlFactory::AdjustValue(m_selValue);
	return true;
}

bool ibValueTextCtrl::SetControlValue(const ibValue& varControlVal)
{
	ibSourceDataObject* sourceObject = m_formOwner->GetSourceObject();
	if (!m_propertySource->IsEmptyProperty() && sourceObject != nullptr) {
		const ibValueMetaObjectAttributeBase* metaObject = m_propertySource->GetSourceAttributeObject();
		wxASSERT(metaObject);
		sourceObject->SetValueByMetaID(m_propertySource->GetValueAsSource(), varControlVal);
		m_selValue = metaObject->AdjustValue(varControlVal);
	}
	else {
		m_selValue = ibTypeControlFactory::AdjustValue(varControlVal);
	}

	m_formOwner->RefreshForm();

	ibControlTextEditor* textEditor = dynamic_cast<ibControlTextEditor*>(GetWxObject());
	if (textEditor != nullptr) {

		textEditor->SetValue(m_selValue.GetString());

		if (m_selValue.IsEmpty())
			textEditor->SetInsertionPoint(wxNOT_FOUND);
		else textEditor->SetInsertionPointEnd();
	}

	return true;
}

//*******************************************************************
//*                            Data		                            *
//*******************************************************************

bool ibValueTextCtrl::LoadData(ibReaderMemory& reader)
{
	wxString caption; reader.r_stringZ(caption);
	m_propertyTitle->SetValue(caption);

	m_propertyPasswordMode->SetValue(reader.r_u8());
	m_propertyMultilineMode->SetValue(reader.r_u8());
	m_propertyTexteditMode->SetValue(reader.r_u8());

	m_propertySelectButton->SetValue(reader.r_u8());
	m_propertyOpenButton->SetValue(reader.r_u8());
	m_propertyClearButton->SetValue(reader.r_u8());

	m_propertyChoiceForm->SetValue(reader.r_s32());

	if (!m_propertySource->LoadData(reader))
		return false;

	//events
	m_eventOnChange->LoadData(reader);
	m_eventStartChoice->LoadData(reader);
	m_eventStartListChoice->LoadData(reader);
	m_eventClearing->LoadData(reader);
	m_eventOpening->LoadData(reader);
	m_eventChoiceProcessing->LoadData(reader);

	return ibValueWindow::LoadData(reader);
}

bool ibValueTextCtrl::SaveData(ibWriterMemory writer)
{
	writer.w_stringZ(m_propertyTitle->GetValueAsString());

	writer.w_u8(m_propertyPasswordMode->GetValueAsBoolean());
	writer.w_u8(m_propertyMultilineMode->GetValueAsBoolean());
	writer.w_u8(m_propertyTexteditMode->GetValueAsBoolean());

	writer.w_u8(m_propertySelectButton->GetValueAsBoolean());
	writer.w_u8(m_propertyOpenButton->GetValueAsBoolean());
	writer.w_u8(m_propertyClearButton->GetValueAsBoolean());

	writer.w_s32(m_propertyChoiceForm->GetValueAsInteger());

	if (!m_propertySource->SaveData(writer))
		return false;

	//events
	m_eventOnChange->SaveData(writer);
	m_eventStartChoice->SaveData(writer);
	m_eventStartListChoice->SaveData(writer);
	m_eventClearing->SaveData(writer);
	m_eventOpening->SaveData(writer);
	m_eventChoiceProcessing->SaveData(writer);

	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueTextCtrl, "Textctrl", "Widget", string_to_clsid("CT_TXTC"));