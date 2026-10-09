
#include "widgets.h"
#include "form.h"                             // ibValueForm — the bound read goes through the owning form
#include "backend/srcDataObject.h"            // ibSourceDataObject IS-A ibSourceObject (the upcast below)
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                              StaticText                                  *
//****************************************************************************

ibValueStaticText::ibValueStaticText() : ibValueWindow(), ibTypeControlFactory()
{
}

//****************************************************************************
//*                      the optional source behind it                       *
//****************************************************************************

ibSourceObject* ibValueStaticText::GetSourceObject() const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceObject() : nullptr;
}

bool ibValueStaticText::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
}

const ibMetaData* ibValueStaticText::GetMetaData() const
{
	return ibValueControl::GetMetaData();
}

wxString ibValueStaticText::GetControlTitle() const
{
	// The Title when it is filled, otherwise the bound field's synonym — the same rule the text
	// box and the checkbox follow, so a bound label is captioned by the metadata and nobody types
	// "Counterparty" beside a field already called that. Unbound, this is simply the Title, which
	// is what a decoration has always shown.
	if (!m_propertyTitle->IsEmptyProperty())
		return m_propertyTitle->GetValueAsTranslateString();

	if (!m_propertySource->IsEmptyProperty()) {
		const ibBackendAbstractColumn* column = GetSourceAbstractColumn();
		if (column != nullptr)   // null when the bound field is gone / whole-attribute binding
			return column->GetSynonym();
	}

	return wxEmptyString;
}

bool ibValueStaticText::GetControlValue(ibValue& pvarControlVal) const
{
	if (m_propertySource->IsEmptyProperty() || m_formOwner == nullptr)
		return false;

	// The same read every bound control does — a direct field or a dotted walk, decided by the
	// path, not by this control.
	return m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), pvarControlVal);
}

// The click handler lives in statictextEvent.cpp, beside every other control's — see
// checkboxEvent.cpp / textctrlEvent.cpp.

void ibValueStaticText::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);

	// The CAPTION is always the caption — Title, or the bound field's synonym. Unbound, that is the
	// whole control: the value text is empty and takes no room. Bound, the value sits beside it and is
	// the part that leads somewhere.
	ibValue value;
	const bool read = GetControlValue(value);

	state.SetValue(wxT("Caption"), GetControlTitle());
	state.SetValue(wxT("Text"), read ? value.GetString() : ibString());

	// A link only where there is something to open: an empty value is plain text, because a link
	// that leads nowhere is worse than no link.
	state.SetValue(wxT("Link"), read && !value.IsEmpty());

	// Multi-line captions ride on explicit '\n' in the text; word-wrap at a pixel width is not
	// supported (Wrap / Markup are kept on the metaobject for backward compatibility and have no
	// effect).
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibValueStaticText::ReadData(const ibDataNode& node)
{
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	m_propertyTitleLocation->SetNodeValue(node.GetProperty(m_propertyTitleLocation->GetName()));
	m_propertyMarkup->SetNodeValue(node.GetProperty(m_propertyMarkup->GetName()));
	m_propertyWrap->SetNodeValue(node.GetProperty(m_propertyWrap->GetName()));
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueStaticText::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyTitleLocation->GetName(), m_propertyTitleLocation->GetNodeValue());
	node.SetProperty(m_propertyMarkup->GetName(), m_propertyMarkup->GetNodeValue());
	node.SetProperty(m_propertyWrap->GetName(), m_propertyWrap->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());

	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticText, "Statictext", "Widget", control_to_clsid("CT_STTX"));
