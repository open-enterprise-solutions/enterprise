////////////////////////////////////////////////////////////////////////////
//	The available-fields tree — one answer to "which fields does this have"
////////////////////////////////////////////////////////////////////////////

#include "frmclient/win/dlgs/settings/settingsFieldTree.h"

// ON THE CLIENT ONE ROAD IS WALKED — the fields DESCRIBED: the server names them (ibDialogListSettings), with their
// types. A source that describes itself and a query text read for its fields are the configuration's to answer; a
// reference unfolding into its target's fields is asked of the server, where the configuration is.

#include "frmclient/backend/serialize/dataProtocol.h"                       // a field's type, as the server writes it
#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — what a reference points at
#include "frmclient/mainFrame/mainFrame.h"                                  // ibFrontendMainFrame::Respond — a target's fields asked
#include "protocol/protocol.h"

#include <wx/dialog.h>
#include <wx/dnd.h>
#include <wx/imaglist.h>
#include <wx/sizer.h>
#include <wx/tokenzr.h>
#include <wx/wupdlock.h>

// ---------------------------------------------------------------------------
// Tree-item payload for one source field: its dot-path (technical names), the leaf
// id + type (so a filter row edits its value through the runtime), and the referenced
// target ids for lazy expansion.
// ---------------------------------------------------------------------------
struct ibSourceFieldNode : public wxTreeItemData {
	wxString              m_path;          // technical: Reference.Number — what the query is built from
	// READABLE, and accumulated the SAME way: "Reference.Number". A field two hops
	// deep presented by its leaf alone ("Number") loses the only thing that says
	// WHICH number it is — which is why a dot-walked field read as a plain one.
	wxString              m_presentation;
	ibMetaID              m_leafId = wxNOT_FOUND;
	ibTypeDescription     m_type;
	std::vector<ibMetaID> m_refTypes;   // non-empty => reference field, lazy-expand
	bool                  m_loaded = false;
};

// Append each field of `explorer` under `parent`, carrying the accumulated dot-path.
// A reference field gets a dummy [+] and expands lazily (ExpandSourceFieldNode); a
// tabular section is skipped (a setting binds a scalar / reference field, not a section).
// ⭐ ONE PICTURE, ADDED ONCE. A tree's image list is by INDEX, so the same icon asked for twice
// would be two entries and a growing list on every refill. Icons compare by handle here, which is
// enough: a metaobject vends the same icon object to everyone who asks.
//
// A null icon (a field with no column behind it) answers 0 — the attribute picture the list is
// seeded with, which is what such a field has always worn.
static int ibSettingsFieldIcon(wxTreeCtrl* tree, const wxIcon& icon)
{
	wxImageList* images = tree->GetImageList();
	if (images == nullptr || !icon.IsOk())
		return 0;

	// The list is small (the kinds a source publishes), so a walk is cheaper than a map that would
	// have to be cleared with the tree.
	for (int i = 0; i < images->GetImageCount(); ++i)
		if (images->GetIcon(i).IsSameAs(icon))
			return i;
	return images->Add(icon);
}

// ⭐ THE RESOURCE PICTURE — THE METAOBJECT'S OWN (Max, 2026-08-22: take the resource from the
// metaobject). A resource is a registered metatype (`ibValueMetaObjectResource`) and it vends its
// class icon like every other; drawing a mark of our own would be a second picture for a thing that
// already has one, and it would drift from the one the metadata tree shows.
static int ibSettingsResourceIcon(wxTreeCtrl* tree)
{
	// The metaobject's own picture is the configuration's: a list has no resources, a composition's are the report's step.
	return ibSettingsFieldIcon(tree, wxIcon());
}

// THE FIELDS OF A REFERENCE'S TARGETS — asked of the server, which holds the configuration (Act Expand): the window it
// asked stays open, and it asks it again with them (Expanded), and says which of their types are references.
static std::vector<ibSettingsPlainField> AskTargetFields(const std::vector<ibMetaID>& targets)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || targets.empty())
		return {};

	ibProtocolNode act;
	act.SetValue(ibProtocolName::Act, static_cast<long long>(ibProtocolListSettingsAct::Expand));
	for (const ibMetaID& target : targets)
		act.AddItem(ibProtocolName::Targets, static_cast<long long>(target));
	if (!frame->Respond(act))
		return {};

	const ibProtocolNode request = frame->GetRequest();
	ibValueReferenceDataObject::ReadReferences(request.FindChild(ibProtocolName::References));
	return ibReadSettingsFields(request.FindChild(ibProtocolName::Expanded));
}

