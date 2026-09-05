////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree window
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"
#include <wx/wupdlock.h>   // wxWindowUpdateLocker — the Freeze/Thaw pair taken as a guard
#include "backend/fileKind.h"   // extensions live in one table, not at each call site

#include "mainFrame/mainFrameDesigner.h"   // the documents are the window's — see the verbs below
#include "backend/backend_exception.h"     // an engine refusal arrives as an exception
#include "backend/mcp/mcpTool.h"           // ibMcpActing — who is the source, before anything asks
#include "backend/metadataConfiguration.h" // SaveDatabase / the four apply stages
#include "backend/restructureInfo.h"       // …and the ledger the decision reads
#include "frontend/mainFrame/objinspect/objinspect.h"
#include "frontend/docView/docView.h"
#include "backend/appData.h"
#include "backend/appEnv.h"
#include "backend/session/session.h"        // ibSession::CurrentFrame — the chrome a Changed stage repaints
#include "backend/metaCollection/metaCommandObject.h"   // ibValueMetaObjectCommand::GetSubCommands (hub — nested commands)
#include "backend/metaCollection/metaComposerObject.h"  // the report's composers, as tree items

#include <wx/intl.h>  // wxGetTranslation — the layout table holds SOURCE strings, translated on use

#include <iterator>   // std::rbegin / std::rend over the layout table

// The COMMON folder is not a metatype's group — it is the band itself, so its label stays here.
// Every GROUP label moved into the layout table further down (s_groups), where it sits beside the
// metatype it names instead of in a block of defines that nothing tied to anything.
#define commonName _("Common")

// The labels of the groups INSIDE an object — attributes, tabular sections, forms, commands,
// templates. These are still written at each adder, because an adder is what decides that a
// register shows dimensions and resources while a catalog shows tabular sections.
#define	objectFormsName _("Forms")
#define	objectModulesName _("Modules")
#define	objectTemplatesName _("Templates")
#define objectAttributesName _("Attributes")
#define objectCommandsName _("Commands")
#define objectDimensionsName _("Dimensions")
#define objectResourcesName _("Resources")

#define objectComposersName _("Composers")
#define objectTablesName _("Tables")
#define objectEnumerationsName _("Enums")

//***********************************************************************
//*								metadata                                * 
//***********************************************************************

#include "frontend/mainFrame/mainFrame.h"

#include "frontend/win/dlgs/formSelector/formSelector.h"

// ⭐⭐ THE CYCLE, ONCE, FOR EVERY TREE — see the note in the header.
//
// The engine says only THAT the metadata changed, so the answer is to re-read. What it must not be
// is a re-read per change: Modify() fires on every property write, so creating one object is a
// dozen signals and a paste is hundreds. The flag collapses them — the first schedules, the rest
// find it already scheduled — and the rebuild happens once when the current work is done, which is
// also the only moment at which the metadata is in a state worth drawing.
void ibMetaTreeBase::MetaDataChanged()
{
	// ⭐ THE CHROME, AND NOTHING ELSE. This says only THAT the configuration is no longer what
	// you last read — so the asterisk follows the metadata's own flag, asked rather than carried.
	//
	// 🛑 IT USED TO RE-READ THE WHOLE TREE, and that was wrong twice over. It threw away every
	// row on every property write — which is what folded `Common` shut each time somebody typed
	// (Max, 2026-09-01: *"Common keeps collapsing"*, then *"you do not need to rebuild, you
	// already know what changed"*) — and it was a bigger hammer than the facts require: every
	// occasion that changes the SHAPE of the tree already names its object through a stage.
	if (ibMetaData* metaData = GetMetaData())
		Modify(metaData->IsModified());
}


// ============================================================================
//  The open list — a metaobject's data, drawn once, for all three trees
// ============================================================================

// 🛑 IT DOES NOT UNRESERVE, AND THAT WAS A REAL CRASH. `wxMenuItem` holds its id in a
// `wxWindowIDRef` (menuitem.h), so the moment an id from NewControlId is handed to Append the
// MENU owns it: reserved becomes referenced, and the reference is dropped when the menu dies at
// the end of the popup. Calling UnreserveControlId afterwards asserts inside wx - *"id already in
// use or not reserved"* - on the SECOND right-click, because the first block was already gone.
//
// So this only forgets. wx frees; we merely stop pointing.

// ⭐⭐ A LINE NEEDS SOMETHING ON BOTH SIDES OF IT. Seven places in this file append a separator and
// each decided on its own whether one belonged — so a menu whose earlier blocks all dropped out
// opened with a rule across its top, and two blocks that both dropped out left two rules with
// nothing between them (Max, 2026-09-01, on the configuration root's menu after a rollback:
// *"put a check on the separator"*).
//
// Asked of the MENU, which is the only thing that knows what is already in it — every caller keeps
// saying "a new block starts here" and stops having to work out whether that is visible.
static void ibAppendSeparator(wxMenu* menu)
{
	if (menu == nullptr)
		return;

	const size_t count = menu->GetMenuItemCount();
	if (count == 0)
		return;   // nothing above it — the line would be the first thing in the menu

	const wxMenuItem* last = menu->FindItemByPosition(count - 1);
	if (last != nullptr && last->IsSeparator())
		return;   // …and never two in a row

	menu->AppendSeparator();
}

bool ibMetaTreeBase::AppendMetaMenu(wxMenu* menu, ibValueMetaObject* metaObject)
{
	// The previous menu's block goes back now rather than at the end of the popup: the click is
	// dispatched from inside PopupMenu on some platforms and after it on others, and a block freed
	// while an id from it is still in flight is the kind of difference that only shows on one.

	if (menu == nullptr || metaObject == nullptr)
		return false;

	std::vector<ibMetaMenuItem> items;
	const bool standardSuppressed = metaObject->CollectContextMenu(items);

	if (items.empty())
		return standardSuppressed;

	for (size_t idx = 0; idx < items.size(); idx++) {

		const ibMetaMenuItem& item = items[idx];

		// THE LINE GOES WHERE THE KIND CHANGES — never where somebody asked for one.
		if (idx > 0 && items[idx - 1].m_kind != item.m_kind)
			ibAppendSeparator(menu);

		const int id = wxWindow::NewControlId();
		wxMenuItem* menuItem = menu->Append(id, item.m_caption);

		// Asked of the metaobject when there is one, taken from the item when there is not.
		if (item.m_metaObject != nullptr)
			menuItem->SetBitmap(item.m_metaObject->GetIcon());
		else if (item.m_picture != 0)
			menuItem->SetBitmap(ibBackendPicture::GetPicture(item.m_picture));

		// ⭐⭐ THE ITEM CARRIES ITS OWN ACTION, so nothing is remembered between building the menu
		// and clicking it. The item already NAMES what to open (Max, 2026-09-01: *"OpenObjectForm
		// is not needed either — we made the structure for exactly that"*), and the rest is a modal
		// editor with no metaobject, handed straight back to the object that offered it.
		//
		// 🛑 IT USED TO BE BOOKKEEPING: a block of ids from NewControlId(n), the vector of items and
		// the first id kept as MEMBERS, an index worked out by subtracting one from the other at
		// click time, and a ReleaseOpenItems to clear both by hand before the next menu was built
		// (Max: *"m_openIdFirst = wxID_NONE; m_openItems.clear() — THAT is the junk"*). Three
		// members and three methods to carry across a popup what the binding carries by itself.
		ibValueMetaObject* const opens = item.m_metaObject;
		const int command = item.m_id;

		menu->Bind(wxEVT_MENU, [this, opens, command, metaObject](wxCommandEvent&) {
			if (opens != nullptr)
				OpenObjectForm(opens);
			else if (command != wxNOT_FOUND)
				metaObject->ProcessCommand(command);
		}, id);
	}

	// …and the boundary to the tree's OWN block below, which is the tree's to draw and was the one
	// separator every one of the twenty-two menus used to append by hand.
	ibAppendSeparator(menu);

	return standardSuppressed;
}


// 🛑 A ROW YOU MAY NOT ASK ANYTHING OF — and there is exactly one of them per control.
//
// With `wxTR_HIDE_ROOT` (both external trees carry it) GetRootItem() answers with wx's VIRTUAL
// root: an id that passes IsOk(), survives every null check, and asserts the moment anything is
// asked of it — *"can't retrieve virtual root item"*, from inside GetItemData. It is not an empty
// tree, so counting items does not find it: the data processor's tree is full and its root is
// still virtual.
//
// ⚠ ITS CHILDREN ARE FINE. Only the row itself must not be questioned, so every walk below tests
// this before reading and descends either way.
static bool ibRowCanBeAsked(const wxTreeCtrl* ctrl, const wxTreeItemId& item)
{
	return ctrl != nullptr && item.IsOk()
		&& !(ctrl->HasFlag(wxTR_HIDE_ROOT) && item == ctrl->GetRootItem());
}

// ⭐⭐ ONE ROW FOR ONE OBJECT — the answer to `Created`, and the reason nothing is rebuilt.
//
// Max, 2026-09-01: *"you do not need to rebuild, you already know what changed"*. The stage carries
// the object, so the tree touches its row and leaves the other nine hundred alone — which is also
// what stopped `Common` folding shut on every keystroke, since nothing else is destroyed.
//

// ⭐⭐ A FORM IS ASKED WHAT KIND IT IS — by whoever has a person in front of them.
//
// 🛑 THIS LIVED ON THE CONFIGURATION TREE ALONE, and that is why a form created inside an
// external data processor or report came out with no kind and an empty layout (Max, 2026-09-01:
// *"it should ask for the form type too"*). The question is not the configuration's — it belongs to
// EVERY tree that shows metadata to somebody, so it is answered once, here.
//
// ⚠ ONLY A FORM THAT HAS NO KIND YET. A paste, a copy and a tool-made form all arrive here too,
// and every one of them already knows what it is — asking again would put a dialog in front of
// somebody who never pressed anything.
void ibMetaTreeBase::AskFormKind(ibValueMetaObject* object)
{
	if (object == nullptr || m_bReadOnly)
		return;

	ibValueMetaObjectForm* form = dynamic_cast<ibValueMetaObjectForm*>(object);
	if (form == nullptr || form->GetTypeForm() != wxNOT_FOUND)
		return;

	ibValueMetaObjectGenericData* owner =
		dynamic_cast<ibValueMetaObjectGenericData*>(form->GetParent());
	if (owner == nullptr)
		return;

	// THE DIALOG, right here. It stood in a `SelectFormType` of its own — virtual, and overridden by
	// nobody: a leftover from when the ENGINE asked this through the tree's interface. Its only
	// caller is this line, so the question and the asking are one verb again.
	ibDialogSelectTypeForm dlg(owner, form);
	const ibFormTypeList optList = owner->GetFormType();
	for (unsigned int idx = 0; idx < optList.GetItemCount(); idx++)
		dlg.AppendTypeForm(optList.GetItemName(idx), optList.GetItemLabel(idx), optList.GetItemId(idx));

	dlg.CreateSelector();

	const ibFormID chosen = dlg.ShowModal();
	if (chosen == wxNOT_FOUND)
		return;   // closed the dialog: the form stands, with its kind still to be chosen

	// Placed the way the object inspector places one: ask the owner, set, tell the owner — so
	// whatever watches a property change sees this one too.
	if (ibProperty* kind = form->GetProperty(wxT("FormType"))) {
		const wxVariant before = kind->GetValue();
		if (form->OnPropertyChanging(kind, wxVariant((long)chosen))) {
			kind->SetValue(wxVariant((long)chosen));
			form->OnPropertyChanged(kind, before, kind->GetValue());
		}
	}

	// …AND THE LAYOUT, which the owner builds once the kind is known. It used to sit in the same
	// branch as the question inside the engine, so skipping the ask skipped the build and the form
	// came out with nothing in it.
	owner->OnCreateFormObject(form);
}

