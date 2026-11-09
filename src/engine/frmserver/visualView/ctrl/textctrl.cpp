#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)


//****************************************************************************

#include "form.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/choiceLinkResolver.h"   // ibChoiceHolder — where this control's link reads its neighbours

bool ibValueTextCtrl::GetChoiceForm(ibPropertyList* property)
{
	const ibMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		const ibValueMetaObjectRecordDataRef* metaObject = nullptr;
		if (!m_propertySource->IsEmptyProperty()) {
			// Resolve the bound attribute config-wide — a dotted path's leaf lives in a
			// referenced type, not the form's own metaobject (so the source-scoped lookup
			// here would miss it and assert). The dot (WalkSource) handles both.
			const ibBackendSourceColumn* attribute = WalkSource(GetSourceDesc());
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
					form->GetClassType(),
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
	ibValueWindow(), ibTypeControlFactory()
{
	m_members.Bind(this, &ibValueTextCtrl::FillControlMembers);
	//set default params
	m_propertyBG->SetValue(ibColour(255, 255, 255));
}

const ibMetaData* ibValueTextCtrl::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueTextCtrl::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(ibT("Value"), eControlValue, eControl);
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

ibString ibValueTextCtrl::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty()) {
		return m_propertyTitle->GetValueAsTranslateString();
	}
	else if (!m_propertySource->IsEmptyProperty()) {
		const ibBackendSourceColumn* column = WalkSource(GetSourceDesc());
		if (column != nullptr)   // null when the bound field is gone
			return column->GetSynonym();
	}
	return ibEmptyString;
}

bool ibValueTextCtrl::IsReadOnly() const
{
	if (!m_propertyTexteditMode->GetValueAsBoolean())
		return true;
	// A dotted reference path (Source.Ref.Field), a view-only form or a read-only source cannot be
	// written, whatever TextEditMode says. Unbound = editable.
	const bool writableBinding = m_propertySource->IsEmptyProperty()
		|| (m_formOwner != nullptr && m_formOwner->IsWritableBinding(m_propertySource->GetValueAsSourceDesc()));
	return !writableBinding;
}

#include "backend/appData.h"
#include "core/formatString.h"   // ibFormatString — what the field shows its value through

// The format of the field this control is bound to — asked of the source that holds it, by the binding's leaf.
const ibTranslateString& ibValueTextCtrl::GetSourceFormat() const
{
	return GetChoiceHolder().GetFormat(GetSourceDesc().GetLeaf());
}

void ibValueTextCtrl::OnCreated(ibVisualHost* host, bool firstCreated)
{
	// A just-dropped textbox with no source auto-binds to a fresh attribute (control-side helper) so it
	// renders bound, not hidden. firstCreated is set ONLY by the designer's add, never on a form open.
	if (firstCreated)
		AutoBindNewSource(this);   // `this` upcasts to ibTypeControlFactory* — the source-set side
}

void ibValueTextCtrl::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);

	state.SetValue(ibProtocol::ibForm::ibState::Caption, GetControlTitle());

	// The value as text, through the field's own format or, without one, the bound attribute's. Not in
	// the designer: there the field shows no value.
	if (!appData->DesignerMode()) {
		ibValue value;
		GetControlValue(value);

		ibString text;
		const ibTranslateString& format = m_propertyFormat->GetValueAsFormatString();
		GetFormatFromColumn(!format.IsEmpty() ? format : GetSourceFormat(), GetTypeDesc()).Apply(value, text);
		state.SetValue(ibProtocol::ibForm::ibState::Text, text);
		// …and the value itself, with its type (ibValue::Serialize): a client edits by the type — a date as a date —
		// and may answer a Change with the value instead of text. One that does not travel (an open object) is shown
		// by its text alone.
		if (value.IsTransferable())
			value.Serialize(state.Child(ibProtocol::ibForm::ibState::Value));
	}

	// TextEditMode off or a binding that cannot be written: the text is shown but not typed into, and
	// Select / Clear are locked with it; Open stays live.
	state.SetValue(ibProtocol::ibForm::ibState::ReadOnly, IsReadOnly());
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
	// A bound source writes back through the form — a whole attribute or a field of it, the column the dot finds
	// (the head selects the attribute; a dotted reference path is read-only → no-op). Adjusting the value to the
	// bound Type is the factory's job in EVERY case (bound or not), so it is unconditional.
	//
	// ⭐ THE FIELD'S OWN LINK BY TYPE NARROWS IT FIRST (#616) — the one verb a holder writes through
	// (ibChoiceLinkResolver::Adjust), the form being its source. It is an exception to "where the data is, not where the
	// window is" (choiceLinkResolver.h), and only because the link is the control's: a field reached through the dot
	// has no attribute to carry it, so no other write knows of it.
	ibValue value = varControlVal;
	ibValue adjusted;
	if (ibChoiceLinkResolver::Adjust(ibChoiceHolder(m_formOwner), GetTypeLink(), varControlVal, adjusted))
		value = adjusted;

	ibValue before;
	GetControlValue(before);

	// What the field shows is what its source is given: an empty value written in (a linked field emptied, ClearLinked)
	// is the empty value of its type, not Undefined.
	m_selValue = ibTypeControlFactory::AdjustValue(value);
	const ibBackendSourceColumn* column = !m_propertySource->IsEmptyProperty()
		? WalkSource(GetSourceDesc()) : nullptr;
	if (column != nullptr && m_formOwner != nullptr)
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), m_selValue);

	// …and the controls chosen within this field are emptied, on a change (#616).
	ibTypeControlFactory::ClearLinked(this, before);

	// The text the field shows is formatted from the value on the next Update — nothing is pushed.
	m_formOwner->RefreshForm();

	return true;
}

