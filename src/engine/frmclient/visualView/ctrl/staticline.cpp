////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a line of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // the properties, as the frame carries them

void ibValueStaticLine::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_staticLine = new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		static_cast<long>(node.GetInt(ibProtocolName::Orient, wxLI_HORIZONTAL)));
}

void ibValueStaticLine::Update(const ibProtocolNode& node)
{
	UpdateWindow(m_staticLine, node);
}

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData                          *
//*******************************************************************************************

bool ibValueStaticLine::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueStaticLine::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

