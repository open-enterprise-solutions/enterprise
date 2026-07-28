#include "advpropCommandSource.h"

#include "backend/propertyManager/property/propertyCommandSource.h"            // ibPropertyCommandSource (registered here)
#include "backend/propertyManager/property/variant/variantCommandSource.h"    // ibVariantDataCommandSource (the cell value)
#include "backend/backend_command.h"            // ibBackendCommandReceiver — the backend factory the committed variant casts to

#include "frontend/propertyManager/property/private/prop.h"                    // wxPGPropertyFlags_*, property_cast
#include "frontend/propertyManager/property/private/propertyRegistry.h"        // ibPropertyRegistry::Register
#include "frontend/propertyManager/propertyEditor.h"

#include "frontend/visualView/layers/commandBar.h"   // ibFrontendCommandReceiver (the door) + GatherFormCommands / ibCommandSourceEntry
#include "backend/metaCollection/attribute/metaAttributeObject.h"   // ibValueMetaObjectAttribute::GetIconGroup (tree default icon)

#include <map>   // std::map — pre-create the fixed command sections (shown even empty)
#include <wx/treectrl.h>

// -----------------------------------------------------------------------
// ibPGCommandSourceProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGCommandSourceProperty, wxPGProperty, TextCtrlAndButton)

// register the frontend editor for the backend command-source property (mirrors ibPropertySourceLoader)
class ibPropertyCommandSourceLoader
{
public:
	ibPropertyCommandSourceLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyCommandSource* prop) -> wxPGProperty* {
			return new ibPGCommandSourceProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
} g_commandSourceLoader;

ibPGCommandSourceProperty::ibPGCommandSourceProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& name, const wxVariant& value) : wxPGProperty(label, name), m_propertyObject(property)
{
	m_flags |= wxPGPropertyFlags_ActiveButton;   // the "…" button is always live
	SetValue(value);
}

// Resolve the command DOOR from a command-source property's owner — the owner IS-A door (a button / the bar), or a
// bar ITEM that delegates to its bar. The door WALKS a binding (ResolveValueByPath) and vends the gate form. Null
// when not reachable.
static const ibFrontendCommandReceiver* ResolveDoor(const ibPropertyObject* propObj)
{
	if (const ibFrontendCommandReceiver* door = dynamic_cast<const ibFrontendCommandReceiver*>(propObj))
		return door;                                     // a BUTTON / the command BAR is-a door
	if (const ibValueCommandBarItem* item = dynamic_cast<const ibValueCommandBarItem*>(propObj))
		return item->GetBar();                           // a bar ITEM isn't a door — delegate to its bar
	return nullptr;
}

wxString ibPGCommandSourceProperty::ValueToString(wxVariant& variant, wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	// Just the variant's own live string — MakeString casts the owner to ibBackendCommandReceiver and WALKS the binding
	// SERVER-SIDE (existence + name), so the "<not found>" check is identical at runtime. No resolve logic here;
	// exactly as the source cell returns variant.GetString().
	ibVariantDataCommandSource* data = property_cast(variant, ibVariantDataCommandSource);
	wxString s;
	if (data != nullptr)
		data->Write(s);
	return s;
}

void ibPGCommandSourceProperty::OnSetValue()
{
	m_valueBitmapBundle = wxBitmapBundle();   // no bound command -> no image (cell shows text only)
	wxVariant v = GetValue();                 // keep the variant alive so the cast target outlives the read
	ibVariantDataCommandSource* data = property_cast(v, ibVariantDataCommandSource);
	if (data == nullptr || !data->GetCommandDesc().IsOk())
		return;

	// The bound command's OWN picture, so the cell shows WHICH command is pulled. Resolve it through THE one point
	// (door->ResolveCommand — walk + reliable gather fallback), the SAME resolve the button and bar use. Left empty
	// when the command has no picture or was deleted.
	const ibCommandDescription& desc = data->GetCommandDesc();
	const ibFrontendCommandReceiver* door = ResolveDoor(m_propertyObject);
	if (door == nullptr)
		return;
	wxString caption; wxBitmap icon;
	if (door->ResolveCommand(desc, caption, icon, nullptr) && icon.IsOk())
		m_valueBitmapBundle = icon;
}

bool ibPGCommandSourceProperty::StringToValue(wxVariant& WXUNUSED(variant), const wxString& WXUNUSED(text),
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return false;   // read-only cell — the value is set only through the dialog
}

