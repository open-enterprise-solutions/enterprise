#include "formEditor.h"

#include "backend/backend_mainFrame.h"
#include "backend/backend_picture.h"
#include "backend/compiler/value.h"
#include "backend/picturePredefined.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "frmserver/settings/formSettings.h"
#include "frmserver/visualView/ctrl/form.h"

#include "protocol/protocol.h"   // ibProtocolRequestKind::FormEditor, ibProtocolFormEditorAct

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
		request.SetValue(ibProtocolName::Kind, static_cast<s32>(ibProtocolRequestKind::FormEditor));
		request.SetValue(ibProtocolName::Picture, wxString(ibBackendPicture::GetServerPicture(g_picChangeFormCLSID).GetData()));
		request.SetValue(ibProtocolName::HasSetting, ibHasFormSettings(m_owner));
		if (result != wxNOT_FOUND)
			request.SetValue(ibProtocolName::Result, static_cast<s32>(result));

		// The controls' classes and their pictures — what the desktop's editor lists its tree's icons from.
		ibDataNode& classes = request.Child(ibProtocolName::Classes);
		for (const ibCtorAbstractType* ctor : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control)) {
			ibDataNode& described = classes.AddChild(0, 0);
			described.SetValue(ibProtocolName::Name, ctor->GetClassName());
			described.SetValue(ibProtocolName::Picture, wxString(ibBackendPicture::GetServerPicture(ctor->GetClassType()).GetData()));
		}

		ibDataNode response;
		if (!frame->Request(request, response) || response.FindField(ibProtocolName::Act) == nullptr)
			return wxID_OK;   // closed

		switch (static_cast<ibProtocolFormEditorAct>(response.GetValue<s32>(ibProtocolName::Act))) {
		case ibProtocolFormEditorAct::Apply:
			if (const ibDataNode* const controls = response.FindChild(ibProtocolName::Controls))
				ibApplyFormSettings(m_owner, *controls);
			m_owner->UpdateForm();
			result = wxNOT_FOUND;
			break;
		case ibProtocolFormEditorAct::Save:
			result = static_cast<long long>(ibSaveFormSettings(m_owner));
			break;
		case ibProtocolFormEditorAct::Reset:
			result = static_cast<long long>(ibResetFormSettings(m_owner));
			break;
		default:
			return wxID_OK;   // an act this server does not know — taken for closed
		}
	}
}
