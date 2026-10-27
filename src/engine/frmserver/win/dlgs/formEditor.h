#ifndef __FORM_EDITOR_H__
#define __FORM_EDITOR_H__

class ibValueForm;

// THE FORM EDITOR — the old desktop's ibDialogFormEditor, its window the client's
// (frmclient/win/dlgs/formEditor.h): the client edits its own copy of the form's controls, read from the frame as the
// desktop's are read from the form, and each act of the window is its answer (ibProtocol::ibRequest::ibKind::FormEditor) —
// Apply lays the arrangement over this form as a saved setting is laid (ibApplyFormSettings) and draws it again; Save and
// Reset are the setting's own doors. The form's ChangeForm asks it as the desktop's does.
class ibDialogFormEditor {
public:

	ibDialogFormEditor(ibValueForm* valueForm);

	int ShowModal();

private:

	ibValueForm* m_owner;
};

#endif