// Lazily build a reference field node's children (its target's fields). An EMPTY typed
// reference-as-source vends the target's explorer; its fields are copied into tree nodes
// synchronously, so the temporary reference can die after.
static void ExpandSourceFieldNode(wxTreeCtrl* tree, const wxTreeItemId& item, const ibMetaData* metaData,
	const std::function<bool(const wxString&)>& isResource = nullptr)
{
	ibSourceFieldNode* data = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(item));
	if (data == nullptr || data->m_refTypes.empty() || data->m_loaded)
		return;
	// FILL FIRST, THEN DROP THE DUMMY — a fruitless attempt keeps its [+] and can be retried (the desktop's).
	const size_t before = tree->GetChildrenCount(item, false);

	// ⭐⭐ ONE NAME, ONE FIELD — AND ALL THE NAMES THERE ARE: what the targets have, united, is the server's to work out
	// (the configuration is there); it answers with them in the order the targets declare them.
	for (const ibSettingsPlainField& col : AskTargetFields(data->m_refTypes)) {
		ibSourceFieldNode* child = new ibSourceFieldNode();
		child->m_path = data->m_path + wxT(".") + col.m_name;
		const wxString label = col.m_presentation.IsEmpty() ? col.m_name : col.m_presentation;
		child->m_presentation = data->m_presentation.IsEmpty() ? label : data->m_presentation + wxT(".") + label;
		child->m_leafId = col.m_id;
		child->m_type = col.m_type;
		child->m_refTypes = ibValueReferenceDataObject::ConvertToMetaIds(col.m_type.GetClsidList(), metaData);
		const bool resource = isResource && isResource(child->m_path);
		const int icon = resource ? ibSettingsResourceIcon(tree) : 0;
		const wxTreeItemId added = tree->AppendItem(item, label, icon, icon, child);
		if (!child->m_refTypes.empty())
			tree->AppendItem(added, wxEmptyString);   // dummy -> [+] (a reference expands into its target's fields)
	}

	if (tree->GetChildrenCount(item, false) <= before)
		return;   // nothing came back — keep the [+] and let the user try again

	// Drop the placeholder (it is the FIRST child, the real fields were appended after it).
	wxTreeItemIdValue cookie;
	const wxTreeItemId dummy = tree->GetFirstChild(item, cookie);
	if (dummy.IsOk() && tree->GetItemData(dummy) == nullptr)
		tree->Delete(dummy);
	data->m_loaded = true;
}

std::vector<ibSettingsPlainField> ibReadSettingsFields(const ibProtocolNode& fields)
{
	std::vector<ibSettingsPlainField> read;
	for (const ibProtocolNode& described : fields.Children()) {
		ibSettingsPlainField field;
		field.m_name = described.GetString(ibProtocolName::Name);
		field.m_presentation = described.GetString(ibProtocolName::Presentation);
		field.m_id = static_cast<ibMetaID>(described.GetInt(ibProtocolName::Id, wxNOT_FOUND));
		auto type = std::make_shared<ibDataNode>();
		ibReadProtocolNode(described.FindChild(ibProtocolName::Type), *type);
		ibTypeDescriptionMemory::ReadNode(ibDataValue::Child(type), field.m_type);
		field.m_available = described.GetBool(ibProtocolName::Available, true);
		read.push_back(std::move(field));
	}
	return read;
}

// ===========================================================================
//  ibSettingsFieldTree
// ===========================================================================

const ibMetaData* ibSettingsFieldTree::GetMetaData() const
{
	return m_metaData;
}

