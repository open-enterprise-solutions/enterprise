
#include "widgets.h"
#include "form.h"                             // ibValueForm — the bound read goes through the owning form
#include "backend/srcDataObject.h"            // ibSourceDataObject IS-A ibSourceObject (the upcast below)
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#else
#include "frontend/win/ctrls/controlStaticText.h"
#include "frontend/win/ctrls/controlStaticTextValue.h"   // caption + clickable value, when bound
#endif


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

wxObject* ibValueStaticText::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	// Parenting is done by the host walker after Create returns — mirrors
	// the wx pattern where the owning container adopts the new object.
	return new ibWebStaticText(m_propertyTitle->GetValueAsTranslateString());
#else
	// ONE widget for both cases. Bound, it shows a caption and a value; unbound, the value half is
	// simply empty and takes no room, so the caption is the whole control — which is exactly what
	// a decoration is. Two widgets would have meant deciding at creation time and rebuilding the
	// control whenever somebody bound or unbound it in the designer.
	return new ibControlStaticTextValue(wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
#endif
}

void ibValueStaticText::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifndef OES_USE_WEB
	// Only the value-showing widget ever sends a click — the plain label has nothing to open, so
	// there is nothing to bind on it.
	if (ibControlStaticTextValue* valueText = dynamic_cast<ibControlStaticTextValue*>(wxobject))
		valueText->Bind(wxEVT_BUTTON, &ibValueStaticText::OnHyperlinkClicked, this);
#endif
}

void ibValueStaticText::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibControlStaticTextValue* staticText = dynamic_cast<ibControlStaticTextValue*>(wxobject);

	if (staticText != nullptr) {

		// The CAPTION is always the caption — Title, or the bound field's synonym. Unbound, that
		// is the whole control; bound, the value sits beside it and is the part that leads
		// somewhere.
		ibValue value;
		const bool read = GetControlValue(value);

		staticText->SetLabel(GetControlTitle());
		staticText->SetValueText(read ? value.GetString() : wxString());

		// A link only where there is something to open: an empty value is plain text, because a
		// link that leads nowhere is worse than no link.
		staticText->SetHyperlink(read && !value.IsEmpty());

		staticText->SetWindowStyle(m_propertyTitleLocation->GetValueAsInteger() == 1
			? wxALIGN_LEFT : wxALIGN_RIGHT);

		// Multi-line captions ride on explicit '\n' in the text; word-wrap at a pixel width is not
		// supported (Wrap / Markup are kept on the metaobject for backward compatibility and have
		// no effect here).
	}

	UpdateWindow(staticText);
#endif
}

void ibValueStaticText::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
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
