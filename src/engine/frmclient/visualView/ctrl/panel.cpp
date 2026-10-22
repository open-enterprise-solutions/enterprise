#include "panel.h"

#include "frmclient/visualView/pageWindow.h"   // ibPanelPage — plain wxPanel + wxBoxSizer

//***********************************************************************************
//*                                 Value Panel                                     *
//***********************************************************************************

void ibValuePanel::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_panel = new ibPanelPage(parent, wxID_ANY);
	m_panel->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
}

void ibValuePanel::Update(const ibProtocolNode& node)
{
	m_panel->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));

	UpdateWindow(m_panel, node);
}

wxWindow* ibValuePanel::GetWindow() const
{
	return m_panel;
}

wxSizer* ibValuePanel::GetSizer() const
{
	return m_panel != nullptr ? m_panel->GetSizer() : nullptr;   // what Create built
}