// ONE WALK, THREE TREES. Every row that stands for a metaobject carries an ibTreeDataObject —
// the configuration navigator, the external report's and the external data processor's alike — so
// the search reads that and nothing tree-specific.
//
// ⚠ ONE COOKIE PER LEVEL. GetFirstChild / GetNextChild keep their place in the cookie, so a
// recursion that shared one would leave this level walking its parent's siblings — the same shape
// as a nested directory walk driven by one handle, which is a defect that hides on the platform
// where it happens to survive.
wxTreeItemId ibMetaTreeBase::FindItemByMetaObject(const ibValueMetaObject* object) const
{
	// ⚠ AN EMPTY TREE ANSWERS GetRootItem() WITH THE VIRTUAL ROOT — an id that passes IsOk() and
	// then asserts the moment anything is asked of it. Tested once, here, rather than in the walk.
	if (m_treeCtrl == nullptr || m_treeCtrl->GetCount() == 0)
		return wxTreeItemId();

	return FindItemByMetaObject(m_treeCtrl->GetRootItem(), object);
}

wxTreeItemId ibMetaTreeBase::FindItemByMetaObject(const wxTreeItemId& from,
	const ibValueMetaObject* object) const
{
	if (m_treeCtrl == nullptr || !from.IsOk() || object == nullptr)
		return wxTreeItemId();

	wxTreeCtrl* const walk = const_cast<wxTreeCtrl*>(m_treeCtrl);   // wx's child walk is non-const

	// …and the row itself is only questioned when it may be — see ibRowCanBeAsked. A hidden root
	// carries no metaobject anyway; what is under it does.
	if (ibRowCanBeAsked(walk, from))
		if (const ibTreeDataObject* data = dynamic_cast<ibTreeDataObject*>(walk->GetItemData(from)))
			if (data->m_metaObject == object)
				return from;

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = walk->GetFirstChild(from, cookie); child.IsOk();
		child = walk->GetNextChild(from, cookie)) {

		const wxTreeItemId found = FindItemByMetaObject(child, object);
		if (found.IsOk())
			return found;
	}

	return wxTreeItemId();
}

// ⭐⭐ THE WHOLE CYCLE, IN ONE PLACE, FOR ALL THREE TREES. Create, load, save, rename, delete —
// and copy and paste, which are a create and a rename arriving through a different door. A watcher
// answers each of them once, here, so a tree cannot know about an edit that reached it by one road
// and miss the same edit arriving by another.
//
// ⚠ EVERY BRANCH IS ALLOWED TO FIND NOTHING. A stage says what HAPPENED, not what is on screen:
// an object created under a collapsed branch has no row, and a load that fires before this tree was
// handed the metadata has no rows at all. Those are states, not failures.
void ibMetaTreeBase::MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object)
{
	// ⭐⭐ FROZEN FOR THE WHOLE OF IT (Max, 2026-09-01: *"so the tree does not flicker, it has to be
	// frozen the whole time"*). Every branch below changes the control — draws a row, takes a
	// branch away, relabels, empties and refills — and one stage can be several changes. The
	// control is what freezes: this class is the PANEL around it, and on MSW a freeze is
	// WM_SETREDRAW to one window, so freezing the panel leaves the tree repainting through all of
	// it. RAII, because half the branches return early.
	wxWindowUpdateLocker freeze;
	if (m_treeCtrl != nullptr)
		freeze.Lock(m_treeCtrl);

	switch (stage) {

	// READ IN — from a file, from the database, or rebuilt from scratch. Nothing that was on screen
	// still stands for anything, so this is the one stage where the tree is emptied and filled
	// again, with the two calls that have always done it. `Run` is the same answer to a different
	// fact: the tree is now ALIVE — every ctor registered, every reference resolved — and half of
	// what a row shows (icons, resolved types, a form's kind) is only true after that.
	//
	// 🛑 THIS WENT THROUGH A `RefreshWholeTree` OF MY OWN — a queued re-read that captured every
	// expanded path, cleared, refilled and reopened them. It was built when a re-read answered
	// EVERY change, which it no longer does, and it was a second version of a pair that already
	// worked (Max, 2026-09-01: *"we can close the tree, refresh it completely — we HAVE that
	// function. You invented some nonsense. When we work with metadata we add one, remove one,
	// change one — there is no need for any of it"*). Gone, with the expanded-path machinery that
	// existed only to survive it.
	// …AND `Reverted` IS READ THE SAME WAY. A rollback replaces the metaobjects wholesale, so what
	// the navigator holds afterwards is a tree of freed pointers — the same situation a load leaves
	// it in, and the same answer. It is a stage of its own because what HAPPENED differs; what a
	// tree has to DO about it does not.
	case ibMetaDataNotifier::ibMetaStage::Loaded:
	case ibMetaDataNotifier::ibMetaStage::Run:
	case ibMetaDataNotifier::ibMetaStage::Reverted:
		ClearTree();
		FillData();
		return;

	// GOING, ALL OF IT. The editors opened over this metadata close — once, here, instead of once
	// per object inside every OnAfterCloseMetaObject on the way out. The rows are not touched: the
	// tree is either about to be given another metadata (which refills it) or about to die.
	case ibMetaDataNotifier::ibMetaStage::Closed:
		CloseDocuments();
		return;

	// WRITTEN OUT. Nothing in the tree changed — what changed is that it is no longer modified, and
	// that is what the chrome shows. Asked of the metadata rather than assumed: a save that only
	// dumped a copy to a file leaves the configuration modified, and saying otherwise would be a
	// clean-looking window over unsaved work.
	case ibMetaDataNotifier::ibMetaStage::Saved:
		if (ibMetaData* metaData = GetMetaData())
			Modify(metaData->IsModified());
		return;

	// GOING AWAY — close what its row stands for and everything under it, while the object can
	// still be found, and then take the row away.
	//
	// 🛑 THE ROW USED TO BE LEFT TO "the re-read that follows", AND NOTHING FOLLOWED. EraseItem
	// does not erase: it walks down closing the editors opened over the object and its children,
	// which is all its body has ever done. The row went because a delete ended in Modify() and the
	// whole tree was re-read — and that re-read is gone, deliberately (it was what collapsed
	// Common on every property write). So the erase became a road with no reader: an object
	// deleted over MCP was gone from the configuration and still drawn, and the tree said the
	// configuration held something it did not (Max, 2026-09-01, watching two of them stay).
	case ibMetaDataNotifier::ibMetaStage::Removed:
		if (object != nullptr) {
			const wxTreeItemId item = FindItemByMetaObject(object);
			if (item.IsOk() && m_treeCtrl != nullptr) {
				EraseItem(item);            // what the row stood for closes…
				m_treeCtrl->Delete(item);   // …and the row itself goes, with its subtree
			}
			UpdateChoiceSelection();
		}
		return;

	// ⭐ THE ANSWER TO A RENAME. The name on the object is ALREADY the new one — read it off the
	// object rather than from anything the person typed, because what was typed and what was taken
	// are different facts and only the second one is true. Both the row and the open editor's tab
	// follow from it, and so does any list the name appears in.
	case ibMetaDataNotifier::ibMetaStage::Renamed:
		if (object != nullptr) {
			const wxTreeItemId item = FindItemByMetaObject(object);
			if (item.IsOk())
				SetItemText(item, object->GetName());

			if (ibMetaDocument* document = GetDocument(object)) {
				document->SetTitle(object->GetClassName() + wxT(": ") + object->GetName());
				document->OnChangeFilename(true);
			}

			// ⭐ AND THE PROPERTY PANEL, WHEN IT IS THIS OBJECT ON SCREEN. The inspector is bound to the
			// OBJECT, not to its name, so a rename leaves the old one standing in the Name cell — and
			// SelectObject with the pointer it already holds returns without doing anything, which is
			// exactly what `force` is for (Max, 2026-09-01).
			if (objectInspector->GetSelectedObject() == object)
				objectInspector->SelectObject(object, true);

			UpdateChoiceSelection();
		}
		return;

	// BORN — and it survived every phase that could have undone it, INCLUDING a paste that had still
	// to fill it. This arrives exactly once per object, whichever road built it: a plain create says
	// it at the end of CreateMetaObject, and a paste says it at the end of PasteObject, by which
	// time the properties are read, the children are grown and the name is the one that was free.
	// Nothing in between is announced, which is why the row can simply be drawn from the object.
	// ⭐⭐ DRAWN AS IF THE CLICK HAD DRAWN IT (Max, 2026-09-01: *"only the event draws — but it must
	// draw it the way a click would. You already have the information that this is a new element;
	// from there you simply file it as new"*, and *"this event can come from my click or from
	// yours"*). The row goes under the SELECTED group row — exactly what CreateItem held in its
	// hand before the create moved onto the stage.
	//
	// 🛑 IT USED TO HUNT FOR THE PLACE: the owner's row from the root, then its group, then a
	// fallback, then a check that the row was not already drawn — four walks of the tree on every
	// single add, to work out something the click already knew.
	case ibMetaDataNotifier::ibMetaStage::Created:
		if (object != nullptr && !object->IsDeleted()) {
			AskFormKind(object);

			// ⭐⭐ WHERE THE ROW GOES IS THE OBJECT'S OWN BUSINESS — its OWNER says it.
			//
			// 🛑 IT WENT UNDER THE SELECTION FOR ONE BUILD, and that is only ever right for a click:
			// the person picked the group they wanted. Anything else — a tool, a second window —
			// has no selection to mean anything, and a Dimension created over MCP landed under
			// `Documents` beside a document, because that is what happened to be highlighted (Max,
			// 2026-09-01, watching it).
			//
			// ⚠ AND IT IS NOT A SEARCH. A top-level object is a MAP lookup — the tree's group row
			// for its metatype — and only a nested one costs a walk to its owner's row, plus its
			// owner's own children for the group inside it. That is one walk, for the case that
			// needs one, rather than four for every add.
			ibValueMetaObject* const owner = object->GetParent();

			const wxTreeItemId ownerRow = owner != nullptr
				? FindItemByMetaObject(owner) : wxTreeItemId();

			wxTreeItemId parentRow = Group(object->GetClassType());

			if (ownerRow.IsOk() && ownerRow != m_treeRoot && m_treeCtrl != nullptr) {

				parentRow = ownerRow;   // a kind its owner shows directly, with no group of its own

				wxTreeItemIdValue cookie;
				for (wxTreeItemId child = m_treeCtrl->GetFirstChild(ownerRow, cookie); child.IsOk();
					child = m_treeCtrl->GetNextChild(ownerRow, cookie)) {
					const ibTreeDataClsid* group =
						dynamic_cast<ibTreeDataClsid*>(m_treeCtrl->GetItemData(child));
					if (group != nullptr && group->m_clsid == object->GetClassType()) {
						parentRow = child; break;
					}
				}
			}

			// ⚠ NOT AN ERROR when there is nowhere to put it: a group the search filter dropped, or
			// an owner whose branch has never been drawn, simply has no row yet.
			//
			// 🛑 AND NOT TWICE. Two roads reach one create and both are legitimate: this stage, and
			// the re-entrant pair a FORM sends when its kind is chosen — AskFormKind above writes
			// FormType, and that write announces Removed + Created for the same object before this
			// line is reached. Without the question below a form was drawn twice, and a pasted
			// object's forms came out as `FormObject, FormObject, FormObject1, FormObject2,
			// FormObject2` (Max, 2026-09-01).
			if (parentRow.IsOk() && !FindItemByMetaObject(object).IsOk())
				FillItem(object, parentRow, true, false);

			// The header's default-form / default-composer lists gained a candidate; and every open
			// editor re-gathers, because a form's command navigator lists what the metadata holds.
			UpdateChoiceSelection();
			NotifyDocuments();
		}
		return;

	// SOMETHING IN IT WAS EDITED. A property write used to reach through the session for the main
	// window and repaint it FROM THE BACKEND — the engine deciding that a window exists. It is a
	// watcher's business, and this is the watcher.
	case ibMetaDataNotifier::ibMetaStage::Edited:
		if (ibBackendDocFrame* frame = ibSession::CurrentFrame())
			frame->RefreshFrame();
		return;
	}
}

