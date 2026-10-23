#include "formEditor.h"

#include "backend/backend_mainFrame.h"
#include "backend/compiler/value.h"
#include "core/picturePredefined.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "backend/backend_picture.h"              // ibBackendPicture::SendPicture — the pictures, by their ids
#include "frmserver/client/propertyInspector.h"   // a property's row, as the inspector writes it
#include "frmserver/settings/formSettings.h"
#include "frmserver/visualView/ctrl/form.h"

#include "protocol/protocol.h"   // ibProtocol::ibRequest::ibKind::FormEditor, ibProtocol::ibRequest::ibFormEditor::ibAct

namespace {

// WHAT A PERSON MAY ARRANGE — cut here, by the desktop's list (ibValueFrame::GetAllowedUserProperty): each control
// under its id, the rows of those it has (a sizer item's are its own), and the controls in it. The client's controls
// declare none: they show what this says.
void AppendControl(ibDataNode& controls, ibValueFrame* control)
{
	ibDataNode& described = controls.AddChild(0, 0);
	described.SetValue(ibProtocol::ibRequest::ibFormEditor::Control, control->GetControlID());
	for (const wxString& name : ibValueFrame::GetAllowedUserProperty()) {
		if (ibProperty* const property = control->GetProperty(name))
			ibPropertyInspector::AppendProperty(described, property);
	}

	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
		AppendControl(controls, control->GetChild(idx));
}

} // namespace

ibDialogFormEditor::ibDialogFormEditor(ibValueForm* valueForm) : m_owner(valueForm)
{
}

int ibDialogFormEditor::ShowModal()
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || m_owner == nullptr)
		return wxID_CANCEL;

	// THE WINDOW STAYS OPEN THROUGH EVERY ACT BUT THE LAST, as the desktop's does: each act is an answer, done here, and
	// the window asked again — how a save or a reset went (Result) said with it.
	long long result = wxNOT_FOUND;
	for (;;) {
		ibDataNode request;
		request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::FormEditor));
		request.SetValue(ibProtocol::ibRequest::ibFormEditor::Picture, ibBackendPicture::SendPicture(g_picChangeFormCLSID));
		request.SetValue(ibProtocol::ibRequest::ibFormEditor::HasSetting, ibHasFormSettings(m_owner));
		if (result != wxNOT_FOUND)
			request.SetValue(ibProtocol::ibRequest::ibFormEditor::Result, static_cast<std::int32_t>(result));

		// The controls' classes and their pictures — what the desktop's editor lists its tree's icons from.
		ibDataNode& classes = request.Child(ibProtocol::ibRequest::ibFormEditor::Classes);
		for (const ibCtorAbstractType* ctor : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control)) {
			ibDataNode& described = classes.AddChild(0, 0);
			described.SetValue(ibProtocol::ibRequest::ibFormEditor::Name, ctor->GetClassName());
			described.SetValue(ibProtocol::ibRequest::ibFormEditor::Picture, ibBackendPicture::SendPicture(ctor->GetClassType()));
		}
		// …and the form's controls, what a person may arrange of each — as they stand now: asked again after each act.
		AppendControl(request.Child(ibProtocol::ibRequest::ibFormEditor::Controls), m_owner);

		ibDataNode response;
		if (!frame->Request(request, response) || response.FindField(ibProtocol::ibRequest::ibFormEditor::Act) == nullptr)
			return wxID_OK;   // closed

		switch (static_cast<ibProtocol::ibRequest::ibFormEditor::ibAct>(response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibFormEditor::Act))) {
		case ibProtocol::ibRequest::ibFormEditor::ibAct::Apply:
			if (const ibDataNode* const controls = response.FindChild(ibProtocol::ibRequest::ibFormEditor::Controls))
				ibApplyFormSettings(m_owner, *controls);
			m_owner->UpdateForm();
			result = wxNOT_FOUND;
			break;
		case ibProtocol::ibRequest::ibFormEditor::ibAct::Save:
			result = static_cast<long long>(ibSaveFormSettings(m_owner));
			break;
		case ibProtocol::ibRequest::ibFormEditor::ibAct::Reset:
			result = static_cast<long long>(ibResetFormSettings(m_owner));
			break;
		default:
			return wxID_OK;   // an act this server does not know — taken for closed
		}
	}
}