// Root an available-fields tree. ALWAYS via a source EXPLORER when the thing IS a source, so a
// REFERENCE field gets a [+] and expands into its target's fields — exactly like advpropSource's
// picker. Only a source that cannot describe itself falls back to flat fields (and even those get a
// [+] when their type is a reference).
void ibSettingsFieldTree::Populate(wxTreeCtrl* tree) const
{
	if (tree == nullptr)
		return;
	// ⚠ REBUILT, not built once. Changing the arbitrary query changes which fields exist, and this
	// runs again to say so — so the old rows go first.
	//
	// ⚠⚠ AND THE ROOT IS REUSED, never re-added. `DeleteAllItems` does NOT destroy the root of a
	// wxTR_HIDE_ROOT tree — MSW keeps a VIRTUAL root object for it — so a second `AddRoot` walks
	// straight into `assert "!m_pVirtualRoot" failed in AddRoot(): tree can have only a single root`.
	// Building the tree once hid this; rebuilding it on every query change is what found it.
	wxWindowUpdateLocker hold(tree);

	wxTreeItemId root = tree->GetRootItem();
	if (root.IsOk())
		tree->DeleteChildren(root);   // the rows go, the root the control insists on stays
	else
		root = tree->AddRoot(wxEmptyString);

	// Attribute icon (index 0) on every field — same icon the source-explorer picker uses.
	wxImageList* imgs = new wxImageList(16, 16);
	imgs->Add(ibValue::GetIconGroup());
	tree->AssignImageList(imgs);

	// Flat fields — the ones the server described.
	for (const ibSettingsPlainField& field : m_plain) {
		if (!field.m_available)
			continue;   // known, so a line on it is recognised — and not offered
		ibSourceFieldNode* data = new ibSourceFieldNode();
		data->m_path     = field.m_name;
		data->m_leafId   = field.m_id;
		// Same two questions as the explorer path above: a condition holds VALUES, and a branch walks
		// into what a value may be.
		data->m_type     = field.m_type;
		data->m_refTypes = ibValueReferenceDataObject::ConvertToMetaIds(field.m_type.GetClsidList(), GetMetaData());
		// ⭐ A RESOURCE WEARS THE RESOURCE PICTURE HERE TOO. The question was asked on the explorer
		// road above and NOT on this one — and this is the road a composition over a QUERY takes, so
		// on every report standing on author's text a resource looked like a plain field (Max,
		// 2026-08-29: "I do not see even minimally that it is a resource").
		const int icon = (m_isResource && m_isResource(data->m_path))
			? ibSettingsResourceIcon(tree) : 0;
		// THE TITLE IS WHAT A USER READS, as the synonym is on the explorer road above; the name is what
		// the path is built from and what a pick stores.
		const wxString label = field.m_presentation.IsEmpty() ? field.m_name : field.m_presentation;
		data->m_presentation = label;
		const wxTreeItemId item = tree->AppendItem(root, label, icon, icon, data);
		if (!data->m_refTypes.empty())
			tree->AppendItem(item, wxEmptyString);   // dummy -> [+]
	}
}

// Walk a dotted path down the tree, loading each reference on the way, and land the
// cursor on the leaf. The path is the technical one (that is what a field stores).
wxTreeItemId ibSettingsFieldTree::FindByPath(wxTreeCtrl* tree, const wxString& path) const
{
	if (tree == nullptr || path.IsEmpty())
		return wxTreeItemId();
	wxTreeItemId parent = tree->GetRootItem();
	wxStringTokenizer parts(path, wxT("."));
	while (parts.HasMoreTokens() && parent.IsOk()) {
		const wxString segment = parts.GetNextToken();
		wxTreeItemIdValue cookie;
		wxTreeItemId child = tree->GetFirstChild(parent, cookie);
		wxTreeItemId found;
		while (child.IsOk()) {
			const ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(child));
			if (node != nullptr) {
				const wxString last = node->m_path.AfterLast(wxT('.'));
				if ((last.IsEmpty() ? node->m_path : last).IsSameAs(segment, false)) {
					found = child;
					break;
				}
			}
			child = tree->GetNextChild(parent, cookie);
		}
		if (!found.IsOk())
			return wxTreeItemId();
		if (parts.HasMoreTokens())
			ExpandSourceFieldNode(tree, found, GetMetaData(), m_isResource);   // the road continues — load it
		parent = found;
	}
	return parent != tree->GetRootItem() ? parent : wxTreeItemId();
}

