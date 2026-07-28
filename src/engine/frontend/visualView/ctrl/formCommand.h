#ifndef __FORM_COMMAND_H__
#define __FORM_COMMAND_H__

// -----------------------------------------------------------------------
// ibFormCommandValue — a FORM-LOCAL command (an event), managed exactly like a form attribute.
//
// A form command is NOT a metaobject and never touches the metadata tree. It is the "button pressed" event moved
// off the button onto a named command the button DELEGATES to: its Action is a REAL event property (ibEventControl,
// exactly like a control's OnChange) whose value names a FORM-RUNTIME handler a projection runs by name
// (ibFrontendCommandReceiver → CallAsEvent). The inspector renders it with the standard event editor — the button opens /
// generates the handler procedure in the form module and jumps to it, and edits route through ModifyEvent, just as a
// control (and the non-control layer object) do.
//
// It IS an ibValueLayerObject: the same non-control property object that hangs off an owner FRAME and borrows its
// metadata + designer routing (GetMetaData / OnPropertyChanged / OnEventChanged, all shared through GetOwnerFrame()).
// The form command reuses that machinery wholesale instead of re-deriving it — it only adds its own properties (Name
// / Caption / Picture), its Action event, and its form-unique id.
//
// This is ONE of three "tables" the command navigator/picker splices together: form commands (this, managed) vs
// standard actions vs global config commands (both id-hops, read-only). Their natures stay separate.
// -----------------------------------------------------------------------

#include "frontend/visualView/layerObject.h"                     // ibValueLayerObject — the shared base
#include "backend/propertyManager/property/eventControl.h"       // ibEventControl — the Action event

class BACKEND_API ibDataNode;
class FRONTEND_API ibValueForm;

// Form-command ids are allocated from this HIGH base so a form-command hop id never collides with a standard action
// id (~100+) or a metaobject / source metaID — the uniform command walk probes an untyped hop id against the form's
// own commands, and a distinct id space keeps that probe unambiguous (see ibValueForm::NextFormCommandId).
constexpr ibMetaID kFormCommandIdBase = 0x40000000;

class FRONTEND_API ibFormCommandValue : public ibValueLayerObject {

	friend class ibValueForm;

public:

	explicit ibFormCommandValue(ibValueForm* ownerForm = nullptr);
	ibFormCommandValue(const ibFormCommandValue&) = delete;
	ibFormCommandValue& operator=(const ibFormCommandValue&) = delete;

	// --- ibValueLayerObject ---
	virtual ibValueFrame* GetOwnerFrame() const override;   // → the owner form (metadata + designer routing)
	virtual bool IsLayerContainer() const override { return false; }   // a leaf — no children

	// --- value identity ---
	virtual wxString GetClassName() const override { return ibValue::GetClassName(); }   // registered value type
	virtual wxString GetObjectTypeName() const override { return GetName(); }
	virtual bool IsEditable() const override;   // → owner form (a view-only form's commands aren't editable)
	// Reject a duplicate Name — unique among the OWNER FORM's commands (the command-side twin of a bar item's Name
	// guard). The inspector edits m_propertyName directly, bypassing RenameFormCommand, so the check lives here.
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;

	// The command's own look / behaviour (a projection reads these; its own Title/Picture win on top). The Action
	// is the event whose VALUE names the form-runtime handler the command runs:
	wxString GetName()      const { return m_propertyName->GetValueAsString(); }
	// The fully qualified name, the SAME scheme metaobject commands use (see ibValueMetaObject::GetFullName) — a
	// form command is rooted at the form: "Form.<name>". One naming rule for menu, sections and form.
	wxString GetFullName()  const { return wxT("Form.") + GetName(); }
	wxString GetCaption()   const { return m_propertyCaption->IsEmptyProperty() ? GetName() : m_propertyCaption->GetValueAsTranslateString(); }
	wxString GetProcedure() const { return m_eventCommand->GetValue().GetString(); }
	// The Action as the EVENT object (not just its name), so the fire path dispatches through
	// GetDispatcher() -> named handler OR lambda, exactly like a control event. GetProcedure stays
	// for the string view (serialize / inspector).
	ibEvent* GetActionEvent() const { return m_eventCommand; }
	wxBitmap GetPictureBitmap() const { return m_propertyPicture->GetValueAsBitmap(); }
	bool     IsEmptyPicture()   const { return m_propertyPicture->IsEmptyProperty(); }
	ibMetaID GetId() const { return m_commandId; }

	void SetCommandName(const wxString& name) { m_propertyName->SetValue(name); }
	void SetProcedure(const wxString& proc)   { m_eventCommand->SetValue(proc); }
	void SetCommandId(const ibMetaID& id)     { m_commandId = id; }

	// Serialize the properties + the Action event (base ibPropertyObject::WriteProperty covers only ATTACHED
	// objects — each own property/event is written by hand, exactly as a control / attribute does) PLUS the
	// form-unique id (not a property — kept on the entry).
	virtual bool ReadProperty(const ibDataNode& node) override;
	virtual bool WriteProperty(ibDataNode& node) const override;

	// Clipboard copy / paste — designer-only, OUR own format id (oes_clipboard_command), EXACTLY like an
	// attribute: Copy serializes the entry, Paste deserializes it as a fresh entry on `form` (unique name + id).
	static bool CopyToClipboard(const ibFormCommandValue* entry);
	static ibFormCommandValue* PasteFromClipboard(ibValueForm* form);
	static bool HasClipboardData();

private:

	ibValueForm* m_ownerForm;
	ibMetaID     m_commandId = wxNOT_FOUND;

	ibPropertyCategory* m_categoryCommon = ibPropertyObject::CreatePropertyCategory(wxT("Common"), _("General"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryCommon, wxT("Name"), _("Name"), _("Command name"), wxT(""));
	ibPropertyTString* m_propertyCaption = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCommon, wxT("Caption"), _("Caption"), _("Command caption"), wxT(""));
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryCommon, wxT("Picture"), _("Picture"));

	// The handler event — a parameterless form-runtime procedure (empty args → "Procedure <name>()"), run by the
	// projection through the string CallAsEvent. The inspector's event editor generates / opens it in the form module.
	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventCommand = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("Action"), _("Action"), wxArrayString{});
};

#endif // __FORM_COMMAND_H__
