#include "commandReceiver.h"

#include <vector>
#include "frontend/visualView/ctrl/frame.h"        // ibValueFrame — CallAsAction / GetActionCollection / children
#include "frontend/visualView/ctrl/form.h"         // ibValueForm — GetMetaData / FindFormCommandById; IS-A ibBackendValueForm
#include "frontend/visualView/ctrl/formCommand.h"   // ibFormCommandValue — a form command resolved BY ID (caption / Action)
#include "backend/metaCollection/metaCommandObject.h" // ibValueMetaObjectCommand — the leaf command
#include "backend/metaCollection/metaObject.h"     // g_metaCommandCLSID
#include "backend/backend_picture.h"                // ibBackendPicture::CreatePicture (a standard action's picture)
#include "backend/srcDataObject.h"                  // ibSourceDataObject — the form's main object (command parameter)
#include "backend/metaCollection/genericData.h"     // ibValueMetaObjectGenericData::GetMetaID (parameter type match)
#include "backend/typeDescription.h"                // ibTypeDescription::ContainType / GetClsidList (command parameter type)
#include "backend/compiler/value.h"                 // ibValue (the resolved command parameter)
#include "backend/backend_command.h"              // ibBackendCommandSender::ResolveCommandPath — the SERVER walk (starts on the form)
#include "frontend/visualView/layers/commandBar.h" // GatherFormCommands / ibCommandSourceEntry — reliable fallback for WalkCommand
#ifndef OES_USE_WEB
#include <wx/window.h>                              // wxWindow::FindFocus — the active control for a table-row parameter
#endif

// A control's current value, taken if its type is in `paramType` — a tablebox yields its selected ROW, another
// control its own value. True + fills `out` on a match.
static bool MatchControlValue(ibValueFrame* node, const ibTypeDescription& paramType, ibValue& out)
{
	ibValue value;
	if (node != nullptr && node->GetControlValue(value) && !value.IsEmpty() && paramType.ContainType(value.GetClassType())) {
		out = value;
		return true;
	}
	return false;
}

// Depth-first: the FIRST control on the form whose current value is of `paramType`. The fallback when no control
// is focused. Door-internal, the (b) branch of the command-parameter resolve.
static bool FindRowParameter(ibValueFrame* node, const ibTypeDescription& paramType, ibValue& out)
{
	if (node == nullptr)
		return false;
	if (MatchControlValue(node, paramType, out))
		return true;
	for (unsigned int i = 0; i < node->GetChildCount(); i++)
		if (FindRowParameter(node->GetChild(i), paramType, out))
			return true;
	return false;
}

#ifndef OES_USE_WEB
// The form control that owns the focus — the table / list the user is actually in, so its row wins over some
// other same-typed control elsewhere on the form. A control's widget is its GetWxObject; the focus may be a
// descendant of it (a grid cell inside a tablebox). nullptr when nothing on the form holds focus.
static ibValueFrame* FindFocusedControl(ibValueFrame* node, wxWindow* focus)
{
	if (node == nullptr || focus == nullptr)
		return nullptr;
	if (wxWindow* w = wxDynamicCast(node->GetWxObject(), wxWindow))
		if (w == focus || w->IsDescendant(focus))
			return node;
	for (unsigned int i = 0; i < node->GetChildCount(); i++)
		if (ibValueFrame* found = FindFocusedControl(node->GetChild(i), focus))
			return found;
	return nullptr;
}
#endif