wxPGEditorDialogAdapter* ibPGCommandSourceProperty::GetEditorDialog() const
{
	// One dialog-tree node = one command source (its hop path + display name). Group nodes carry an empty desc.
	class ibTreeItemCommand : public wxTreeItemData {
	public:
		ibTreeItemCommand(const ibCommandDescription& desc, const wxString& label) : m_desc(desc), m_label(label) {}
		ibCommandDescription m_desc;
		wxString             m_label;
	};

	class ibPGEditorCommandSourceDialogAdapter : public wxPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGCommandSourceProperty* dlgProp = wxDynamicCast(prop, ibPGCommandSourceProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			// The property object that owns a command source IS-A command door (a BAR ITEM or a BUTTON) — ask the
			// door (ResolveDoor, the SAME lookup the cell's live display uses) for its gate FORM. The form vends the
			// command sources (own / main / tables / general), exactly what the navigator gathers — ONE gather.
			const ibFrontendCommandReceiver* door = ResolveDoor(dlgProp->GetPropertyObject());
			ibValueForm* form = door != nullptr ? door->GetCommandGateForm() : nullptr;
			if (form == nullptr)
				return false;

			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice command"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);
			dlg->SetFont(pg->GetFont());
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;

			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxTreeCtrl* tc = new wxTreeCtrl(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
				wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);
			tc->SetDoubleBuffered(true);
			topsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));

			// Each command shows ITS OWN picture (the command determines the icon — GatherFormCommands resolved it);
			// index 0 is the group / default icon.
			wxImageList* images = new wxImageList(16, 16);
			images->Add(ibValueMetaObjectAttribute::GetIconGroup());   // 0 = group / default
			tc->AssignImageList(images);

			// Grouped tree of the form's command sources (entries arrive grouped in order; new group on change).
			const wxTreeItemId root = tc->AddRoot(wxEmptyString);
			// Pre-create ALL three sections (form / standard / global) so they show even EMPTY, the SAME
			// GetCommandSections list the navigator uses.
			std::map<wxString, wxTreeItemId> groups;
			for (const wxString& section : GetCommandSections())
				groups[section] = tc->AppendItem(root, section, 0, 0);
			std::map<wxString, wxTreeItemId> subgroups;   // "section\x1fsubgroup" -> node (Standard nests per source)
			wxTreeItemId selNode;
			// the currently-bound command (re-open selects it) — read from the cell's own value, not the owner.
			// Keep the variant in a local so the cast target outlives the read (GetValue returns a temporary).
			wxVariant curVal = dlgProp->GetValue();
			ibCommandDescription current;
			if (ibVariantDataCommandSource* data = property_cast(curVal, ibVariantDataCommandSource))
				current = data->GetCommandDesc();
			for (const ibCommandSourceEntry& e : GatherFormCommands(form)) {
				wxTreeItemId& sectionNode = groups[e.group];
				if (!sectionNode.IsOk())   // defensive: an unexpected section -> append it at the end
					sectionNode = tc->AppendItem(root, e.group, 0, 0);
				wxTreeItemId parent = sectionNode;
				if (!e.subgroup.IsEmpty()) {   // nest under a per-source SUB-group (the form / a table)
					wxTreeItemId& sub = subgroups[e.group + wxT("\x1f") + e.subgroup];
					if (!sub.IsOk())
						sub = tc->AppendItem(sectionNode, e.subgroup, 0, 0);
					parent = sub;
				}
				const int icon = e.icon.IsOk() ? images->Add(e.icon) : 0;   // the command's own picture, else default
				// LEAF text = the NAME (the tree's folders carry the path); the CELL value stored = the full PATH.
				const wxTreeItemId leaf = tc->AppendItem(parent, e.label, icon, icon,
					new ibTreeItemCommand(e.desc, e.fullName.IsEmpty() ? e.label : e.fullName));
				if (current.IsOk() && current == e.desc)
					selNode = leaf;   // re-open on the currently bound command
			}
			tc->ExpandAll();   // sections + their sub-groups all open
			if (selNode.IsOk()) {
				tc->SelectItem(selNode);
				tc->EnsureVisible(selNode);
			}

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));
			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);
			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(360, 320)));
				dlg->Move(pg->GetGoodEditorDialogPosition(dlgProp, dlg->GetSize()));
			}
			tc->SetFocus();

			const int res = dlg->ShowModal();
			const wxTreeItemId sel = tc->GetSelection();
			bool applied = false;
			if (res == wxID_OK && sel.IsOk()) {
				// A GROUP node has no desc (IsOk() false) — ignored, so only a real command commits.
				if (ibTreeItemCommand* d = dynamic_cast<ibTreeItemCommand*>(tc->GetItemData(sel))) {
					if (d->m_desc.IsOk()) {
						// Wire the OWNER factory into the committed variant so it keeps validating live (WalkCommand) —
						// a pick-then-delete in the same session must still flip to "<not found>", never a stale cache.
						const ibBackendCommandReceiver* owner =
							dynamic_cast<const ibBackendCommandReceiver*>(dlgProp->GetPropertyObject());
						SetValue(new ibVariantDataCommandSource(owner, d->m_desc, d->m_label));
						applied = true;
					}
				}
			}
			dlg->Destroy();
			return applied;
		}
	};

	return new ibPGEditorCommandSourceDialogAdapter();
}