// ============================================================================
//  ibMetaTreeNotifier — the subscription, and it lives HERE
// ============================================================================
//
// ⭐⭐ THE HEADER KNOWS ONLY THAT THE POINTER EXISTS (`class ibMetaTreeNotifier* m_notifier`), so
// nothing outside this module can name this class, hold one, or hand one in (Max, 2026-09-01:
// *"it is not accessible from outside at all"* / *"it lives in the class's module"*).
//
// Every method is one line to the tree. Nothing is decided here: this exists so that what the
// metadata keeps a list of is a small object with no window in it.

class ibMetaTreeNotifier : public ibMetaDataNotifier {
	ibMetaTreeBase* const m_owner;
public:
	explicit ibMetaTreeNotifier(ibMetaTreeBase* owner) : m_owner(owner) {}

	virtual void EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject) override {
		m_owner->EditPredefinedValues(metaObject);
	}
	virtual void EditHomePage(ibValueMetaObjectConfiguration* metaObject) override {
		m_owner->EditHomePage(metaObject);
	}
	virtual void MetaDataChanged() override {
		m_owner->MetaDataChanged();
	}
	virtual void MetaObjectChanged(ibMetaStage stage, ibValueMetaObject* object) override {
		m_owner->MetaObjectChanged(stage, object);
	}
};

// ⚠ THE OWNER IS SET AT BIRTH AND CANNOT CHANGE — it is `const`, and the notifier is made by the
// only thing that could own one. There is no owner-less state to guard against any more.
ibMetaTreeBase::ibMetaTreeBase()
	: wxPanel(), m_notifier(new ibMetaTreeNotifier(this)) {}

ibMetaTreeBase::ibMetaTreeBase(wxWindow* parent, int id)
	: wxPanel(parent, id), m_notifier(new ibMetaTreeNotifier(this)) {}

ibMetaTreeBase::ibMetaTreeBase(ibMetaDocument* docParent, wxWindow* parent, int id)
	: wxPanel(parent, id), m_notifier(new ibMetaTreeNotifier(this)), m_docParent(docParent) {}

ibMetaTreeBase::~ibMetaTreeBase()
{
	WatchMetaData(nullptr);   // off whatever list it is on, while this tree is still whole
	wxDELETE(m_notifier);
}

// ⭐ THE ONE PLACE A SUBSCRIPTION STARTS, CHANGES AND ENDS. The metadata is the TREE's — it is given
// one, shows it, saves it — so the field being swapped against lives here too, and the notifier
// keeps nothing at all (Max, 2026-09-01: *"take the metadata out of there — the tree has it"*).
void ibMetaTreeBase::WatchMetaData(ibMetaData* metaData)
{
	if (m_watched == metaData)
		return;
	if (m_watched != nullptr)
		m_watched->RemoveNotifier(m_notifier);
	m_watched = metaData;
	if (m_watched != nullptr)
		m_watched->AddNotifier(m_notifier);
}


void ibMetaTreeBase::Activate()
{
	if (m_docParent == nullptr) {
		unsigned int count_doc = 0;
		for (auto doc : docManager->GetDocumentsVector()) count_doc++;
		if (count_doc <= 1) SetFocus();
	}
}

void ibMetaTreeBase::Modify(bool modify)
{
	if (m_docParent != nullptr) {
		m_docParent->Modify(modify);
	}
	else {
		mainFrame->Modify(modify);
	}
}

// ⭐⭐ ONE QUESTION, ASKED OF THE MANAGER. This used to look for the document itself, raise the
// window itself, and fall back to the manager only when its own search found nothing — so "is it
// already open?" was answered TWICE, in two places, by two different tests: the tree compared the
// metaobject POINTER, the manager compares the guid. They disagree exactly where it matters — a
// reloaded configuration, a comparison window, an external processor opened twice — and then one of
// them opens a rival editor over the other's (Max, 2026-09-01: *"before it all went through the
// tree; now let it all go through the manager"*).
bool ibMetaTreeBase::OpenObjectForm(ibValueMetaObject* obj)
{
	return docManager->OpenForm(obj, m_bReadOnly ? ibDOC_READONLY : ibDOC_NEW) != nullptr;
}

// …and so is this. It was a third copy of the same nested walk over the manager's documents and
// their children, keyed on the pointer.
ibMetaDocument* ibMetaTreeBase::GetDocument(ibValueMetaObject* obj) const
{
	return docManager->FindOpenDocument(obj);
}

// CLOSING THE EDITORS IS PART OF *LEAVING A CONFIGURATION*, not of redrawing the tree — and those
// two used to be the same call. Rebuilding the rows is what a search does on every keystroke, so
// typing into the search box closed every editor opened from this navigator, and so did deleting
// the last character (the empty-string search is what restores the full tree).
// One body for all three trees — it was written out three times, word for word, and there was
// nothing in it that belonged to any one of them.
void ibMetaTreeBase::CloseDocuments()
{
	for (auto& doc : docManager->GetDocumentsVector()) {
		// docManager->GetDocumentsVector() now mixes ibMetaDocument
		// instances (Catalog/Document/Form editors) with plain ibDocument
		// (AuditLog, Text, Help) after step-4b decoupling. Skip non-meta
		// docs — they have no metaobject to compare against this tree.
		const ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc == nullptr) continue;
		const ibValueMetaObject* metaObject = metaDoc->GetMetaObject();
		if (metaObject != nullptr && metaObject->GetMetaData() == GetMetaData()) {
			doc->DeleteAllViews();
		}
	}
}

// ⭐⭐ THE DEBUGGER'S THREE — the module's OPEN document, and nothing if it is not open.
//
// The debugger used to ask for the document and call these itself, which made it know that
// documents exist and that exactly one tree keeps them. Now the verb travels and the lookup lives
// where the documents are. Deliberately NOT opening anything: EditModule above is the one that
// opens, because stopping at a breakpoint should show you the code — while taking a run line OFF,
// or answering a tooltip, must not conjure a window nobody asked for.
//
// One helper for all three: whoever is not open has nothing to do.

//***********************************************************************
//*								 metaData                               * 
//***********************************************************************

void ibConfigurationTree::ActivateItem(const wxTreeItemId& item)
{
	ibValueMetaObject* currObject = GetMetaObject(item);
	if (currObject == nullptr)
		return;

	OpenObjectForm(currObject);
}

ibValueMetaObject* ibConfigurationTree::NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject)
{
	return m_metaData->CreateMetaObject(clsid, parent, runObject);
}


// ⭐⭐ A METAOBJECT SAID IT WAS BORN, and for a FORM that is where its kind is chosen.
//
// The engine used to ask for this (SelectFormType) — a dialog in the middle of a create, with the
// create refused if the person closed it. Now the create simply happens and states the fact; the
// watcher that has a person in front of it asks them, and writes the answer in through the ordinary
// property door. A host with nobody to ask writes nothing, and the form is still made.
//
// ⚠ ONLY A FORM THAT HAS NO KIND YET. A paste, a copy and a tool-made form all arrive here too, and
// every one of them already knows what it is — asking again would put a dialog in front of somebody
// who never pressed anything.


// A command node and everything under it. The configuration's navigator overrides this to apply its
// search filter; nothing else has a search to apply, and this was written out twice besides.
wxTreeItemId ibMetaTreeBase::AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command)
{
	if (command == nullptr || command->IsDeleted())
		return wxTreeItemId();

	const wxTreeItemId hCmd = AppendItem(parent, command);

	if (command->GetClassType() == g_metaCommonCommandCLSID || command->GetClassType() == g_metaCommandCLSID)
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(command)->GetSubCommands())
			AppendCommandNode(hCmd, sub);

	return hCmd;
}

