
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#else
#include "frontend/win/ctrls/controlStaticText.h"
#endif


//****************************************************************************
//*                              StaticText                                  *
//****************************************************************************

ibValueStaticText::ibValueStaticText() : ibValueWindow()
{
}

wxObject* ibValueStaticText::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	// Parenting is done by the host walker after Create returns — mirrors
	// the wx pattern where the owning container adopts the new object.
	return new ibWebStaticText(m_propertyTitle->GetValueAsTranslateString());
#else
	ibControlStaticText* staticText = new ibControlStaticText(wxparent, wxID_ANY,
		m_propertyTitle->GetValueAsTranslateString(),
		wxDefaultPosition,
		wxDefaultSize);

	return staticText;
#endif
}

void ibValueStaticText::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueStaticText::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibControlStaticText* staticText = dynamic_cast<ibControlStaticText*>(wxobject);

	if (staticText != nullptr) {

		staticText->SetLabel(m_propertyTitle->GetValueAsTranslateString());

		// ibControlStaticText renders multi-line labels via explicit '\n'
		// in the source text; word-wrap at a pixel width is not supported
		// (the Wrap and SetLabelMarkup properties are kept on the meta
		// object for backward compatibility but have no effect here).
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
	m_propertyMarkup->ReadNodeValue(node.GetProperty(m_propertyMarkup->GetName()));
	m_propertyWrap->ReadNodeValue(node.GetProperty(m_propertyWrap->GetName()));
	m_propertyTitle->ReadNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueStaticText::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMarkup->GetName(), m_propertyMarkup->GetNodeValue());
	node.SetProperty(m_propertyWrap->GetName(), m_propertyWrap->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());

	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticText, "Statictext", "Widget", string_to_clsid("CT_STTX"));