
#include "widgets.h"
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

bool ibValueStaticText::LoadData(ibReaderMemory& reader)
{
	m_propertyMarkup->SetValue(reader.r_u8());
	m_propertyWrap->SetValue(reader.r_u32());
	wxString label; reader.r_stringZ(label);
	m_propertyTitle->SetValue(label);
	return ibValueWindow::LoadData(reader);
}

bool ibValueStaticText::SaveData(ibWriterMemory& writer)
{
	writer.w_u8(m_propertyMarkup->GetValueAsBoolean());
	writer.w_u32(m_propertyWrap->GetValueAsUInteger());
	writer.w_stringZ(m_propertyTitle->GetValueAsString());

	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStaticText, "Statictext", "Widget", string_to_clsid("CT_STTX"));