// ⭐ THE MODULE, SHOWN. Open its editor and put the caret where the debugger — or a click on an
// error line — is pointing. See the note on the declaration for why the metadata is this tree's.
void ibMetaTreeBase::EditModule(const ibGuid& moduleName, int line, bool setRunLine)
{
	ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return;

	ibValueMetaObject* module = metaData->FindAnyObjectByFilter(moduleName, true);
	if (module == nullptr || module->IsDeleted())
		return;

	OpenObjectForm(module);

	// ⚠ dynamic_cast, NOT wxDynamicCast or static_cast. wx RTTI answers from the base written BY
	// HAND in the wxIMPLEMENT macro, and this chain does not name ibValueModuleDocument:
	// ibModuleDocument declares ibMetaDocument as its base though it really derives from
	// ibValueModuleDocument, so the interface is a SIBLING in the wx graph rather than an ancestor.
	if (ibValueModuleDocument* moduleDoc = dynamic_cast<ibValueModuleDocument*>(GetDocument(module)))
		moduleDoc->SetCurrentLine(line, setRunLine);
}

void ibMetaTreeBase::NotifyDocuments() const
{
	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr) metaDoc->UpdateAllViews();
	}
}

ibValueMetaObject* ibConfigurationTree::CreateItem(bool showValue)
{
	const wxTreeItemId& item = GetSelectionIdentifier();
	if (!item.IsOk()) return nullptr;

	Freeze();

	ibValueMetaObject* createdObject = NewItem(
		GetClassIdentifier(),
		GetMetaIdentifier()
	);

	// ⭐⭐ THE ROW IS NOT DRAWN HERE, and that is the whole concept (Max, 2026-09-01): *"we send our
	// metadata that we changed, and then we just wait for its answer — it says 'I changed it, show
	// that at your end'."*
	//
	// The click ASKS. What comes back is a notification like any other, and the row appears because
	// the metadata answered — the same road a create over MCP takes, and the same road a second
	// designer window watching this configuration takes. Drawing it here as well would be the second
	// road: right for the button, absent for everything else.
	//
	// What stays is what only a CLICK means — open the new object's form.
	if (createdObject != nullptr && showValue)
		OpenObjectForm(createdObject);

	Thaw();

	m_metaTreeCtrl->RefreshSelectedItem();
	return createdObject;
}

// HUB — append a command node and, recursively, its sub-commands (a group command holds commands, shown nested,
// exactly as a subsystem holds subsystems). Skips deleted. The clsid gate makes the cast type-safe.
wxTreeItemId ibConfigurationTree::AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command)
{
	if (command == nullptr || command->IsDeleted())
		return wxTreeItemId();
	const wxTreeItemId hCmd = AppendItem(parent, command);
	if (command->GetClassType() == g_metaCommonCommandCLSID || command->GetClassType() == g_metaCommandCLSID)
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(command)->GetSubCommands())
			AppendCommandNode(hCmd, sub);

	// A command survives a search if IT matched or a command under it did — the recursion above has
	// already answered the second half, because a sub-command that matched nothing removed itself.
	if (!m_strSearch.IsEmpty() && !MatchesSearch(command) && !m_metaTreeCtrl->HasChildren(hCmd)) {
		m_metaTreeCtrl->Delete(hCmd);
		return wxTreeItemId();
	}
	return hCmd;
}

wxTreeItemId ibConfigurationTree::FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select, bool scroll)
{
	m_metaTreeCtrl->Freeze();

	wxTreeItemId createdItem = nullptr;
	if (metaItem->GetClassType() == g_metaTableCLSID || metaItem->GetClassType() == g_metaTableRefCLSID) {
		createdItem = AppendGroupItem(item, g_metaAttributeCLSID, metaItem);
	}
	else if (metaItem->GetClassType() == g_metaSectionCLSID) {
		createdItem = AppendGroupItem(item, g_metaSectionCLSID, metaItem);
	}
	else {
		createdItem = AppendItem(item, metaItem);
	}

	// HOW IT UNFOLDS — the same dispatcher the initial fill uses. It used to be a second chain of
	// `else if` written out here, and the two had drifted: a section created in this path listed
	// its sub-sections as plain rows and did not recurse, while a section LOADED by FillData went
	// through AddInterfaceItem and nested properly.
	ExpandMetaItem(metaItem, createdItem);

	m_metaTreeCtrl->InvalidateBestSize();

	// ⭐⭐ THE VIEW IS MOVED ONCE, AND WHILE THE TREE IS STILL FROZEN. Three things here move the
	// scroll — Expand (to show the new children), SelectItem (wx scrolls a selection into view of
	// its own accord) and ScrollTo — and they used to run in that order with the LAST one after
	// Thaw. So adding an element visibly threw the list about: to the row, then away to fit its
	// children, then back (Max, 2026-09-01: *"the scroll jumps, the item goes there, then here"*).
	//
	// Expanded FIRST, so the shape is final before anything aims at it; selected second, which
	// leaves the row on screen; and the explicit ScrollTo happens before the Thaw, so what the
	// person sees is one position rather than the three it passed through.
	m_metaTreeCtrl->Expand(createdItem);

	// `select` means what it says. It used to wrap SetEvtHandlerEnabled around a SelectItem that ran
	// either way, so a caller asking for no selection got one anyway, silently — only the
	// notification was optional. Nothing asks for that today, and a parameter that does not do what
	// it is named is a trap for whoever asks next.
	if (select)
		m_metaTreeCtrl->SelectItem(createdItem);

	if (scroll)
		m_metaTreeCtrl->ScrollTo(createdItem);

	m_metaTreeCtrl->Thaw();

	return createdItem;
}

void ibConfigurationTree::EditItem()
{
	wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (!selection.IsOk())
		return;
	ibValueMetaObject* currObject = GetMetaObject(selection);
	if (!currObject)
		return;

	OpenObjectForm(currObject);
}

void ibConfigurationTree::RemoveItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();

	if (!selection.IsOk())
		return;

	ibValueMetaObject* metaObject = GetMetaObject(selection);
	// NOT AN ASSERT: wxASSERT is compiled out in Release, and a null here reaches RemoveMetaObject
	// and Delete(selection) — which, on a layout group row, would free a node the group map still
	// points at. A stale wxTreeItemId answers IsOk() == true, so every later guard would pass.
	if (metaObject == nullptr)
		return;

	// ASK, then let the answer come back. Closing the editors happens on the Removed stage — which
	// arrives BEFORE the teardown, where it has to — and the row goes with the re-read that follows.
	// Neither is done here, for the same reason the create no longer draws its own row.
	m_metaData->RemoveMetaObject(metaObject);

	const wxTreeItemId nextSelection = m_metaTreeCtrl->GetFocusedItem();
	if (nextSelection.IsOk()) {
		UpdateToolbar(GetMetaObject(nextSelection), nextSelection);
	}
}

// CLOSE WHAT THIS ROW STANDS FOR, AND EVERYTHING UNDER IT. RemoveMetaObject destroys the whole
// subtree, so an editor open on a form five levels down has to go with it. The caller used to walk
// the DIRECT children instead — and those are group nodes ("Attributes", "Forms", …) that carry a
// class id and no metaobject, so the sweep closed exactly nothing and a form editor survived the
// object it was editing.
void ibConfigurationTree::EraseItem(const wxTreeItemId& item)
{
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie))
		EraseItem(child);

	ibValueMetaObject* const metaObject = GetMetaObject(item);
	if (metaObject == nullptr)
		return;   // a group node — nothing of its own to close

	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr && metaObject == metaDoc->GetMetaObject()) {
			metaDoc->DeleteAllViews();
		}
	}
}

void ibConfigurationTree::SelectItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);

	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	objectInspector->SelectObject(metaObject);
}

void ibConfigurationTree::PropertyItem()
{
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	UpdateToolbar(metaObject, selection);

	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();

	objectInspector->SelectObject(metaObject);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// ⭐⭐ THE ROW THE EVENT IS ABOUT, NOT THE ONE THAT HAPPENS TO BE SELECTED. Both of these took the
// selection and wrote the open/closed flag onto it, while wxTreeEvent had been carrying the actual
// row all along — so clicking the expander of an UNSELECTED row remembered the wrong one, and a
// row opened programmatically wrote onto whatever the person had selected.
//
// 🛑 AND WITH NOTHING SELECTED IT WAS AN ASSERT, which is how it was found (2026-09-01, dump
// designer_27068): a re-read reopened rows by calling Expand, and wx delivers EVT_TREE_ITEM_EXPANDING
// SYNCHRONOUSLY from inside that call — at a moment when the tree had been cleared and refilled but
// nothing was selected yet, so GetSelection() answered an invalid id and GetItemData broke on it.
void ibConfigurationTree::Collapse(const wxTreeItemId& item)
{
	if (!item.IsOk())
		return;

	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(item));
	if (data != nullptr)
		data->m_expanded = false;
}

void ibConfigurationTree::Expand(const wxTreeItemId& item)
{
	if (!item.IsOk())
		return;

	ibTreeData* data =
		dynamic_cast<ibTreeData*>(m_metaTreeCtrl->GetItemData(item));
	if (data != nullptr)
		data->m_expanded = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibConfigurationTree::UpItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	m_metaTreeCtrl->Freeze();

	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	const wxTreeItemId& nextItem = m_metaTreeCtrl->GetPrevSibling(selection);
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != nullptr && nextItem.IsOk()) {
		const wxTreeItemId& parentItem = m_metaTreeCtrl->GetItemParent(nextItem);
		wxTreeItemIdValue coockie; wxTreeItemId nextId = m_metaTreeCtrl->GetFirstChild(parentItem, coockie);
		size_t pos = 0;
		do {
			if (nextId == nextItem)
				break;
			nextId = m_metaTreeCtrl->GetNextChild(parentItem, coockie); pos++;
		} while (nextId.IsOk());
		ibValueMetaObject* parentObject = metaObject->GetParent();
		ibValueMetaObject* nextObject = GetMetaObject(nextItem);
		if (parentObject->ChangeChildPosition(metaObject, parentObject->GetChildPosition(nextObject))) {
			wxTreeItemId newId = m_metaTreeCtrl->InsertItem(parentItem,
				pos + 2,
				m_metaTreeCtrl->GetItemText(nextItem),
				m_metaTreeCtrl->GetItemImage(nextItem),
				m_metaTreeCtrl->GetItemImage(nextItem),
				m_metaTreeCtrl->GetItemData(nextItem)
			);

			auto tree = m_metaTreeCtrl;
			std::function<void(ibMetaTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibMetaTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
				wxTreeItemIdValue coockie; wxTreeItemId nextId = tree->GetFirstChild(dst, coockie);
				while (nextId.IsOk()) {
					wxTreeItemId newId = tree->AppendItem(src,
						tree->GetItemText(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemData(nextId)
					);
					if (tree->HasChildren(nextId)) {
						swap(tree, nextId, newId);
					}
					tree->SetItemData(nextId, nullptr);
					nextId = tree->GetNextChild(dst, coockie);
				}
				};

			swap(tree, nextItem, newId);

			m_metaTreeCtrl->SetItemData(nextItem, nullptr);
			m_metaTreeCtrl->Delete(nextItem);

			//m_metaTreeCtrl->Expand(newId);
		}
	}

	m_metaTreeCtrl->Thaw();
}