// The command PARAMETER, resolved FROM FORM CONTEXT — a "parameterizable" command (its Parameter type names a
// reference) runs ON a value the form carries: the edited object's reference where its type matches (the object
// form), else the current row of an active table / list of that type (the list form). A non-reference (untyped)
// command takes no parameter. Returns true + fills `out`; the caller hands it to the command as CommandParameter.
static bool ResolveCommandParameter(ibValueForm* form, const ibValueMetaObjectCommand* cmd, ibValue& out)
{
	if (form == nullptr || cmd == nullptr)
		return false;
	const ibTypeDescription& paramType = cmd->GetParameterType();
	bool wantsRef = false;
	for (const ibClassID& c : paramType.GetClsidList())
		if (IsReference(c)) { wantsRef = true; break; }
	if (!wantsRef)
		return false;   // untyped / primitive-typed command — no parameter to deliver

	// (a) the edited object — the form's MAIN source, when its reference type IS the parameter type. An object
	// form's source object IS-A ibValue (a reference data object); the cross-base access mirrors cast_value.
	if (ibSourceDataObject* src = form->GetSourceObject())
		if (const ibValueMetaObjectGenericData* mo = src->GetSourceMetaObject())
			if (paramType.ContainType(reference_to_clsid(mo->GetMetaID())))
				if (ibValue* asValue = dynamic_cast<ibValue*>(src)) {
					out = ibValue(asValue);
					return true;
				}

	// (b) otherwise the current row of an active table / list carrying that type — the FOCUSED control wins (the
	// list the user is in), else the first matching control on the form.
#ifndef OES_USE_WEB
	if (ibValueFrame* focused = FindFocusedControl(form, wxWindow::FindFocus()))
		if (MatchControlValue(focused, paramType, out))
			return true;
#endif
	return FindRowParameter(form, paramType, out);
}

// The front-end door is now THIN — it does NOT resolve command paths (that is the SERVER walk's job). It STARTS
// ibBackendCommandSender::ResolveCommandPath on the FORM (the command source) and switches on the resolved LEAF:
// run it (execute), read its look (resolve), or expand it (sub-commands). The 5-case endpoint ladder is GONE — each
// hop self-describes on the backend (form -> its command / a config command / a section / a tablebox's action),
// exactly as a source path self-describes through GetValueBySourceHop.

// Start the SERVER walk on the form and RUN the resolved leaf.
bool ibFrontendCommandReceiver::ExecuteValueByPath(const ibCommandDescription& desc) const
{
	ibValueForm* const gate = GetCommandGateForm();   // the form is the command SOURCE + the ExecuteParameters a handler gets
	if (gate == nullptr || !desc.IsOk())
		return false;
	ibValue start;
	start = static_cast<const ibValue*>(gate);   // the FORM is the walk's entry (command source)
	ibValue leaf;
	if (!ibBackendCommandSender::ResolveCommandPath(start, desc.GetPath(), 0, leaf))
		return false;

	// (1) a FORM COMMAND — run its Action (a form-runtime procedure); an EMPTY Action is a placeholder.
	ibFormCommandValue* fc = nullptr;
	if (leaf.ConvertToValue(fc) && fc != nullptr) {
		const wxString proc = fc->GetProcedure();
		if (!proc.IsEmpty())
			gate->CallAsEvent(proc);
		return true;
	}
	// (2) a COMMAND / OBJECT ITEM (both ibBackendCommandItem) — Execute by the desc's command type. A command runs
	// its handler ON the form value its Parameter type names; an object item opens its own form. Execute is CONST.
	ibBackendCommandItem* item = nullptr;
	if (leaf.ConvertToValue(item) && item != nullptr) {
		ibValue cmdParam;
		bool hasParam = false;
		ibValueMetaObjectCommand* cmd = nullptr;
		if (leaf.ConvertToValue(cmd) && cmd != nullptr)
			hasParam = ResolveCommandParameter(gate, cmd, cmdParam);
		item->Execute(desc.GetCommandType(), gate, hasParam ? &cmdParam : nullptr);
		return true;
	}
	// (3) a STANDARD ACTION — the resolved FRAME (the form / a tablebox) is the action's runtime; run it there.
	ibValueFrame* frame = nullptr;
	if (leaf.ConvertToValue(frame) && frame != nullptr) {
		frame->CallAsAction((ibActionID)desc.GetLeaf(), gate);
		return true;
	}
	return false;
}