// ⭐ THE SAME QUESTION THE TREE ASKS OF A FIELD BEFORE PUTTING IT UP (ibFieldIsOfferable), asked of a
// path already written — and walked the way a composition's run walks it, so a line this window lists is
// one the run prints: from the fields this tree was given (where the path starts, a linked package's
// `Sales.Qty` included), then each hop by its TYPE. A start nobody here knows answers yes — not knowing is
// no verdict, and what the source no longer has is pruned elsewhere.
bool ibSettingsFieldTree::IsAvailable(const wxString& path) const
{
	if (path.IsEmpty())
		return true;   // a line with no field yet is a line being written

	// Where the path starts — among the fields this tree was given, with whether the options leave it. Each hop after it
	// is walked by its type on the server (the engine's WalkPath); a start nobody here knows answers yes — not knowing is
	// no verdict.
	const wxString start = path.BeforeFirst(wxT('.'));
	for (const ibSettingsPlainField& plain : m_plain) {
		if (plain.m_name.IsSameAs(start, false))
			return plain.m_available;
	}
	return true;
}

void ibSettingsFieldTree::SelectByPath(wxTreeCtrl* tree, const wxString& path) const
{
	const wxTreeItemId found = FindByPath(tree, path);
	if (found.IsOk()) {
		tree->SelectItem(found);
		tree->EnsureVisible(found);   // …which opens every road above it
	}
}

// The two behaviours every field tree has. What a DOUBLE-CLICK does is the host's
// business — it differs per tab — so that binding stays outside.
void ibSettingsFieldTree::Attach(wxTreeCtrl* tree)
{
	if (tree == nullptr)
		return;

	// Lazily expand a reference field — the tree that fired the event is the one to expand.
	tree->Bind(wxEVT_TREE_ITEM_EXPANDING, [this, tree](wxTreeEvent& e) {
		ExpandSourceFieldNode(tree, e.GetItem(), GetMetaData(), m_isResource);
		e.Skip();
	});

	// Start dragging a field OUT; dropping on the host's right-hand pane adds it (the dropped field
	// is the remembered m_dragItem — same-process drag, the text payload is a stub).
	tree->Bind(wxEVT_TREE_BEGIN_DRAG, [this, tree](wxTreeEvent& e) {
		ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(e.GetItem()));
		if (node == nullptr || node->m_leafId == wxNOT_FOUND)
			return;   // only a real field is draggable
		m_dragItem = e.GetItem();
		wxTextDataObject data(node->m_path);
		wxDropSource source(tree);
		source.SetData(data);
		source.DoDragDrop(wxDrag_CopyOnly);
	});
}

// ONE NODE READ AS A FIELD. The label the user sees is the presentation; the leaf id and type are
// what let the OTHER side of a condition edit its value through the runtime.
ibValueCompositionField* ibSettingsFieldTree::FieldAt(const wxTreeCtrl* tree, const wxTreeItemId& item)
{
	if (tree == nullptr || !item.IsOk())
		return nullptr;
	const ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(item));

	// A NODE WITH NO PATH IS NOT A FIELD — the lazy-expand placeholder under a reference, which
	// carries no data at all and is what this guard is really for.
	//
	// 🛑 IT TESTED THE LEAF ID. That id is the QUERYABLE COLUMN's, and a field read off a query TEXT
	// has none — so the moment the composer's picker started from the parsed text rather than from a
	// running source, EVERY field answered "a road, not a field" and nothing could be picked at all
	// (Max, 2026-08-24: "cannot set a value"). Having no column id is a fact about the source, not
	// about whether the row is a field.
	if (node == nullptr || node->m_path.IsEmpty())
		return nullptr;

	// NOT AN ibValuePtr HERE. Wrapping the field in a smart pointer and then handing THAT to ibValue
	// picks a different constructor than the one meant for a value object — the caller ends up
	// holding something that is not the field. The field is refcounted by the value that takes it,
	// so the raw object is what travels.
	ibValueCompositionField* field = new ibValueCompositionField(node->m_path, node->m_presentation);
	field->SetTypeInfo(node->m_leafId, node->m_type);
	return field;
}

