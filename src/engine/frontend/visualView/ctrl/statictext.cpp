
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueStaticText, ibValueWindow)

//****************************************************************************
//*                              StaticText                                  *
//****************************************************************************

ibValueStaticText::ibValueStaticText() : ibValueWindow()
{
}

wxObject* ibValueStaticText::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxStaticText* staticText = new wxStaticText(wxparent, wxID_ANY,
		m_propertyTitle->GetValueAsTranslateString(),
		wxDefaultPosition,
		wxDefaultSize);

	return staticText;
}

void ibValueStaticText::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueStaticText::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxStaticText* staticText = dynamic_cast<wxStaticText*>(wxobject);

	if (staticText != nullptr) {
			
		staticText->SetLabel(m_propertyTitle->GetValueAsTranslateString());
		staticText->Wrap(m_propertyWrap->GetValueAsUInteger());
		
		if (m_propertyMarkup->GetValueAsBoolean() != false) {
			staticText->SetLabelMarkup(m_propertyTitle->GetValueAsTranslateString());
		}
	}

	UpdateWindow(staticText);
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

bool ibValueStaticText::SaveData(ibWriterMemory writer)
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