// The READ twin — start the SAME walk, then read the resolved leaf's own default look (caption + icon + modifies
// flag). outModifiesData is optional.
bool ibFrontendCommandReceiver::ResolveValueByPath(const ibCommandDescription& desc, wxString& outCaption, wxBitmap& outIcon,
	bool* outModifiesData, bool* outPictureAndText) const
{
	ibValueForm* const gate = GetCommandGateForm();
	if (gate == nullptr || !desc.IsOk())
		return false;
	ibValue start;
	start = static_cast<const ibValue*>(gate);
	ibValue leaf;
	if (!ibBackendCommandSender::ResolveCommandPath(start, desc.GetPath(), 0, leaf))
		return false;

	// Default DISPLAY: text + picture. Only a standard action carries its own "picture-only" default (Close / Update),
	// read below; form / metaobject / object commands always show their text.
	if (outPictureAndText != nullptr)
		*outPictureAndText = true;

	// (1) form command — its own Caption + Picture; a form command always modifies (it runs form code).
	ibFormCommandValue* fc = nullptr;
	if (leaf.ConvertToValue(fc) && fc != nullptr) {
		outCaption = fc->GetCaption();
		outIcon    = fc->IsEmptyPicture() ? wxNullBitmap : fc->GetPictureBitmap();
		if (outModifiesData != nullptr)
			*outModifiesData = true;
		return true;
	}
	// (2) command metaobject — rendered text = SYNONYM (name when empty); no picture -> text-only.
	ibValueMetaObjectCommand* cmd = nullptr;
	if (leaf.ConvertToValue(cmd) && cmd != nullptr) {
		outCaption = cmd->GetSynonym().IsEmpty() ? cmd->GetName() : cmd->GetSynonym();
		outIcon    = cmd->IsEmptyPicture() ? wxNullBitmap : cmd->GetPictureAsBitmap();
		if (outModifiesData != nullptr)
			*outModifiesData = cmd->GetModifiesData();
		return true;
	}
	// (3) object item (a business object, NOT a command) — its SYNONYM (+ a "Create" tag), icon BY METATYPE (GetIcon).
	ibValueMetaObject* obj = nullptr;
	if (leaf.ConvertToValue(obj) && obj != nullptr) {
		outCaption = obj->GetSynonym();
		if (desc.GetCommandType() == ibInterfaceCommandType_Create)
			outCaption += wxT(": ") + wxString(_("Create"));
		outIcon = wxBitmap(obj->GetIcon());
		return true;
	}
	// (4) standard action — its caption + picture + modifies flag off the resolved frame's bus.
	ibValueFrame* frame = nullptr;
	if (leaf.ConvertToValue(frame) && frame != nullptr) {
		auto actions = frame->GetActionCollection(frame->GetTypeForm());
		outCaption = actions.GetCaptionByID((ibActionID)desc.GetLeaf());
		const ibPictureDescription pic = actions.GetPictureByID((ibActionID)desc.GetLeaf());
		outIcon = pic.IsEmptyPicture() ? wxNullBitmap : ibBackendPicture::CreatePicture(pic, gate->GetMetaData());
		if (outModifiesData != nullptr)
			*outModifiesData = actions.GetModifiesDataByID((ibActionID)desc.GetLeaf());
		// A standard action's OWN default display — Close / Update are picture-only, Add / Post picture+text.
		if (outPictureAndText != nullptr)
			*outPictureAndText = actions.IsCreatePictureAndText((ibActionID)desc.GetLeaf());
		return true;
	}
	return false;
}

