
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueStaticText, IValueWindow)

//****************************************************************************
//*                              StaticText                                  *
//****************************************************************************

CValueStaticText::CValueStaticText() : IValueWindow()
{
}

wxObject* CValueStaticText::Create(wxObject* parent, IVisualHost* visualHost)
{
	wxStaticText* staticText = new wxStaticText((wxWindow*)parent, wxID_ANY,
		m_propertyLabel->GetValueAsString(),
		wxDefaultPosition,
		wxDefaultSize);

	return staticText;
}

void CValueStaticText::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueStaticText::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxStaticText* staticText = dynamic_cast<wxStaticText*>(wxobject);

	if (staticText != NULL) {
		staticText->SetLabel(m_propertyLabel->GetValueAsString());
		staticText->Wrap(m_propertyWrap->GetValueAsInteger());
		if (m_propertyMarkup->GetValueAsBoolean() != false) {
			staticText->SetLabelMarkup(m_propertyLabel->GetValueAsString());
		}
	}

	UpdateWindow(staticText);
}

void CValueStaticText::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool CValueStaticText::LoadData(CMemoryReader& reader)
{
	m_propertyMarkup->SetValue(reader.r_u8());
	m_propertyWrap->SetValue(reader.r_u32());
	wxString label; reader.r_stringZ(label);
	m_propertyLabel->SetValue(label);
	return IValueWindow::LoadData(reader);
}

bool CValueStaticText::SaveData(CMemoryWriter& writer)
{
	writer.w_u8(m_propertyMarkup->GetValueAsBoolean());
	writer.w_u32(m_propertyWrap->GetValueAsInteger());
	writer.w_stringZ(m_propertyLabel->GetValueAsString());

	return IValueWindow::SaveData(writer);
}
