////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : window object
////////////////////////////////////////////////////////////////////////////

#include "window.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"
#include "frmserver/visualView/layers/commandBar.h"  // ibValueCommandBar — the command STORE


//***********************************************************************************
//*                                    ValueWindow                                  *
//***********************************************************************************

ibValueWindow::ibValueWindow() : ibValueControl()
{
}

//***********************************************************************************
//*                                  OnUpdate                                     *
//***********************************************************************************

void ibValueWindow::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// A control with no owner form (one built by hand, never placed) counts its form as enabled; the
	// control's own property still decides.
	ibValueForm* ownerForm = GetOwnerForm();
	const bool formEnabled = (ownerForm == nullptr) ? true : ownerForm->IsFormEnabled();

	state.SetValue(wxT("Enabled"), m_propertyEnabled->GetValueAsBoolean() && formEnabled);

	// A source-bound control with NO source picked is never shown — it is unusable until bound. IsUnbound is
	// a base virtual — false for plain windows, overridden by the source controls — so no cross-cast is
	// needed. Nor is an element that is NOT AVAILABLE (control.h): the functional options of this base switch
	// it off, by the field it stands on or by the options it names itself. Two questions, asked side by side.
	state.SetValue(wxT("Visible"), m_propertyVisible->GetValueAsBoolean() && IsAvailable() && !IsUnbound());
	state.SetValue(wxT("Tooltip"), m_propertyTooltip->GetValueAsTranslateString());
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueWindow::ReadData(const ibDataNode& node)
{
	m_propertyMinSize->SetNodeValue(node.GetProperty(m_propertyMinSize->GetName()));
	m_propertyMaxSize->SetNodeValue(node.GetProperty(m_propertyMaxSize->GetName()));
	m_propertyFont->SetNodeValue(node.GetProperty(m_propertyFont->GetName()));
	m_propertyFG->SetNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->SetNodeValue(node.GetProperty(m_propertyBG->GetName()));
	m_propertyTooltip->SetNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyEnabled->SetNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueWindow::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinSize->GetName(), m_propertyMinSize->GetNodeValue());
	node.SetProperty(m_propertyMaxSize->GetName(), m_propertyMaxSize->GetNodeValue());
	node.SetProperty(m_propertyFont->GetName(), m_propertyFont->GetNodeValue());
	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueControl::WriteData(node);
}

//***********************************************************************************
//*                    ValueWindow — composite (chrome) variant                     *
//***********************************************************************************

// ibBackendCommandSender — a composite control (a tablebox) is a command source: the form's command walk
// descends into it, and it TERMINATES on a standard action of its OWN bus (the composite is the action's runtime;
// the caller runs CallAsAction on it). A plain control / sizer is NOT a command source (it doesn't inherit this).
bool ibValueWindowComposite::GetCommandByHop(const ibCommandHop& hop, ibValue& out)
{
	auto actions = GetStandardCommands(GetTypeForm());
	if (!actions.GetNameByID((ibActionID)hop.m_id).IsEmpty()) {
		out = static_cast<const ibValue*>(this);   // this composite IS the action runtime; caller runs CallAsAction(id)
		return true;
	}
	return false;
}

ibValueWindowComposite::ibValueWindowComposite() : ibValueWindow()
{
	// The frame owns the command-bar STORE and points it back at itself. Non-composite
	// controls never create one, so their GetCommandBar() stays null.
	m_commandBar = new ibValueCommandBar();
	m_commandBar->SetOwner(this);
}

void ibValueWindowComposite::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);
	// A suppressed bar is simply not in the state: the form's own bar carries those commands.
	if (HasCommandBar())
		GetCommandBar()->Update(state.Child(wxT("CommandBar")), host);
}

bool ibValueWindowComposite::WriteData(ibDataNode& node) const
{
	// "Layers" block — the command bar is the first (and today only) layer; a search field /
	// status bar would add more sub-nodes here without touching the call sites.
	if (ibValueCommandBar* bar = GetCommandBar())
		bar->WriteData(node.Child(wxT("Layers")).Child(wxT("CommandBar")));
	return ibValueWindow::WriteData(node);
}

bool ibValueWindowComposite::ReadData(const ibDataNode& node)
{
	if (ibValueCommandBar* bar = GetCommandBar()) {
		if (const ibDataNode* layers = node.FindChild(wxT("Layers"))) {
			if (const ibDataNode* barNode = layers->FindChild(wxT("CommandBar")))
				bar->ReadData(*barNode);
		}
	}
	return ibValueWindow::ReadData(node);
}