// HUB — the resolved leaf is a GROUP command when it holds sub-commands. The SAME walk resolves it; each sub-command
// comes back as a DIRECT 1-hop path (itself terminal). False (empty `out`) for a plain leaf.
bool ibFrontendCommandReceiver::ResolveSubCommands(const ibCommandDescription& desc, std::vector<ibCommandSubItem>& out) const
{
	ibValueForm* const gate = GetCommandGateForm();
	if (gate == nullptr || !desc.IsOk())
		return false;
	ibValue start;
	start = static_cast<const ibValue*>(gate);
	ibValue leaf;
	if (!ibBackendCommandSender::ResolveCommandPath(start, desc.GetPath(), 0, leaf))
		return false;
	ibValueMetaObjectCommand* group = nullptr;
	if (!leaf.ConvertToValue(group) || group == nullptr || !group->HasSubCommands())
		return false;
	for (ibValueMetaObject* sub : group->GetSubCommands()) {
		ibCommandSubItem item;
		item.desc    = ibCommandDescription(sub->GetMetaID());   // the sub-command is terminal on its own
		item.caption = sub->GetSynonym().IsEmpty() ? sub->GetName() : sub->GetSynonym();   // rendered menu text = SYNONYM
		item.icon    = wxBitmap(sub->GetIcon());
		out.push_back(item);
	}
	return !out.empty();
}

// THE one command resolve — see the header. EVERY projection (button Update / bar BuildCommands / the inspector
// cell via WalkCommand) routes through here, so existence + look are decided in ONE place, not guessed per-surface.
bool ibFrontendCommandReceiver::ResolveCommand(const ibCommandDescription& desc, wxString& outCaption,
	wxBitmap& outIcon, bool* outModifies, wxString* outPath, bool* outPictureAndText) const
{
	// (1) The walk on THIS door — the authoritative caption / icon / modifies / default display for a FORM or GLOBAL
	// command. picAndText defaults true (a command shows its text); a standard action overrides (Close = picture only).
	bool modifies = false;
	bool picAndText = true;
	const bool walked = ResolveValueByPath(desc, outCaption, outIcon, &modifies, &picAndText);
	bool exists = walked && !outCaption.IsEmpty();
	// The gather below is a FULL recursive config walk. Skip it on the hot path — the button's Update, the bar's
	// BuildCommands, the inspector cell's OnSetValue all pass outPath == nullptr, and when the walk already resolved a
	// real caption its icon / modifies / default-display are authoritative (the gather would only re-confirm existence).
	// Only the fragile tablebox case (walk resolves the hop but vends an EMPTY caption) and WalkCommand (needs the full
	// PATH name for the <not found> cell) fall through to the gather.
	if (exists && outPath == nullptr) {
		if (outModifies != nullptr) *outModifies = modifies;
		if (outPictureAndText != nullptr) *outPictureAndText = picAndText;
		return true;
	}
	// (2) The RELIABLE gather — a TABLEBOX command's walk resolves the hop but vends an empty caption / no icon
	// (fragile in the designer preview); the gather (the SAME set the picker offered) fills the readable name + icon
	// and PROVES the command still exists. Only a truly gone command misses BOTH.
	if (ibValueForm* form = GetCommandGateForm())
		for (const ibCommandSourceEntry& e : GatherFormCommands(form))
			if (e.desc == desc) {
				exists = true;
				if (outCaption.IsEmpty())               outCaption = e.label;      // readable name (button / bar text)
				if (!outIcon.IsOk() && e.icon.IsOk())   outIcon    = e.icon;
				if (outPath != nullptr)                 *outPath   = e.fullName.IsEmpty() ? e.label : e.fullName;
				break;
			}
	if (outModifies != nullptr) *outModifies = modifies;
	if (outPictureAndText != nullptr) *outPictureAndText = picAndText;      // the command's OWN default display
	if (outPath != nullptr && outPath->IsEmpty()) *outPath = outCaption;   // form/global: no gather path -> the name
	return exists;
}

// ibBackendCommandReceiver — the command-source variant validates a binding here. Thin wrapper over ResolveCommand;
// the inspector cell shows the full PATH name, "<not found>" when the command is gone.
bool ibFrontendCommandReceiver::WalkCommand(const ibCommandDescription& desc, wxString* outText) const
{
	wxString caption; wxBitmap icon; wxString path;
	if (!ResolveCommand(desc, caption, icon, nullptr, &path))
		return false;
	if (outText != nullptr)
		*outText = path;
	return true;
}