void ibConfigurationTree::DownItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;

	m_metaTreeCtrl->Freeze();

	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	const wxTreeItemId& prevItem = m_metaTreeCtrl->GetNextSibling(selection);
	ibValueMetaObject* metaObject = GetMetaObject(selection);
	if (metaObject != nullptr && prevItem.IsOk()) {
		const wxTreeItemId& parentItem = m_metaTreeCtrl->GetItemParent(prevItem);
		wxTreeItemIdValue coockie; wxTreeItemId nextId = m_metaTreeCtrl->GetFirstChild(parentItem, coockie);
		size_t pos = 0;
		do {
			if (nextId == prevItem)
				break;
			nextId = m_metaTreeCtrl->GetNextChild(parentItem, coockie); pos++;
		} while (nextId.IsOk());
		ibValueMetaObject* parentObject = metaObject->GetParent();
		ibValueMetaObject* prevObject = GetMetaObject(prevItem);
		if (parentObject->ChangeChildPosition(metaObject, parentObject->GetChildPosition(prevObject))) {
			wxTreeItemId newId = m_metaTreeCtrl->InsertItem(parentItem,
				pos - 1,
				m_metaTreeCtrl->GetItemText(prevItem),
				m_metaTreeCtrl->GetItemImage(prevItem),
				m_metaTreeCtrl->GetItemImage(prevItem),
				m_metaTreeCtrl->GetItemData(prevItem)
			);

			auto tree = m_metaTreeCtrl;
			std::function<void(ibMetaTreeCtrl*, const wxTreeItemId&, const wxTreeItemId&)> swap = [&swap](ibMetaTreeCtrl* tree, const wxTreeItemId& dst, const wxTreeItemId& src) {
				wxTreeItemIdValue coockie; wxTreeItemId nextId = tree->GetFirstChild(dst, coockie);
				while (nextId.IsOk()) {
					wxTreeItemId newId = tree->AppendItem(src,
						tree->GetItemText(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemImage(nextId),
						tree->GetItemData(nextId)
					);
					if (tree->HasChildren(nextId)) {
						swap(tree, nextId, newId);
					}
					tree->SetItemData(nextId, nullptr);
					nextId = tree->GetNextChild(dst, coockie);
				}
				};

			swap(tree, prevItem, newId);

			m_metaTreeCtrl->SetItemData(prevItem, nullptr);
			m_metaTreeCtrl->Delete(prevItem);

			//m_metaTreeCtrl->Expand(newId);
		}
	}

	m_metaTreeCtrl->Thaw();
}

void ibConfigurationTree::SortItem()
{
	if (appData->GetAppMode() != ibRunMode::eDESIGNER_MODE)
		return;
	m_metaTreeCtrl->Freeze();
	const wxTreeItemId& selection = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* prevObject = GetMetaObject(selection);
	if (prevObject != nullptr && selection.IsOk()) {
		const wxTreeItemId& parentItem =
			m_metaTreeCtrl->GetItemParent(selection);
		if (parentItem.IsOk()) {
			m_metaTreeCtrl->SortChildren(parentItem);
		}
	}
	m_metaTreeCtrl->Thaw();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/metadataDataProcessor.h"
#include "backend/metadataReport.h"

void ibConfigurationTree::InsertItem()
{
	ibValueMetaObject* commonMetaObject = m_metaData->GetCommonMetaObject(); wxTreeItemId hSelItem = m_metaTreeCtrl->GetSelection();

	// ⚠ ASK WHETHER THE GROUP EXISTS, not just whether the ids match: two INVALID wxTreeItemIds
	// compare equal, and a group is legitimately absent while a search filter is on. The field
	// used to be unconditionally valid, so this test is new work rather than a port.
	if (Group(g_metaDataProcessorCLSID).IsOk() && hSelItem == Group(g_metaDataProcessorCLSID)) {

		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			ibFileFilter(ibFileKind::Tool), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		//create main metaObject
		ibMetaDataDataProcessor metadataDataProcessor(m_metaData);

		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectDataProcessor* dataProcessor = metadataDataProcessor.GetDataProcessor();
			wxASSERT(dataProcessor);
			const wxTreeItemId& createdItem = AppendItem(hSelItem, dataProcessor);
			AddDataProcessorItem(dataProcessor, createdItem);
			m_metaTreeCtrl->SelectItem(createdItem);
			dataProcessor->IncrRef();
			m_metaTreeCtrl->Thaw();
		}
	}
	else {
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			ibFileFilter(ibFileKind::Report), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibMetaDataReport metadataReport(m_metaData);

		if (metadataReport.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectReport* report = metadataReport.GetReport();
			wxASSERT(report);
			const wxTreeItemId& createdItem = AppendItem(hSelItem, report);
			AddReportItem(report, createdItem);
			m_metaTreeCtrl->SelectItem(createdItem);
			report->IncrRef();
			m_metaTreeCtrl->Thaw();
		}
	}

	m_metaData->Modify(true);
}