// ⭐ THE SOURCE THE BOUND FIELD STANDS IN — asked by the binding's leaf, so it is the one that has that field (issue #259).
// An attribute of the form itself stands in the form, the source over its attributes: asked of the main object, its
// form-local id named nothing there, or a field of the object's that happened to have the same small number. A field
// of what an attribute holds stands in that attribute's source, which is the main object only for the main attribute.
ibChoiceHolder ibValueTextCtrl::GetChoiceHolder() const
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

// THE FIELD'S OWN WORD over what its type says (issue #259): Use and Don't use say it outright, Auto asks the type.
bool ibValueTextCtrl::HasQuickChoice() const
{
	switch (m_propertyQuickChoice->GetValueAsEnum()) {
	case ibQuickChoiceMode::ibQuickChoiceMode_Use: return true;
	case ibQuickChoiceMode::ibQuickChoiceMode_DontUse: return false;
	case ibQuickChoiceMode::ibQuickChoiceMode_Auto: break;
	}
	return ibValueWindow::HasQuickChoice();
}

// …and over what its attribute's Select says: a form may let folders be picked into a field whose attribute takes items.
ibSelectMode ibValueTextCtrl::GetSelectMode() const
{
	switch (m_propertyChoiceFoldersAndItems->GetValueAsEnum()) {
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Items: return ibSelectMode::ibSelectMode_Items;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Folders: return ibSelectMode::ibSelectMode_Folders;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_FoldersAndItems: return ibSelectMode::ibSelectMode_FoldersAndItems;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Auto: break;
	}
	return ibTypeControlFactory::GetSelectMode();
}

// …and its own rows over its field's, read in the form (#616).
ibChoiceCondition ibValueTextCtrl::GetChoiceCondition() const
{
	return ibChoiceLinkResolver::Resolve(GetChoiceHolder(), GetSourceDesc().GetLeaf(), GetChoiceParameters(),
		ibChoiceHolder(m_formOwner));
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
	m_propertyFormat->SetNodeValue(node.GetProperty(m_propertyFormat->GetName()));
	m_propertySelectButton->SetNodeValue(node.GetProperty(m_propertySelectButton->GetName()));
	m_propertyOpenButton->SetNodeValue(node.GetProperty(m_propertyOpenButton->GetName()));
	m_propertyClearButton->SetNodeValue(node.GetProperty(m_propertyClearButton->GetName()));
	m_propertyChoiceForm->SetNodeValue(node.GetProperty(m_propertyChoiceForm->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	m_propertyQuickChoice->SetNodeValue(node.GetProperty(m_propertyQuickChoice->GetName()));
	m_propertyChoiceFoldersAndItems->SetNodeValue(node.GetProperty(m_propertyChoiceFoldersAndItems->GetName()));
	m_propertyTypeLink->SetNodeValue(node.GetProperty(m_propertyTypeLink->GetName()));
	m_propertyChoiceParameters->SetNodeValue(node.GetProperty(m_propertyChoiceParameters->GetName()));

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
	node.SetProperty(m_propertyFormat->GetName(), m_propertyFormat->GetNodeValue());
	node.SetProperty(m_propertySelectButton->GetName(), m_propertySelectButton->GetNodeValue());
	node.SetProperty(m_propertyOpenButton->GetName(), m_propertyOpenButton->GetNodeValue());
	node.SetProperty(m_propertyClearButton->GetName(), m_propertyClearButton->GetNodeValue());
	node.SetProperty(m_propertyChoiceForm->GetName(), m_propertyChoiceForm->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyQuickChoice->GetName(), m_propertyQuickChoice->GetNodeValue());
	node.SetProperty(m_propertyChoiceFoldersAndItems->GetName(), m_propertyChoiceFoldersAndItems->GetNodeValue());
	node.SetProperty(m_propertyTypeLink->GetName(), m_propertyTypeLink->GetNodeValue());
	node.SetProperty(m_propertyChoiceParameters->GetName(), m_propertyChoiceParameters->GetNodeValue());

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
