#include "field.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"     // ibDataNode (control -> node)
#include "backend/metaData.h"

//****************************************************************************
//*                        the view's child, as the field                    *
//****************************************************************************

ibString ibFieldViewProvider::GetClassName() const
{
	return m_field->GetClassName();
}

ibString ibFieldViewProvider::GetObjectTypeName() const
{
	return m_field->GetObjectTypeName();
}

bool ibFieldViewProvider::IsEditable() const
{
	return m_field->IsEditable();
}

const ibMetaData* ibFieldViewProvider::GetMetaData() const
{
	return m_field->GetMetaData();
}

// A view that is not typed into answers the choice with what the field's type and its attribute say — the base answers
// the field's own doors would give, were the view not asked first.
bool ibFieldViewProvider::HasQuickChoice() const
{
	return m_field->ibValueWindow::HasQuickChoice();
}

ibSelectMode ibFieldViewProvider::GetSelectMode() const
{
	return m_field->ibTypeControlFactory::GetSelectMode();
}

const ibChoiceTypeLinkDescription& ibFieldViewProvider::GetTypeLink() const
{
	return m_field->ibControlFrame::GetTypeLink();
}

const ibChoiceParametersDescription& ibFieldViewProvider::GetChoiceParameters() const
{
	return m_field->ibControlFrame::GetChoiceParameters();
}

ibChoiceCondition ibFieldViewProvider::GetChoiceCondition() const
{
	return m_field->ibTypeControlFactory::GetChoiceCondition();
}

std::unique_ptr<ibFieldViewProvider> ibFieldViewProvider::Create(ibFieldView view, ibValueField* field)
{
	switch (view) {
	case ibFieldView::ibFieldView_Input:  return std::make_unique<ibFieldViewInput>(field);
	case ibFieldView::ibFieldView_Flag:   return std::make_unique<ibFieldViewFlag>(field);
	case ibFieldView::ibFieldView_Label:  return std::make_unique<ibFieldViewLabel>(field);
	case ibFieldView::ibFieldView_Switch: return std::make_unique<ibFieldViewSwitch>(field);
	case ibFieldView::ibFieldView_Gauge:  return std::make_unique<ibFieldViewGauge>(field);
	case ibFieldView::ibFieldView_Slider: return std::make_unique<ibFieldViewSlider>(field);
	}
	return nullptr;
}

//****************************************************************************
//*                              the source                                  *
//****************************************************************************

ibSourceObject* ibValueField::GetSourceObject() const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceObject() : nullptr;
}

bool ibValueField::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
}

//****************************************************************************
//*                                Field                                     *
//****************************************************************************

enum prop {
	eControlValue,
};

ibValueField::ibValueField() :
	ibValueWindow(), ibTypeControlFactory()
{
	m_members.Bind(this, &ibValueField::FillControlMembers);
	// born typed into
	SetView(ibFieldView::ibFieldView_Input);
}

const ibMetaData* ibValueField::GetMetaData() const
{
	return m_formOwner != nullptr ? m_formOwner->GetMetaData() : nullptr;
}

void ibValueField::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(ibT("Value"), eControlValue);
}

bool ibValueField::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (m_members.GetPropData(lPropNum, this, &ibValueField::FillControlMembers)) {
	case eControlValue:
		return SetControlValue(varPropVal);
	}
	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueField::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (m_members.GetPropData(lPropNum, this, &ibValueField::FillControlMembers)) {
	case eControlValue:
		return GetControlValue(pvarPropVal);
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

ibString ibValueField::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty())
		return m_propertyTitle->GetValueAsTranslateString();

	if (!m_propertySource->IsEmptyProperty()) {
		const ibBackendSourceColumn* column = WalkSource(GetSourceDesc());
		if (column != nullptr)   // null when the bound field is gone
			return column->GetSynonym();
	}

	return ibEmptyString;
}

bool ibValueField::IsUnbound() const
{
	return m_provider->IsBindingView() && m_propertySource->IsEmptyProperty();
}

bool ibValueField::IsReadOnly() const
{
	// Set so (ReadOnly), or a view that edits nothing (a Label, a Gauge, a switch of no items).
	if (m_propertyReadOnly->GetValueAsBoolean() || m_provider->IsReadOnly())
		return true;
	// A dotted reference path (Source.Ref.Field), a view-only form or a read-only source cannot be written, whatever
	// the view says. Unbound = writable.
	const bool writableBinding = m_propertySource->IsEmptyProperty()
		|| (m_formOwner != nullptr && m_formOwner->IsWritableBinding(m_propertySource->GetValueAsSourceDesc()));
	return !writableBinding;
}

