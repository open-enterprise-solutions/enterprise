#include "form.h"

#include <functional>

#include "frmclient/backend/serialize/dataProtocol.h"   // ibDataNode — a control's properties, as the wire carries them
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/visualView/visualHostClient.h"   // ibFormVisualDocument — the tab's document, by the host's tab

namespace {

// THE FORM AS IT STANDS NOW — itself and each control under it, under its id: where it stands among its siblings (the
// form among none), and what the properties a person may arrange hold — the server's own setting's shape
// (frmserver/settings/formSettings.cpp, ibSaveFormSettings, which walks the form itself too).
void WriteControl(const ibValueFrame* control, long long position, ibProtocolNode controls)
{
	ibProtocolNode node = controls.AddChild();
	node.SetValue(ibProtocolName::Control, control->GetControlID())
		.SetValue(ibProtocolName::Position, position);

	ibDataNode properties;
	for (const wxString& name : ibValueFrame::GetAllowedUserProperty()) {
		const ibProperty* const property = control->GetProperty(name);
		ibDataValue value;
		if (property != nullptr && property->CopyNodeValue(value))
			properties.SetProperty(name, value);
	}
	ibWriteProtocolNode(properties, node);

	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
		WriteControl(control->GetChild(idx), static_cast<long long>(idx), controls);
}

} // namespace

ibValueFrame* ibValueForm::GetActiveControl() const
{
	if (m_activeControl == nullptr)
		return nullptr;

	// One of the form's own still — a frame of another shape built them anew.
	std::function<bool(const ibValueFrame*)> holds = [&](const ibValueFrame* frame) {
		for (unsigned int idx = 0; idx < frame->GetChildCount(); idx++) {
			const ibValueFrame* const child = frame->GetChild(idx);
			if (child == m_activeControl || holds(child))
				return true;
		}
		return false;
	};
	return holds(this) ? m_activeControl : nullptr;
}

void ibValueForm::SetActiveControl(ibValueFrame* control)
{
	if (control == m_activeControl)
		return;
	m_activeControl = control;
	if (control != nullptr)
		control->Send(ibProtocolEvent::Focus);
}

void ibValueForm::UpdateForm()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	ibProtocolNode response;
	response.SetValue(ibProtocolName::Act, static_cast<long long>(ibProtocolFormEditorAct::Apply));
	WriteControl(this, wxNOT_FOUND, response.Child(ibProtocolName::Controls));

	// …and drawn anew by the answer: the controls read what they hold from the frame, as they always do.
	frame->Respond(response);
}

bool ibValueForm::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	m_propertyFG->SetNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->SetNodeValue(node.GetProperty(m_propertyBG->GetName()));
	m_propertyEnabled->SetNodeValue(node.GetProperty(m_propertyEnabled->GetName()));

	// Its attributes and its commands are the server's — the form here is what is drawn.
	return ibValueFrame::ReadData(node);
}

bool ibValueForm::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());

	return ibValueFrame::WriteData(node);
}

ibFormVisualDocument* ibValueForm::GetVisualDocument() const
{
	// The desktop's finds it by the form's key; a tab here is the form drawn, so by the tab its host draws in.
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr ? frame->FindDocument(m_host.GetTabId()) : nullptr;
}

ibValueFrame* ibValueForm::FindControlByID(long long controlId) const
{
	// The form itself among them, as the desktop's DoFindControlByID has it.
	std::function<ibValueFrame*(ibValueFrame*)> find = [&](ibValueFrame* top) -> ibValueFrame* {
		for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
			if (ibValueFrame* const found = find(top->GetChild(idx)))
				return found;
		}
		return top->GetControlID() == controlId ? top : nullptr;
	};
	return find(const_cast<ibValueForm*>(this));
}