void ibConfigurationTree::ReplaceItem()
{
	wxTreeItemId hSelItem = m_metaTreeCtrl->GetSelection();
	ibValueMetaObject* currentMetaObject = GetMetaObject(m_metaTreeCtrl->GetSelection());

	if (currentMetaObject->GetClassType() == g_metaDataProcessorCLSID) {

		wxFileDialog openFileDialog(this, _("Open data processor file"), "", "",
			ibFileFilter(ibFileKind::Tool), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibMetaDataDataProcessor metadataDataProcessor(m_metaData);
		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectDataProcessor* metaObject = metadataDataProcessor.GetDataProcessor();
			wxTreeItemData* itemData = m_metaTreeCtrl->GetItemData(hSelItem);
			if (itemData != nullptr) {
				ibTreeDataObject* metaItem = dynamic_cast<ibTreeDataObject*>(itemData);
				if (metaItem != nullptr)
					metaItem->m_metaObject = metaObject;
			}
			// ⚠ THE REPOINT ABOVE HAPPENS FIRST, AND NOW HAS TO. RemoveMetaObject announces the
			// removal, and a listener that still found this row pointing at the old object would
			// delete the row this path is about to reuse.
			m_metaData->RemoveMetaObject(currentMetaObject);
			m_metaTreeCtrl->SetItemText(hSelItem, metaObject->GetName());
			m_metaTreeCtrl->DeleteChildren(hSelItem);
			AddDataProcessorItem(metaObject, hSelItem);
			m_metaTreeCtrl->Thaw();
		}
	}
	else
	{
		wxFileDialog openFileDialog(this, _("Open report file"), "", "",
			ibFileFilter(ibFileKind::Report), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibValueMetaObjectReport* newReport = dynamic_cast<ibValueMetaObjectReport*>(
			currentMetaObject
			);

		wxASSERT(newReport);

		ibMetaDataReport metadataDataProcessor(m_metaData);
		if (metadataDataProcessor.LoadFromFile(openFileDialog.GetPath())) {
			m_metaTreeCtrl->Freeze();
			ibValueMetaObjectReport* metaObject = metadataDataProcessor.GetReport();
			wxTreeItemData* itemData = m_metaTreeCtrl->GetItemData(hSelItem);
			if (itemData != nullptr) {
				ibTreeDataObject* metaItem = dynamic_cast<ibTreeDataObject*>(itemData);
				if (metaItem != nullptr)
					metaItem->m_metaObject = metaObject;
			}
			// ⚠ THE REPOINT ABOVE HAPPENS FIRST, AND NOW HAS TO. RemoveMetaObject announces the
			// removal, and a listener that still found this row pointing at the old object would
			// delete the row this path is about to reuse.
			m_metaData->RemoveMetaObject(currentMetaObject);

			// …AND THE ROW IS DRESSED FROM THE NEW OBJECT. It read `newReport` — which is the OLD
			// one, cast — so replacing a report from a file relabelled the row with the name it
			// already had and unfolded the report that had just been removed.
			m_metaTreeCtrl->SetItemText(hSelItem, metaObject->GetName());
			m_metaTreeCtrl->DeleteChildren(hSelItem);
			AddReportItem(metaObject, hSelItem);
			m_metaTreeCtrl->Thaw();
		}
	}

	m_metaData->Modify(true);
}

void ibConfigurationTree::SaveItem()
{
	ibValueMetaObject* currentMetaObject = GetMetaObject(m_metaTreeCtrl->GetSelection());

	if (currentMetaObject->GetClassType() == g_metaDataProcessorCLSID) {

		wxFileDialog saveFileDialog(this, _("Open data processor file"), "", "",
			ibFileFilter(ibFileKind::Tool), wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeCtrl->GetItemText(m_metaTreeCtrl->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibValueMetaObjectDataProcessor* newDataProcessor = dynamic_cast<ibValueMetaObjectDataProcessor*>(
			currentMetaObject
			);
		wxASSERT(newDataProcessor);
		ibMetaDataDataProcessor metadataDataProcessor(m_metaData, newDataProcessor);
		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
	else {
		wxFileDialog saveFileDialog(this, _("Open report file"), "", "",
			ibFileFilter(ibFileKind::Report), wxFD_SAVE);

		saveFileDialog.SetFilename(m_metaTreeCtrl->GetItemText(m_metaTreeCtrl->GetSelection()));

		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		ibValueMetaObjectReport* newDataProcessor = dynamic_cast<ibValueMetaObjectReport*>(
			currentMetaObject
			);
		wxASSERT(newDataProcessor);
		ibMetaDataReport metadataDataProcessor(m_metaData, newDataProcessor);
		metadataDataProcessor.SaveToFile(saveFileDialog.GetPath());
	}
}


void ibConfigurationTree::PrepareReplaceMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_REPLACE, _("Replace data processor, report..."));
	menuItem->Enable(!m_bReadOnly);
	menuItem = defaultMenu->Append(ID_METATREE_SAVE, _("Save data processor, report..."));
	ibAppendSeparator(defaultMenu);
}

#include "frontend/artProvider/artProvider.h"

// ⭐ THE TWO TEXTS, AS ONE SECTION, WHICHEVER MENU IT LANDS IN. Help is what the person USING the
// application reads on F1; the technical text is how the thing works inside, and never leaves the
// designer. Two items rather than one editor with a switch, because choosing between them is
// choosing an AUDIENCE — and a switch inside would let the wrong one be written into the other
// without noticing.
//
// 🛑 IT USED TO BE WRITTEN INLINE IN ONE BRANCH, so a metaobject that builds its OWN context menu
// (the configuration root, with its modules) had no texts at all — the same state reached by two
// roads, carried by only one of them. It is a function now, called from both.
//
// Placed ABOVE Properties rather than at the end: the texts are about the OBJECT, properties are
// its settings, and the order reads the same way in both menus because the position is found
// rather than counted.
void ibConfigurationTree::AppendTextsMenu(wxMenu* defaultMenu)
{
	const bool readOnly = m_bReadOnly;

	size_t position = defaultMenu->GetMenuItemCount();

	size_t index = 0;
	for (const wxMenuItem* menuItem : defaultMenu->GetMenuItems()) {
		if (menuItem->GetId() == ID_METATREE_PROPERTY) {
			position = index;
			break;
		}
		++index;
	}

	wxMenuItem* help = nullptr;
	wxMenuItem* notes = nullptr;

	if (position < defaultMenu->GetMenuItemCount()) {
		help = defaultMenu->Insert(position, ID_METATREE_HELP, _("Help information..."));
		notes = defaultMenu->Insert(position + 1, ID_METATREE_NOTES, _("Technical information..."));
		defaultMenu->InsertSeparator(position + 2);
	}
	else {
		// No Properties to sit above — a menu that ends here, so the separator leads instead.
		ibAppendSeparator(defaultMenu);
		help = defaultMenu->Append(ID_METATREE_HELP, _("Help information..."));
		notes = defaultMenu->Append(ID_METATREE_NOTES, _("Technical information..."));
	}

	// ⚠ BOTH ARE EDITORS, so they are shut in a read-only configuration for the same reason New and
	// Remove are. Reading is not lost: the help text is what F1 shows, and the technical text is
	// what notes_read answers.
	help->Enable(!readOnly);
	notes->Enable(!readOnly);
}

void ibConfigurationTree::PrepareContextMenu(wxMenu* defaultMenu, const wxTreeItemId& item)
{
	ibValueMetaObject* metaObject = GetMetaObject(item);

	// ⭐ ASKED ONCE. The metaobject offers what it opens, this draws it, and the answer says
	// whether the standard New / Edit / Remove block applies — held rather than asked twice.
	const bool ownMenu = AppendMetaMenu(defaultMenu, metaObject);

	if (metaObject && !ownMenu)
	{
		if (g_metaDataProcessorCLSID == metaObject->GetClassType()
			|| g_metaReportCLSID == metaObject->GetClassType()) {
			PrepareReplaceMenu(defaultMenu);
		}

		wxMenuItem* menuItem = nullptr;

		menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picAddCLSID));
		menuItem->Enable(!m_bReadOnly);
		menuItem = defaultMenu->Append(ID_METATREE_EDIT, _("Edit"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picEditCLSID));
		menuItem = defaultMenu->Append(ID_METATREE_DELETE, _("Remove"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picDeleteCLSID));
		menuItem->Enable(!m_bReadOnly);
	}

	// ⭐ PROPERTIES ALWAYS APPLIES, and so do the two texts — EVERY metaobject has them, the
	// configuration root included. They sat inside the block above, which is suppressed when
	// CollectContextMenu answers true, so the root and a common-attribute copy lost their
	// Properties along with a New and a Remove that genuinely do not apply to them.
	//
	// 🛑 THE FLAG SAYS ONE THING AND WAS READ AS ANOTHER: *"this row cannot be created or
	// deleted"*, not *"this row has nothing to show"*. Bundling them made the narrower fact
	// silently carry the wider one.
	if (metaObject != nullptr) {
		ibAppendSeparator(defaultMenu);
		wxMenuItem* propertyItem = defaultMenu->Append(ID_METATREE_PROPERTY, _("Properties"));
		propertyItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));

		AppendTextsMenu(defaultMenu);
	}
	else if (!metaObject && item != m_treeCOMMON) {
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(ibBackendPicture::GetPicture(g_picAddCLSID));
		menuItem->Enable(!m_bReadOnly);

		// Same rule as InsertItem: an absent group must not match an invalid selection.
		if ((Group(g_metaDataProcessorCLSID).IsOk() && item == Group(g_metaDataProcessorCLSID))
			|| (Group(g_metaReportCLSID).IsOk() && item == Group(g_metaReportCLSID))) {
			ibAppendSeparator(defaultMenu);
			wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_INSERT, _("Insert data processor, report..."));
			menuItem->Enable(!m_bReadOnly);
		}
	}
	else if (item == m_treeRoot) {
		ibAppendSeparator(defaultMenu);
		wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_PROPERTY, _("Properties"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));
	}
}

void ibConfigurationTree::ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos)
{
	wxMenu innerMenu;   // stack — PopupMenu does not take ownership, and it blocks until dismissed
	PrepareContextMenu(&innerMenu, item);

	// ⭐ AND THAT IS THE WHOLE OF IT. The menu's own items carry their actions (AppendMetaMenu binds
	// each one), so there is nothing to route and nothing to unroute.
	//
	// 🛑 THERE WAS A SECOND LAYER HERE: walk the finished menu, skip the nine standard ids by name,
	// Bind the rest to OnCommandItem, pop the menu up, then Unbind every id again — and OnCommandItem
	// forwarded to CommandItem, which forwarded to RunOpenItem, which turned the id back into an
	// index into a vector kept as a member since the menu was built. Four hops and three pieces of
	// state to carry a click across a popup that lasts one call.
	m_metaTreeCtrl->PopupMenu(&innerMenu, m_metaTreeCtrl->ScreenToClient(eventSrc->ClientToScreen(pos)));
}

void ibConfigurationTree::UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item)
{
	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly && item != m_treeCOMMON);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem());
	m_metaTreeToolbar->EnableTool(ID_METATREE_DELETE, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, obj != nullptr && item != m_metaTreeCtrl->GetRootItem() && !m_bReadOnly);

	m_metaTreeToolbar->Refresh();
}

// ASK ONLY. The row is relabelled when the answer comes back, on the Renamed stage below — the
// person typed a new name, the metadata decides whether it may be taken, and what the tree shows
// follows from what the metadata says rather than from what was typed.
bool ibConfigurationTree::RenameMetaObject(ibValueMetaObject* metaObject, const wxString& newName)
{
	return m_metaData->RenameMetaObject(metaObject, newName);
}

#include "backend/metaCollection/partial/commonObject.h"

void ibConfigurationTree::AddInterfaceItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectSection* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectSection>();
	wxASSERT(metaObject);

	// SECTIONS NEST, so a section survives a search if IT matched or something inside it did —
	// the recursion below answers the second half, exactly as it does for commands.
	for (auto commonInterface : metaObjectValue->GetInterfaceArrayObject()) {

		if (commonInterface->IsDeleted())
			continue;

		const wxTreeItemId hSection = AppendGroupItem(hParentID, g_metaSectionCLSID, commonInterface);
		AddInterfaceItem(commonInterface, hSection);

		if (!m_strSearch.IsEmpty() && !MatchesSearch(commonInterface)
			&& !m_metaTreeCtrl->HasChildren(hSection))
			m_metaTreeCtrl->Delete(hSection);
	}

	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())   // a section owns its own commands
		AppendCommandNode(hParentID, metaCommand);
}

// A REFERENCE OBJECT — attributes, tabular sections, forms, commands, templates. Catalogs and
// documents render identically, and so do the two charts and a parameterized job (they reach here
// through ExpandMetaItem, which is where "renders AS a catalog" is written down).
void ibConfigurationTree::AddCatalogItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordDataRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataRef>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	// A catalog / document is ALWAYS a reference, so its table is the DB-backed MD_TBLR;
	// processors / reports are RAM, so MD_TBL. Set explicitly per object kind.
	AppendTableGroup(hParentID, g_metaTableRefCLSID, objectTablesName,
		metaObjectValue->GetTableArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A DOCUMENT renders exactly as a catalog does — the same five groups in the same order. It keeps
// an entry point of its own only because the dispatcher names KINDS, not shapes.
void ibConfigurationTree::AddDocumentItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	AddCatalogItem(metaObject, hParentID);
}

// AN ENUMERATION has values where the others have attributes and tabular sections.
void ibConfigurationTree::AddEnumerationItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordDataEnumRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataEnumRef>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaEnumCLSID, objectEnumerationsName,
		metaObjectValue->GetEnumObjectArray());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A DATA PROCESSOR — the same five groups as a catalog, except that its tabular sections live in
// RAM (MD_TBL) rather than in the database (MD_TBLR). That one clsid is the whole difference.
void ibConfigurationTree::AddDataProcessorItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRecordData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordData>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	AppendTableGroup(hParentID, g_metaTableCLSID, objectTablesName,
		metaObjectValue->GetTableArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A REPORT is a data processor plus the thing that makes it a report: its COMPOSERS. They are its
// own children, like its forms — the default one is what the generated form is built from, so a
// report that declares one needs no form at all (docs/report-engine.md §4b).
void ibConfigurationTree::AddReportItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	AddDataProcessorItem(metaObject, hParentID);   // same shape, down to the RAM tabular sections

	ibValueMetaObjectReport* metaReport = metaObject->ConvertToType<ibValueMetaObjectReport>();
	wxASSERT(metaReport);

	AppendObjectGroup(hParentID, g_metaComposerCLSID, objectComposersName,
		metaReport->GetComposerArrayObject());
}

// A REGISTER — dimensions and resources where a reference object has tabular sections.
void ibConfigurationTree::AddInformationRegisterItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectRegisterData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRegisterData>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaDimensionCLSID, objectDimensionsName,
		metaObjectValue->GetDimensionArrayObject());
	AppendObjectGroup(hParentID, g_metaResourceCLSID, objectResourcesName,
		metaObjectValue->GetResourceArrayObject());
	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

void ibConfigurationTree::AddAccumulationRegisterItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	AddInformationRegisterItem(metaObject, hParentID);   // same shape — an accounting register too
}

#include "frontend/artProvider/artProvider.h"