void ibValueField::OnCreated(ibVisualHost* host, bool firstCreated)
{
	// firstCreated is set ONLY by the studio's add, never on a form open. AN ATTRIBUTE DROPPED ON THE FORM makes an Input
	// bound to it, and the field takes the default view (Max, 2026-11-23: a Boolean dropped — a Flag); a view a paste
	// brought is kept. A field dropped with no source auto-binds to a fresh attribute (control-side helper) so it
	// renders bound, not hidden — where its view binds one.
	if (!firstCreated)
		return;
	if (!m_propertySource->IsEmptyProperty()) {
		if (GetView() == ibFieldView::ibFieldView_Input)
			SetDefaultView();
	}
	else if (m_provider->IsBindingView())
		AutoBindNewSource(this);   // `this` upcasts to ibTypeControlFactory* — the source-set side
}

void ibValueField::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// A VIEW THE SOURCE'S TYPE DOES NOT ALLOW — set so, left so by an attribute retyped in the metadata (no field is told
	// of that), read so — gives way to Input here, the place every road ends in (Max, 2026-11-23: "if the type does not
	// support it, it all resets to Input"); a source changed does it at once (OnPropertyChanged). Through the door, so
	// the view's change runs its own course.
	if (!IsViewAllowed(GetView()))
		ibPropertyGate::SetValue(this, m_propertyView, ibVariant(static_cast<int>(ibFieldView::ibFieldView_Input)));

	ibValueWindow::OnUpdate(state, host);

	// …no caption where the title is set to none — a flag's text with it.
	state.SetValue(ibProtocol::ibForm::ibState::Caption,
		m_propertyTitleLocation->GetValueAsEnum() != ibTitleLocation::eNoTitle ? GetControlTitle() : ibString());

	// …the view's part: what it shows of the value, and how.
	m_provider->OnUpdate(state, host);

	// A binding that cannot be written, or a view that edits nothing: the value is shown but not changed — NOT Enabled
	// off, which would grey it out (availability, for action buttons, not data controls).
	state.SetValue(ibProtocol::ibForm::ibState::ReadOnly, IsReadOnly());
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool ibValueField::GetControlValue(ibValue& pvarControlVal) const
{
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
		m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), pvarControlVal)) {
		return true;   // attribute-table / dotted path -> read-only walk
	}

	pvarControlVal = ibTypeControlFactory::AdjustValue(m_selValue);
	return true;
}

bool ibValueField::SetControlValue(const ibValue& varControlVal)
{
	// ⭐ THE FIELD'S OWN LINK BY TYPE NARROWS THE VALUE FIRST (#616), whatever the view — the one verb a holder writes
	// through (ibChoiceLinkResolver::Adjust), the form being its source. It is an exception to "where the data is, not
	// where the window is" (choiceLinkResolver.h), and only because the link is the field's: a field reached through the
	// dot has no attribute to carry it, so no other write knows of it.
	ibValue value = varControlVal;
	ibValue adjusted;
	if (ibChoiceLinkResolver::Adjust(ibChoiceHolder(m_formOwner), GetTypeLink(), varControlVal, adjusted))
		value = adjusted;

	ibValue before;
	GetControlValue(before);

	// What the field shows is what its source is given: an empty value written in is the empty value of its type.
	m_selValue = ibTypeControlFactory::AdjustValue(value);
	const ibBackendSourceColumn* column = !m_propertySource->IsEmptyProperty()
		? WalkSource(GetSourceDesc()) : nullptr;
	if (column != nullptr && m_formOwner != nullptr)
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), m_selValue);

	// …and the controls chosen within this field are emptied, on a change (#616).
	ibTypeControlFactory::ClearLinked(this, before);

	// What the field shows is read from the value on the next Update — nothing is pushed.
	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();

	return true;
}

//*******************************************************************
//*							 Choice     	                        *
//*******************************************************************

