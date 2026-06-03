#include "widgets.h"

#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#else
#include "frontend/win/ctrls/controlTextEditor.h"
#endif


//****************************************************************************

#include "form.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueTextCtrl::GetChoiceForm(ibPropertyList* property)
{
	const ibMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		const ibValueMetaObjectRecordDataRef* metaObject = nullptr;
		if (!m_propertySource->IsEmptyProperty()) {
			const ibValueMetaObjectGenericData* metaObjectValue =
				m_formOwner->GetMetaObject();
			if (metaObjectValue != nullptr) {
				const ibValueMetaObjectAttributeBase* attribute = dynamic_cast<ibValueMetaObjectAttributeBase*>(metaObjectValue->FindAnyObjectByFilter(m_propertySource->GetValueAsSource()));
				wxASSERT(attribute);
				const ibCtorMetaValueType* so = metaData->GetTypeCtor(attribute->GetFirstClsid());
				if (so != nullptr) {
					metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
				}
			}
		}
		else {
			const ibCtorMetaValueType* so = metaData->GetTypeCtor(ibTypeControlFactory::GetFirstClsid());
			if (so != nullptr) {
				metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
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

const ibMetaData* ibValueTextCtrl::GetMetaData() const
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

wxObject* ibValueTextCtrl::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
	(void)visualHost;
	// Allocation ifdef; rest shared.
#ifdef OES_USE_WEB
	(void)wxparent;
	auto* textEditor = new ibWebTextCtrl(GetControlID());
	textEditor->Bind(wxEVT_TEXT, &ibValueTextCtrl::OnWebTextChanged, this);
#else
	auto* textEditor = new ibControlNavigationTextEditor(
		wxparent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
#endif

	if (!m_propertySource->IsEmptyProperty()) {
		ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr)
			srcObject->GetValueByMetaID(m_propertySource->GetValueAsSource(), m_selValue);
	} else {
		m_selValue = ibTypeControlFactory::AdjustValue(m_selValue);
	}

	return textEditor;
}

void ibValueTextCtrl::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

#include "backend/appData.h"

void ibValueTextCtrl::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
	// Only the cast differs between builds — ibWebTextCtrl mirrors the
	// subset of ibControlTextEditor's API that the runtime path needs
	// (SetValue / SetLabel / SetPasswordMode / SetMultilineMode /
	// SetTextEditMode / Show{Select,Open,Clear}Button + the six text
	// events). From here on the body is a single pass.
#ifdef OES_USE_WEB
	auto* textEditor = static_cast<ibWebTextCtrl*>(wxobject);
#else
	auto* textEditor = static_cast<ibControlTextEditor*>(wxobject);
#endif
	if (textEditor == nullptr) {
		UpdateWindow(textEditor);
		return;
	}

	// Source-backed value refresh — if the textctrl is bound to an
	// attribute, pull the current source value; otherwise the control
	// carries its own m_selValue across Update passes.
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr) {
		ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr)
			srcObject->GetValueByMetaID(m_propertySource->GetValueAsSource(), m_selValue);
	}
	else {
		m_selValue = ibTypeControlFactory::AdjustValue(m_selValue);
	}

	// Property push — identical setter surface on both builds.
	// Desktop's "if (!DesignerMode()) SetValue" guard is pointless on web
	// (wfrontend is never in designer mode); the no-op check is cheap.
	textEditor->SetLabel(GetControlTitle());
	if (!appData->DesignerMode())
		textEditor->SetValue(m_selValue.GetString());
	textEditor->SetPasswordMode(m_propertyPasswordMode->GetValueAsBoolean());
	textEditor->SetMultilineMode(m_propertyMultilineMode->GetValueAsBoolean());
	textEditor->SetTextEditMode(m_propertyTexteditMode->GetValueAsBoolean());
	textEditor->ShowSelectButton(m_propertySelectButton->GetValueAsBoolean());
	textEditor->ShowOpenButton(m_propertyOpenButton->GetValueAsBoolean());
	textEditor->ShowClearButton(m_propertyClearButton->GetValueAsBoolean());

	// Button binds — unified, handlers (OnSelect/Open/ClearButtonPressed)
	// are pure metadata work, identical on both builds.
	if (!appData->DesignerMode()) {
		textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibValueTextCtrl::OnSelectButtonPressed, this);
		textEditor->Bind(wxEVT_CONTROL_BUTTON_OPEN,   &ibValueTextCtrl::OnOpenButtonPressed,   this);
		textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR,  &ibValueTextCtrl::OnClearButtonPressed,  this);
#ifndef OES_USE_WEB
		// Text-edit events are wxTextCtrl-internal on desktop; their
		// handlers (OnTextEnter / OnKillFocus) wxDynamicCast the event
		// object to wxTextCtrl and GetValue() it — not valid against
		// ibWebTextCtrl. Web gets its own commit path via OnWebTextChanged
		// bound in Create() (wxEVT_TEXT, fired by /change).
		textEditor->Bind(wxEVT_CONTROL_TEXT_ENTER,    &ibValueTextCtrl::OnTextEnter,           this);
		textEditor->Bind(wxEVT_CONTROL_TEXT_INPUT,    &ibValueTextCtrl::OnTextUpdated,         this);
		textEditor->Bind(wxEVT_CONTROL_TEXT_CLEAR,    &ibValueTextCtrl::OnTextUpdated,         this);
		textEditor->Bind(wxEVT_KILL_FOCUS,            &ibValueTextCtrl::OnKillFocus,           this);
#endif
	}

	UpdateWindow(textEditor);
}

void ibValueTextCtrl::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxobject;
	(void)visualHost;
#else
	ibControlTextEditor* textEditor = static_cast<ibControlTextEditor*>(wxobject);

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
#endif
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

	// Push m_selValue into the live editor. GetWxObject() is unified —
	// both builds return the web/wx node the walker stashed in the host's
	// m_baseObjects. Desktop additionally repositions the insertion
	// point; web handles cursor placement client-side (see webClient.cpp
	// TextCtrl renderer, which does setSelectionRange(0,0) on Clear).
#ifdef OES_USE_WEB
	// dynamic_cast (not static_cast) — paranoid: if the walker ever
	// stores a different wxObject subtype in m_baseObjects for this
	// frame (e.g., a stub), static_cast's UB would write wxString at
	// the wrong offset and corrupt the heap (seen 2026-04-19 in a
	// dump from /change path). dynamic_cast returns nullptr on a type
	// mismatch instead.
	auto* textEditor = dynamic_cast<ibWebTextCtrl*>(GetWxObject());
	if (textEditor != nullptr)
		textEditor->SetValue(m_selValue.GetString());
#else
	ibControlTextEditor* textEditor = static_cast<ibControlTextEditor*>(GetWxObject());
	if (textEditor != nullptr) {
		textEditor->SetValue(m_selValue.GetString());
		if (m_selValue.IsEmpty())
			textEditor->SetInsertionPoint(wxNOT_FOUND);
		else textEditor->SetInsertionPointEnd();
	}
#endif

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

bool ibValueTextCtrl::SaveData(ibWriterMemory& writer)
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