// ===========================================================================
//  The FIELD PICKER — the available-fields tree as a form
// ===========================================================================
//
// A field is a VALUE. Choosing one is therefore choosing a value, and it happens
// where every other value choice happens: the Select button of the cell. What
// opens is this form — the same tree the tab shows on the left, because there is
// only one answer to "which fields does this source have".
ibValueCompositionField* ibSettingsFieldTree::ChooseField(wxWindow* parent, const wxString& currentPath) const
{
	wxDialog dlg(parent, wxID_ANY, _("Select field"),
		wxDefaultPosition, wxSize(320, 420),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	wxTreeCtrl* tree = new wxTreeCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	Populate(tree);
	// References expand by the SAME rule the tabs use — a deep path
	// (Supplier.Region.Country) has to be reachable from here too, or the picker
	// would be a worse door than the tree beside it.
	// EXPANDING IS HANDLED HERE, in the picker's own lambda, and the node is filled
	// BEFORE wx decides what to draw. Routing it through the host's method left
	// the picker's tree refusing to unfold while the identical tree on the tab
	// behind it worked — the same handler, a different window.
	tree->Bind(wxEVT_TREE_ITEM_EXPANDING, [tree, this](wxTreeEvent& e) {
		ExpandSourceFieldNode(tree, e.GetItem(), GetMetaData(), m_isResource);
		e.Skip();
	});
	// A CLICK ON THE ARROW is not the only way in: selecting a reference loads its
	// fields too, so the node is ready by the time the user reaches for the arrow.
	//
	// ⚠ AND IT COSTS WHAT IT LOOKS LIKE IT COSTS. This was suspected of being the lag and was taken
	// out for a while; the lag was a reference reading its (non-existent) ROW to be asked what fields
	// it has — the load default, one storey down. Filling a node is a metadata walk, which is what
	// this was always meant to be.
	tree->Bind(wxEVT_TREE_SEL_CHANGED, [tree, this](wxTreeEvent& e) {
		ExpandSourceFieldNode(tree, e.GetItem(), GetMetaData(), m_isResource);
		e.Skip();
	});

	// DOUBLE-CLICK ON A REFERENCE OPENS IT; double-click on a FIELD chooses it.
	//
	// This is what "the picker does not work" was: a reference is ALSO a field (it
	// has a leaf id), so double-clicking it — the natural way to go deeper — closed
	// the window and picked the reference itself. Expanding by the little arrow is
	// the only thing that ever worked, and it is not what anyone does.
	tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [&dlg, tree, this](wxTreeEvent& e) {
		const wxTreeItemId item = e.GetItem();
		if (!item.IsOk())
			return;
		const ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(item));
		if (node != nullptr && !node->m_refTypes.empty()) {
			ExpandSourceFieldNode(tree, item, GetMetaData(), m_isResource);
			tree->Expand(item);
			return;   // a reference is a ROAD — going down it is not choosing it
		}
		dlg.EndModal(wxID_OK);
	});

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(tree, 1, wxALL | wxEXPAND, dlg.FromDIP(6));
	sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, dlg.FromDIP(6));
	dlg.SetSizer(sizer);

	// THE TREE GETS THE FOCUS, and the cursor stands on the field the cell already
	// holds. Without this the focus lands on the OK button: the tree looks alive but
	// answers no key and no arrow, which reads exactly as "it does not expand".
	// Standing on the current value also means re-picking starts where the user is,
	// instead of at the top of a list they have to re-read.
	dlg.Bind(wxEVT_INIT_DIALOG, [tree, currentPath, this](wxInitDialogEvent& e) {
		tree->SetFocus();
		if (!currentPath.IsEmpty())
			SelectByPath(tree, currentPath);
		else if (tree->GetRootItem().IsOk()) {
			wxTreeItemIdValue cookie;
			const wxTreeItemId first = tree->GetFirstChild(tree->GetRootItem(), cookie);
			if (first.IsOk())
				tree->SelectItem(first);
		}
		e.Skip();
	});

	if (dlg.ShowModal() != wxID_OK)
		return nullptr;

	return FieldAt(tree, tree->GetSelection());
}