////////////////////////////////////////////////////////////////////////////
// THE LAYOUT — one table, read top to bottom
////////////////////////////////////////////////////////////////////////////
//
// Every group node in the navigator is a row here, and the row is the whole truth about it: the
// METATYPE it stands for (which already gives it its icon, its "New" and its context menu), the
// words on it, which band it lives in, and how a member of it is put in. THE ORDER OF THE TABLE
// IS THE ORDER ON SCREEN — there is no second place that says it, where before there were three
// (the sequence of AppendGroupItem calls, the sequence of fill loops, and a list of DeleteChildren
// that had silently fallen three entries behind).
//
// This is deliberately a table and NOT YET a walk over the type registry. What a metatype would
// have to answer for a navigator to draw it without this table is exactly the columns below —
// writing them out in one place is what makes that question askable. Once a metatype answers it
// itself (a band + a rank + a label, beside the GetIconGroup it already has), this table goes away
// and the tree becomes a walk over whatever registered — which is what would let a metatype added
// later appear here with nothing edited in this file. Same shape as backend_picture.cpp, which
// already walks ibValue::GetListCtorsByType(object_metadata) for exactly one such answer.
//
// ⚠ ORDER MATTERS TWICE: a row that nests inside another group (m_owner) must come AFTER it — the
// parent node has to exist to hang from.

namespace {

enum class ibMetaBand { Common, Metadata };   // the two bands of the navigator

// HOW A MEMBER OF THE GROUP IS PUT IN. Three shapes and no more: an ordinary row, a row that is
// itself a group (a section holds sections), and a command (which nests its sub-commands).
enum class ibMetaRow { Item, Group, Command };

struct ibMetaTreeGroupDef {
	ibClassID   m_clsid;
	// The SOURCE string, marked for extraction and left untranslated here: this table is static
	// data, built before a locale is loaded (same rule as backend/fileKind.cpp).
	const char* m_label;
	ibMetaBand  m_band;
	ibClassID   m_owner;   // 0 = straight in the band; otherwise the group it nests under
	ibMetaRow   m_row;
};

const ibMetaTreeGroupDef s_groups[] = {
	// ——— Common: what belongs to the configuration as a whole and to no business object ———
	{ g_metaCommonModuleCLSID,    wxTRANSLATE("Common modules"),   ibMetaBand::Common, 0, ibMetaRow::Item    },
	{ g_metaCommonFormCLSID,      wxTRANSLATE("Common forms"),     ibMetaBand::Common, 0, ibMetaRow::Item    },
	{ g_metaCommonCommandCLSID,   wxTRANSLATE("Common commands"),  ibMetaBand::Common, 0, ibMetaRow::Command },
	{ g_metaCommonTemplateCLSID,  wxTRANSLATE("Common templates"), ibMetaBand::Common, 0, ibMetaRow::Item    },

	// SCHEDULED JOBS stay under COMMON — unattended work belongs to the configuration as a whole.
	// One branch, two kinds inside it: the branch itself holds the PARAMETERIZED jobs (it is their
	// metatype's group node, so File → New reaches them the usual way), and the PREDEFINED ones
	// live in a sub-branch declared FIRST, which is what puts them above — a configuration declares
	// a handful of those and they never multiply with the data, while the parameterized list grows.
	{ g_metaParameterizedJobCLSID, wxTRANSLATE("Scheduled jobs"),  ibMetaBand::Common, 0, ibMetaRow::Item },
	{ g_metaScheduledJobCLSID,     wxTRANSLATE("Predefined jobs"), ibMetaBand::Common,
	  g_metaParameterizedJobCLSID, ibMetaRow::Item },

	// SESSION PARAMETERS sit beside the jobs for the same reason: each is an ATTRIBUTE whose owner
	// is the session — declared here, set once by the session module, read everywhere.
	{ g_metaSessionParameterCLSID, wxTRANSLATE("Session parameters"), ibMetaBand::Common, 0, ibMetaRow::Item },
	// COMMON ATTRIBUTES — declared here, carried by many objects. What the declaration puts INTO
	// each object is a child of THAT object and appears there, in its own attribute list.
	{ g_metaCommonAttributeCLSID,  wxTRANSLATE("Common attributes"),  ibMetaBand::Common, 0, ibMetaRow::Item },
	{ g_metaPictureCLSID,          wxTRANSLATE("Pictures"),           ibMetaBand::Common, 0, ibMetaRow::Item },
	// Sections come AFTER the common items — a top-level navigation grouping, not a common asset.
	{ g_metaSectionCLSID,          wxTRANSLATE("Sections"),           ibMetaBand::Common, 0, ibMetaRow::Group },
	{ g_metaRoleCLSID,             wxTRANSLATE("Roles"),              ibMetaBand::Common, 0, ibMetaRow::Item },
	{ g_metaLanguageCLSID,         wxTRANSLATE("Languages"),          ibMetaBand::Common, 0, ibMetaRow::Item },

	// ——— Metadata: the business objects a configuration is made of ———
	{ g_metaConstantCLSID,                   wxTRANSLATE("Constants"),        ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaCatalogCLSID,                    wxTRANSLATE("Catalogs"),         ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaDocumentCLSID,                   wxTRANSLATE("Documents"),        ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaEnumerationCLSID,                wxTRANSLATE("Enumerations"),     ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaDataProcessorCLSID,              wxTRANSLATE("Data processors"),  ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaReportCLSID,                     wxTRANSLATE("Reports"),          ibMetaBand::Metadata, 0, ibMetaRow::Item },
	// ⭐ THE CHARTS FIRST, THE REGISTERS LAST — the reading order of the tree.
	//
	// A register is expressed in terms of what stands above it: an accumulation register by its
	// dimensions, an accounting register by the chart of accounts that types its account and every
	// analytics slot. With the registers listed before the charts, the tree presented the dependants
	// before the things they depend on. (The compare tree carries the same order as ranks —
	// metaDiff.cpp; the two lists are separate copies of one decision, § metadata-tree.md.)
	{ g_metaChartOfCharacteristicTypesCLSID, wxTRANSLATE("Charts of characteristic types"), ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaChartOfAccountsCLSID,            wxTRANSLATE("Charts of accounts"),      ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaInformationRegisterCLSID,        wxTRANSLATE("Information Registers"),  ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaAccumulationRegisterCLSID,       wxTRANSLATE("Accumulation Registers"), ibMetaBand::Metadata, 0, ibMetaRow::Item },
	{ g_metaAccountingRegisterCLSID,         wxTRANSLATE("Accounting registers"),    ibMetaBand::Metadata, 0, ibMetaRow::Item },
};

} // namespace

// HOW A METAOBJECT UNFOLDS — the ONE dispatcher. The initial fill and the create path both come
// through here, so a kind cannot unfold one way when it is loaded and another way when it is made
// (they were two separate chains of `else if` before, and they had already drifted apart).
//
// The reuse is meaningful and is the only real knowledge in it: a chart of characteristic types
// and a chart of accounts render AS a catalog, an accounting register AS an accumulation register,
// a parameterized job AS a catalog entry with a second verb.
void ibConfigurationTree::ExpandMetaItem(ibValueMetaObject* metaItem, const wxTreeItemId& item)
{
	const ibClassID clsid = metaItem->GetClassType();

	if      (clsid == g_metaCatalogCLSID)                    AddCatalogItem(metaItem, item);
	else if (clsid == g_metaDocumentCLSID)                   AddDocumentItem(metaItem, item);
	else if (clsid == g_metaEnumerationCLSID)                AddEnumerationItem(metaItem, item);
	else if (clsid == g_metaDataProcessorCLSID)              AddDataProcessorItem(metaItem, item);
	else if (clsid == g_metaReportCLSID)                     AddReportItem(metaItem, item);
	else if (clsid == g_metaInformationRegisterCLSID)        AddInformationRegisterItem(metaItem, item);
	else if (clsid == g_metaAccumulationRegisterCLSID)       AddAccumulationRegisterItem(metaItem, item);
	else if (clsid == g_metaParameterizedJobCLSID)           AddCatalogItem(metaItem, item);
	else if (clsid == g_metaChartOfCharacteristicTypesCLSID) AddCatalogItem(metaItem, item);
	else if (clsid == g_metaChartOfAccountsCLSID)            AddCatalogItem(metaItem, item);
	else if (clsid == g_metaAccountingRegisterCLSID)         AddAccumulationRegisterItem(metaItem, item);
	else if (clsid == g_metaSectionCLSID)                    AddInterfaceItem(metaItem, item);

	// A COMMAND HOLDS COMMANDS. The fill path always knew this (it goes through AppendCommandNode);
	// the create/paste path did not, so pasting a command that owns sub-commands drew a leaf — the
	// children were restored in the metadata and stayed invisible until the configuration reopened.
	else if (clsid == g_metaCommandCLSID || clsid == g_metaCommonCommandCLSID) {
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(metaItem)->GetSubCommands())
			AppendCommandNode(item, sub);
	}

	// A TABULAR SECTION shows its own columns. It reaches this dispatcher from the create path
	// only — a table is never a top-level group — but it belongs here rather than beside the call,
	// so "how does a kind unfold" has one answer wherever it is asked.
	else if (clsid == g_metaTableCLSID || clsid == g_metaTableRefCLSID) {
		ibValueMetaObjectTableData* metaTable = metaItem->ConvertToType<ibValueMetaObjectTableData>();
		wxASSERT(metaTable);
		for (auto attribute : metaTable->GetAttributeArrayObject()) {
			if (!attribute->IsAcceptedByParent())
				continue;
			AppendItem(item, attribute);
		}
	}
	// Anything else is a leaf row — a module, a form, a picture, a role, a language.
}

// DOES THIS OBJECT ANSWER THE SEARCH BOX. One predicate, asked in one place, so a filtered tree
// cannot disagree with itself the way it did when the test was written out at each loop.
//
// CASE-INSENSITIVE, and by NAME OR SYNONYM: `Find` is case-sensitive, so typing what is on screen
// in the wrong case found nothing — and the words a person reads in this tree are the name, while
// the words they remember are often the synonym.
bool ibConfigurationTree::MatchesSearch(const ibValueMetaObject* metaObject) const
{
	if (m_strSearch.IsEmpty())
		return true;
	const wxString needle = m_strSearch.Lower();
	return metaObject->GetName().Lower().Find(needle) != wxNOT_FOUND
		|| metaObject->GetSynonym().Lower().Find(needle) != wxNOT_FOUND;
}