// ⭐ THE SOURCE THE BOUND FIELD STANDS IN — asked by the binding's leaf (the text field's rule, issue #259): the source
// is the field's in every view, so this is the field's question.
ibChoiceHolder ibValueField::GetChoiceHolder() const
{
	if (m_formOwner == nullptr)
		return ibChoiceHolder();
	const ibSourceDescription& path = GetSourceDesc();
	if (path.GetHopCount() == 1)
		return ibChoiceHolder(m_formOwner);
	const ibBackendFormAttributeValue* head = path.GetHopCount() > 1 ? FindSourceHolder(path.GetFirst()) : nullptr;
	ibSourceDataObject* const source = head != nullptr ? head->GetSourceValue() : nullptr;
	return ibChoiceHolder(source != nullptr ? source : m_formOwner->GetSourceObject());
}

bool ibValueField::HasQuickChoice() const
{
	return m_provider->HasQuickChoice();
}

ibSelectMode ibValueField::GetSelectMode() const
{
	return m_provider->GetSelectMode();
}

const ibChoiceTypeLinkDescription& ibValueField::GetTypeLink() const
{
	return m_provider->GetTypeLink();
}

const ibChoiceParametersDescription& ibValueField::GetChoiceParameters() const
{
	return m_provider->GetChoiceParameters();
}

ibChoiceCondition ibValueField::GetChoiceCondition() const
{
	return m_provider->GetChoiceCondition();
}

void ibValueField::ChoiceProcessing(ibValue& vSelected)
{
	m_provider->ChoiceProcessing(vSelected);
}

//*******************************************************************
//*							 The view    	                        *
//*******************************************************************

void ibValueField::SetView(ibFieldView view)
{
	std::unique_ptr<ibFieldViewProvider> made = ibFieldViewProvider::Create(view, this);
	if (made == nullptr) {
		// A view the enumeration has and Create does not — a new view half added: the field is kept, typed into.
		m_propertyView->SetValue(ibFieldView::ibFieldView_Input);
		made = ibFieldViewProvider::Create(ibFieldView::ibFieldView_Input, this);
	}

	if (m_provider != nullptr) {
		// WHAT THE TWO VIEWS HAVE BY THE SAME NAME CROSSES OVER — a handler on Opening stays on Opening. The old view's
		// node read by the new one carries the properties and the handlers' names…
		ibDataNode carried;
		m_provider->WriteData(carried);
		made->ReadData(carried);
		// …and a lambda a script hung on, which a node does not carry.
		for (unsigned int idx = 0; idx < made->GetEventCount(); idx++) {
			ibEvent* const event = made->GetEvent(idx);
			const ibEvent* const was = event != nullptr ? m_provider->GetEvent(event->GetName()) : nullptr;
			ibValue handler;
			if (was != nullptr && was->GetDataValue(handler))
				event->SetDataValue(handler);
		}
		RemoveAttachedObject(m_provider.get());
	}

	m_provider = std::move(made);
	AttachPropertyObject(m_provider.get());

	// The members a script names are the new view's.
	InvalidateNames();
}

//*******************************************************************
//*                            Data		                            *
//*******************************************************************

bool ibValueField::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyTitleLocation->SetNodeValue(node.GetProperty(m_propertyTitleLocation->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	// A view this version does not know (a later one, or a damaged form) is refused by the property, which keeps the
	// one it holds: the field is kept, typed into, and the number is said.
	const auto saved = node.FindProperty(m_propertyView->GetName());
	if (saved && !m_propertyView->SetNodeValue(*saved)) {
		ibJournalWarning(ibT("ui.form"), _("Field '%s' has a view this version does not know (%d); it is shown as Input."),
			GetControlName(), (int)saved->AsInt());
		m_propertyView->SetValue(ibFieldView::ibFieldView_Input);
	}
	SetView(GetView());

	if (!m_provider->ReadData(node))
		return false;

	return ibValueWindow::ReadData(node);
}

bool ibValueField::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyView->GetName(), m_propertyView->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyTitleLocation->GetName(), m_propertyTitleLocation->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	if (!m_provider->WriteData(node))
		return false;

	return ibValueWindow::WriteData(node);
}

// The copy walk (ibValueFrame::CopyNode) carries every property and event, the view's among them; the paste makes the
// field of the view pasted first, and its child takes what the copy carried.
bool ibValueField::PasteData(const ibDataNode& node)
{
	SetView(GetView());
	return m_provider->PasteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueField, "Field", "Widget", g_controlFieldCLSID);
