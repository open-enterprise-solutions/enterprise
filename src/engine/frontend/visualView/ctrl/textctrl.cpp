#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)

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
			// Resolve the bound attribute config-wide — a dotted path's leaf lives in a
			// referenced type, not the form's own metaobject (so the source-scoped lookup
			// here would miss it and assert). GetSourceAttributeObject handles both.
			const ibBackendSourceColumn* attribute = m_propertySource->GetSourceAttributeObject();
			if (attribute != nullptr) {
				const ibCtorMetaValueType* so = metaData->GetTypeCtor(attribute->GetTypeDesc().GetFirstClsid());
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

bool ibValueTextCtrl::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
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
	m_members.Bind(this, &ibValueTextCtrl::FillControlMembers);
	//set default params
	m_propertyBG->SetValue(wxColour(255, 255, 255));
}

const ibMetaData* ibValueTextCtrl::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueTextCtrl::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Value"), eControlValue, eControl);
}

bool ibValueTextCtrl::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			SetControlValue(varPropVal);
		}
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueTextCtrl::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
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
		const ibBackendAbstractColumn* column = GetSourceAbstractColumn();
		if (column != nullptr)   // null when the bound field is gone / whole-attribute binding
			return column->GetSynonym();
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
		if (m_formOwner != nullptr)
			m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), m_selValue);
	} else {
		m_selValue = ibTypeControlFactory::AdjustValue(m_selValue);
	}

	return textEditor;
}

void ibValueTextCtrl::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
	// A just-dropped textbox with no source auto-binds to a fresh attribute (control-side helper) so it
	// renders bound, not hidden. firstCreated is set ONLY by the designer's add, never on a form open.
	if (firstCreated)
		AutoBindNewSource(this);   // `this` upcasts to ibTypeControlFactory* — the source-set side
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
		m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), m_selValue);
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
	// A dotted reference path (Source.Ref.Field) is read-only — force edit mode off
	// regardless of the control's TextEditMode property. Unbound = editable.
	const bool writableBinding = m_propertySource->IsEmptyProperty()
		|| (m_formOwner != nullptr && m_formOwner->IsWritableBinding(m_propertySource->GetValueAsSourceDesc()));
	// A read-only binding (view-only form / read-only source / dotted reference) is just TextEditMode off — the
	// control then greys the text AND locks Select / Clear itself (Open stays live).
	textEditor->SetTextEditMode(m_propertyTexteditMode->GetValueAsBoolean() && writableBinding);
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
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
		m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), pvarControlVal)) {
		return true;   // attribute-table / dotted path -> read-only walk
	}

	pvarControlVal = ibTypeControlFactory::AdjustValue(m_selValue);
	return true;
}

bool ibValueTextCtrl::SetControlValue(const ibValue& varControlVal)
{
	// A bound DIRECT-FIELD source writes back through the form (the head selects the attribute; a
	// dotted reference path is read-only → no-op). Adjusting the value to the bound Type is the
	// factory's job in EVERY case (bound or not), so it is unconditional.
	const ibBackendSourceColumn* column = !m_propertySource->IsEmptyProperty()
		? m_propertySource->GetSourceAttributeObject() : nullptr;
	if (column != nullptr && m_formOwner != nullptr)
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), varControlVal);
	m_selValue = ibTypeControlFactory::AdjustValue(varControlVal);

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

bool ibValueTextCtrl::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyPasswordMode->SetNodeValue(node.GetProperty(m_propertyPasswordMode->GetName()));
	m_propertyMultilineMode->SetNodeValue(node.GetProperty(m_propertyMultilineMode->GetName()));
	m_propertyTexteditMode->SetNodeValue(node.GetProperty(m_propertyTexteditMode->GetName()));
	m_propertySelectButton->SetNodeValue(node.GetProperty(m_propertySelectButton->GetName()));
	m_propertyOpenButton->SetNodeValue(node.GetProperty(m_propertyOpenButton->GetName()));
	m_propertyClearButton->SetNodeValue(node.GetProperty(m_propertyClearButton->GetName()));
	m_propertyChoiceForm->SetNodeValue(node.GetProperty(m_propertyChoiceForm->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	//events
	m_eventOnChange->SetNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	m_eventStartChoice->SetNodeValue(node.GetProperty(m_eventStartChoice->GetName()));
	m_eventStartListChoice->SetNodeValue(node.GetProperty(m_eventStartListChoice->GetName()));
	m_eventClearing->SetNodeValue(node.GetProperty(m_eventClearing->GetName()));
	m_eventOpening->SetNodeValue(node.GetProperty(m_eventOpening->GetName()));
	m_eventChoiceProcessing->SetNodeValue(node.GetProperty(m_eventChoiceProcessing->GetName()));

	return ibValueWindow::ReadData(node);
}

bool ibValueTextCtrl::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyPasswordMode->GetName(), m_propertyPasswordMode->GetNodeValue());
	node.SetProperty(m_propertyMultilineMode->GetName(), m_propertyMultilineMode->GetNodeValue());
	node.SetProperty(m_propertyTexteditMode->GetName(), m_propertyTexteditMode->GetNodeValue());
	node.SetProperty(m_propertySelectButton->GetName(), m_propertySelectButton->GetNodeValue());
	node.SetProperty(m_propertyOpenButton->GetName(), m_propertyOpenButton->GetNodeValue());
	node.SetProperty(m_propertyClearButton->GetName(), m_propertyClearButton->GetNodeValue());
	node.SetProperty(m_propertyChoiceForm->GetName(), m_propertyChoiceForm->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	//events
	node.SetProperty(m_eventOnChange->GetName(), m_eventOnChange->GetNodeValue());
	node.SetProperty(m_eventStartChoice->GetName(), m_eventStartChoice->GetNodeValue());
	node.SetProperty(m_eventStartListChoice->GetName(), m_eventStartListChoice->GetNodeValue());
	node.SetProperty(m_eventClearing->GetName(), m_eventClearing->GetNodeValue());
	node.SetProperty(m_eventOpening->GetName(), m_eventOpening->GetNodeValue());
	node.SetProperty(m_eventChoiceProcessing->GetName(), m_eventChoiceProcessing->GetNodeValue());

	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueTextCtrl, "Textctrl", "Widget", g_controlTextCtrlCLSID);