void ibConfigurationTree::InitTree()
{
	wxImageList* imageList = m_metaTreeCtrl->GetImageList();
	wxASSERT(imageList);

	m_treeRoot = AppendRootItem(g_metaCommonMetadataCLSID, _("Configuration"));

	const int imageCommonIndex = imageList->Add(wxArtProvider::GetBitmapBundle(wxART_COMMON_FOLDER, wxART_METATREE).GetBitmap(wxDefaultSize));
	m_treeCOMMON = m_metaTreeCtrl->AppendItem(m_treeRoot, commonName, imageCommonIndex, imageCommonIndex);

	m_groups.clear();
	for (const ibMetaTreeGroupDef& def : s_groups) {
		const wxTreeItemId parent = def.m_owner != 0
			? Group(def.m_owner)                                                   // nested (predefined jobs)
			: (def.m_band == ibMetaBand::Common ? m_treeCOMMON : m_treeRoot);
		wxASSERT(parent.IsOk());   // a nested row placed before its owner — see the table's ⚠
		m_groups[def.m_clsid] = AppendGroupItem(parent, def.m_clsid,
			wxGetTranslation(wxString::FromUTF8(def.m_label)));
	}



	//Set item bold and name
	m_metaTreeCtrl->SetItemText(m_treeRoot, _("Configuration"));
	m_metaTreeCtrl->SetItemBold(m_treeRoot);
}

void ibConfigurationTree::ActivateTree()
{
	if (m_metaData != nullptr)
		objectInspector->SelectObject(GetMetaObject(m_metaTreeCtrl->GetSelection()));
}


void ibConfigurationTree::ClearTree()
{
	// disable events for the whole rebuild - RAII, so a throw from InitTree cannot leave them off
	const ibEventsOff eventsOff(m_metaTreeCtrl);

	// THE CLEAR IS TOTAL, and it always was. A per-group DeleteChildren pass used to stand here,
	// written as a list of nineteen branches — but it ran immediately before DeleteAllItems, so
	// nothing it did could survive, and the list had fallen three entries behind (session
	// parameters, common attributes, languages) without any way for that to show. The intent it
	// carried — "clear the contents on demand" — is what these two lines do, for every group
	// including the ones nobody remembered to add.
	m_groups.clear();

	// ROWS FIRST, THEN THE LIST THEY INDEX INTO — the twins already do it in this order. A row
	// holds an INDEX into the image list, so dropping the images while the rows still reference
	// them leaves every surviving row pointing past the end for as long as the delete pass runs.
	m_metaTreeCtrl->DeleteAllItems();

	// THE IMAGE LIST IS PART OF THE TREE, so it is cleared with it. Every Append* adds a bitmap and
	// nothing ever removed one, so each rebuild — and a search is a rebuild — grew the list by the
	// whole configuration again and kept it for the life of the process. InitTree / FillData re-add
	// what they need; the indices they hand out are only ever read back from the same pass.
	if (wxImageList* imageList = m_metaTreeCtrl->GetImageList())
		imageList->RemoveAll();

	//initialize tree
	InitTree();

}

void ibConfigurationTree::FillData()
{
	ibValueMetaObject* commonMetadata = m_metaData->GetCommonMetaObject();
	wxASSERT(commonMetadata);

	m_metaTreeCtrl->SetItemText(m_treeRoot, m_metaData->GetConfigName());
	m_metaTreeCtrl->SetItemData(m_treeRoot, new ibTreeItemObject(commonMetadata));

	// ONE PASS OVER THE LAYOUT. Every group asks the metadata for its own kind and puts what comes
	// back in the way its row says. This was twenty copies of the loop below, one per group, each
	// with its own spelling of the same three tests — and the copies had already diverged (the
	// search test was written out in some and left commented out in others).
	for (const ibMetaTreeGroupDef& def : s_groups) {

		const wxTreeItemId group = Group(def.m_clsid);
		if (!group.IsOk())
			continue;

		for (auto metaObject : m_metaData->GetAnyArrayObject(def.m_clsid)) {

			if (metaObject->IsDeleted())
				continue;

			wxTreeItemId node;
			switch (def.m_row) {
			case ibMetaRow::Command:
				node = AppendCommandNode(group, metaObject);   // hub — nests sub-commands, skips deleted
				break;
			case ibMetaRow::Group:
				// A section holds sections, so its row is a group node in its own right.
				node = AppendGroupItem(group, def.m_clsid, metaObject);
				ExpandMetaItem(metaObject, node);
				break;
			default:
				node = AppendItem(group, metaObject);
				ExpandMetaItem(metaObject, node);
				break;
			}

			// AN OBJECT SURVIVES A SEARCH IF IT MATCHED — or if anything inside it did. The unfold
			// above has already filtered its contents, so "nothing left under it" is the answer to
			// the second half. This is what makes searching for an attribute name show the catalog
			// that carries it, instead of finding nothing at all.
			if (!m_strSearch.IsEmpty() && node.IsOk() && !MatchesSearch(metaObject)
				&& !m_metaTreeCtrl->HasChildren(node))
				m_metaTreeCtrl->Delete(node);
		}
	}

	// A GROUP THAT MATCHED NOTHING GOES AWAY — but ONLY while a search is running. Empty is the
	// normal state of a group otherwise: it is where an object of that kind gets created, so
	// hiding it would hide the way in.
	//
	// BOTTOM-UP, because a nested group counts as a child of its owner: sweeping top-down left the
	// jobs branch standing on the strength of a sub-branch that the same pass was about to remove.
	if (!m_strSearch.IsEmpty()) {
		for (auto def = std::rbegin(s_groups); def != std::rend(s_groups); ++def) {
			const wxTreeItemId group = Group(def->m_clsid);
			if (group.IsOk() && !m_metaTreeCtrl->HasChildren(group)) {
				m_metaTreeCtrl->Delete(group);
				m_groups.erase(def->m_clsid);   // the entry goes with the node — no dangling id
			}
		}
	}

	// 🛑 AND THE MODIFIED MARK IS NOT SAID FROM HERE. Drawing rows is a READ, and a read is not an
	// occasion on which anything became modified — but this stood at the end of the fill, so every
	// road that redraws reported it again: the load fills, the Loaded/Run/Reverted stage fills, and
	// SEARCH fills on every keystroke. The frame's caption arms itself on the first report it hears
	// and paints the asterisk on the SECOND, so a plain open — one fill plus one more signal — lit
	// a mark that means "you have edits" over a configuration nobody had touched.
	//
	// The rule it broke is the whole of it (Max, 2026-09-05): *the flag is set ONCE at load, and
	// after that only if somebody actually changes something.* Both of those already have a road —
	// the load says it below, and an edit arrives as MetaDataChanged / the Saved stage. A third
	// sender could only repeat them.

	//update toolbar
	UpdateToolbar(commonMetadata, m_treeRoot);

	// ⭐⭐ AND IT IS OPENED, because a tree nobody can see is not filled in any sense that matters.
	//
	// 🛑 THESE THREE LINES STOOD AT THE CALLERS — in Load and in Search, written out twice — and the
	// THIRD road that rebuilds, the Loaded/Run stage, did not have them. So a rollback left the
	// navigator holding its root with twelve group rows under it, collapsed: everything present,
	// nothing visible. Clearing the search box "fixed" it because that goes through Search, which
	// expands (Max, 2026-09-01: *"you can simply press the clear in the search and the tree
	// appears"* — the whole diagnosis was in that sentence).
	//
	// Finishing the fill is the fill's own business.
	m_metaTreeCtrl->SelectItem(m_treeRoot);
	m_metaTreeCtrl->Expand(m_treeRoot);
	m_metaTreeCtrl->Expand(m_treeCOMMON);
}

bool ibConfigurationTree::Load(ibMetaDataConfigurationBase* metaData)
{
	m_metaTreeCtrl->Freeze();
	CloseDocuments();   // a configuration is being left — its editors go with it
	ClearTree();

	m_metaData = metaData ? metaData : appEnv::ActiveMetaData();
	WatchMetaData(m_metaData);   // off the old list, onto this one — one call, one place
	FillData(); //Fill all data from metaData

	// …and the metadata learns whether this file may be edited at all. Said HERE because the view
	// sets it on the widget before the metadata is known, and because one file has one view — so
	// there is nobody to disagree with.
	m_metaData->SetReadOnly(m_bReadOnly);

	// ⭐ ONCE, HERE — the caption is drawn for the configuration just taken. This is the load half of
	// the rule above; everything after it arrives as a signal, because from now on the tree IS on the
	// watcher list. At the FIRST open it is not yet: the metadata settles its flag while it is being
	// read (Modify(!CompareMetadata) in ibMetaDataConfigurationStorage::LoadDatabase), which is before
	// anything here exists to hear it — so without this call the caption would never be drawn at all.
	Modify(m_metaData->IsModified());

	m_metaTreeCtrl->Thaw();
	return true;
}

bool ibConfigurationTree::Save()
{
	wxASSERT(m_metaData);

	if (m_metaData->IsModified() && wxMessageBox(wxString::Format(_("Configuration '%s' has been changed. Save?"), m_metaData->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES)
		return m_metaData->SaveDatabase();

	return false;
}

/////////////////////////////////////////////////////////////

void ibConfigurationTree::Search(const wxString& strSearch)
{
	m_metaTreeCtrl->Freeze();

	//InitTree();
	ClearTree();

	m_strSearch = strSearch;

	FillData(); //Fill all data from metaData — which opens the root and Common on its way out

	// SEARCHING opens everything, because a match three levels down is not an answer until it can
	// be seen. That is this caller's own business and stays here.
	if (!m_strSearch.IsEmpty())
		m_metaTreeCtrl->ExpandAll();

	m_strSearch = wxEmptyString;

	m_metaTreeCtrl->Thaw();
}

/////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////
//     the three configuration verbs (ibMetaDataNotifier)
/////////////////////////////////////////////////////////////
//
// ⭐ ONE ROAD, REACHED FROM BOTH SIDES. These are what the Configuration menu's items do, and what
// the assistant's config_* tools do — the same function, not two that agree. They live on the TREE
// because ibMetaDataNotifier is the interface the backend already holds: a tool asks a
// configuration for its tree and finds them here, while the designer's menu redirects into the
// same three.
//
// ⭐ AND THE TREE'S OWN CONFIGURATION IS THE ONE ACTED ON — m_metaData, never the `activeMetaData`
// global. Several are open at once (the base, one being compared, one from a file, external
// processors and reports), so "the active one" is not the same question as "mine": reaching for
// the global here would apply an edit to a configuration the caller never named, and it would do
// it quietly.
//
// ⚠ AND THE PRESENCE OF A TREE IS THE CAPABILITY. A configuration with no metadata tree is a
// runtime host, and restructuring is not something it does — the base class refuses in words for
// that case, and these overrides exist only where a designer does.

