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
	ibValueWindow(), ibTypeControlFactory()
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
#include "backend/metaCollection/attribute/metaAttributeObject.h"

const ibTranslateString& ibValueTextCtrl::GetSourceFormat() const
{
	static const ibTranslateString s_none;
	const ibValueMetaObjectAttributeBase* attribute = ibChoiceLinkResolver::FieldOf(GetChoiceHolder(), this);
	return attribute != nullptr ? attribute->GetFormat() : s_none;
}

void ibValueTextCtrl::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);

	state.SetValue(wxT("Caption"), GetControlTitle());

	// The value as text, through the field's own format or, without one, the bound attribute's. Not in
	// the designer: there the field shows no value.
	if (!appData->DesignerMode()) {
		ibValue value;
		GetControlValue(value);

		wxString text;
		const ibTranslateString& format = m_propertyFormat->GetValueAsFormatString();
		GetFormatFromColumn(!format.IsEmpty() ? format : GetSourceFormat(), GetTypeDesc()).Apply(value, text);
		state.SetValue(wxT("Text"), text);
		// …and the value itself, with its type (ibValue::Serialize): a client edits by the type — a date as a date —
		// and may answer a Change with the value instead of text. One that does not travel (an open object) is shown
		// by its text alone.
		if (value.IsTransferable())
			value.Serialize(state.Child(wxT("Value")));
	}

	// TextEditMode off or a binding that cannot be written: the text is shown but not typed into, and
	// Select / Clear are locked with it; Open stays live.
	state.SetValue(wxT("ReadOnly"), IsReadOnly());
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

	// The text the field shows is formatted from the value on the next Update — nothing is pushed.
	m_formOwner->RefreshForm();

	return true;
}

ibChoiceHolder ibValueTextCtrl::GetChoiceHolder() const
{
	return m_formOwner != nullptr ? ibChoiceHolder(m_formOwner->GetSourceObject()) : ibChoiceHolder();
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
