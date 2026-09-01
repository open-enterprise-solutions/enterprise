#ifndef _METATREE_H__
#define _METATREE_H__

#include "frontend/docView/docView.h"
#include "backend/metadataConfiguration.h"
#include "backend/debugger/debugDefs.h"

#include <wx/aui/aui.h>
#include <wx/srchctrl.h>
#include <wx/treectrl.h>

#include <map>
#include <set>

// ⭐ WHAT A TREE ITEM CARRIES — a metatype, a metaobject, or both. Two words, used the same way all
// the way down: `Clsid` for the metatype, `Object` for the metaobject.
//
// 🛑 THE SAME THING HAD THREE VOCABULARIES. The mixins said `ClassIdentifier` against `MetaItem`;
// the wx nodes over them said `Clsid` against `Meta` and wore a `wx` prefix though they are ours;
// and the accessors said `Identifier` for a row. Three pairs of names for one thing, and every
// reader had to learn all three (Max, 2026-09-01).
//
// They used to sit as protected members of the backend's tree interface, which is how the engine
// came to know that a watcher has ROWS in it. Nothing outside these three files touches them.
struct ibTreeData {
	bool m_expanded = false;
};

struct ibTreeDataClsid : ibTreeData {
	ibClassID m_clsid; // element type
public:
	ibTreeDataClsid(const ibClassID& clsid) :
		m_clsid(clsid) {
	}
};

struct ibTreeDataObject : ibTreeData {
	ibValueMetaObject* m_metaObject; // element type
public:
	ibTreeDataObject(ibValueMetaObject* metaObject) :
		m_metaObject(metaObject) {
	}
};



// ⚠ IT IS A PANEL, AND IT IS THE THING OTHERS SAY THINGS TO — ibMetaTreeAbstract (docView.h) is the
// second half: four verbs, declared where the front end can see them and implemented here.
class ibMetaTreeBase : public wxPanel, public ibMetaTreeAbstract {
	wxDECLARE_ABSTRACT_CLASS(ibMetaTreeBase);
public:

	// PAIRED UI STATE SURVIVES AN EARLY EXIT — the same rule as ibClipboardLock and wx's own
	// wxWindowUpdateLocker. Events are switched off around a total rebuild, and a throw from
	// InitTree (metadata is read there) used to fly past the switch-back: the navigator stayed
	// deaf to every click for the rest of the session, with nothing on screen to say why.
	class ibEventsOff {
		wxWindow* const m_window;
	public:
		explicit ibEventsOff(wxWindow* window) : m_window(window) { m_window->SetEvtHandlerEnabled(false); }
		~ibEventsOff() { m_window->SetEvtHandlerEnabled(true); }
	};

	// ⭐⭐ THE SUBSCRIPTION IS MADE HERE AND DIES HERE (Max, 2026-09-01: *"it is set in the
	// constructor and lives in the class's module"*) — which is why all four are out of line: this
	// header does not know what an ibMetaTreeNotifier is, and that is the point.
	//
	// 🛑 IT USED TO BE HANDED IN: SetNotifier / GetNotifier / DetachNotifier, called from four
	// places, with three destructors detaching by hand — and the designer window inherited
	// ibMetaTreeNotifier to have one to hand over, while overriding NOTHING in it. Everything the
	// notifier receives it forwards to this tree, so the whole handover carried nothing.
	//
	// The other members carry their defaults at the DECLARATION, so a ctor states only what it is
	// TOLD — which is also why the (wxWindow*, int) form cannot differ from its neighbours by
	// forgetting one.
	ibMetaTreeBase();
	ibMetaTreeBase(wxWindow* parent, int id = wxID_ANY);
	ibMetaTreeBase(ibMetaDocument* docParent, wxWindow* parent, int id = wxID_ANY);
	virtual ~ibMetaTreeBase();

	// ⭐ WHICH METADATA THIS TREE WATCHES — the ONE place a subscription starts, changes and ends.
	// Called from every Load: being given a metadata IS subscribing to it.
	//
	// ⭐⭐ THE FIELD IS HERE, BESIDE THE SWAP. It used to live in the notifier, which then had to
	// remember a metadata just to be able to leave its list (Max, 2026-09-01: *"take the metadata
	// out of there — the tree has it"*). A metadata is the tree's: the tree is given one, shows it,
	// saves it. The subscription only has to know who to forward to.
	void WatchMetaData(ibMetaData* metaData);

	// ⭐⭐ THE WHOLE CYCLE, ANSWERED ONCE FOR ALL THREE TREES. Add, remove, rename, copy, paste — the
	// configuration's navigator, the external report's and the external data processor's all show a
	// metadata and all have to catch up the same way, so the answer lives in the base and none of
	// them writes its own.
	//
	// What differs between them is only HOW they rebuild, and that is asked of the subclass through
	// the three below.
	virtual void MetaDataChanged();

	// ⭐⭐ ONE OBJECT CHANGED, ONE ROW TOUCHED. The stage carries the object, so `Created` draws its
	// row, `Renamed` relabels it and `Removed` takes it away — the other nine hundred are not
	// touched (Max, 2026-09-01: *"you do not need to rebuild, you already know what changed"*).
	//
	// Emptying and refilling is what `Loaded` / `Run` mean, and nothing else: those are the stages
	// where the whole metadata is new, so nothing on screen still stands for anything.

	// A form is asked what kind it is — every tree that shows metadata to a person asks.
	void AskFormKind(ibValueMetaObject* object);

	virtual void MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object);

protected:

	// ⭐⭐ WHAT EVERY TREE HAS, HELD ONCE. All three kept these word for word: the row their contents
	// hang under, the group row per metatype, and whether the tree has been built yet. Only the
	// ROOT's NAME differed — m_treeMETADATA, m_treeDATAPROCESSORS, m_treeREPORTS — for one role
	// (Max, 2026-09-01: *"look, initialised, the group map, half of what is there is common"*).
	wxTreeItemId m_treeRoot;

	// THE GROUP NODES, keyed by the metatype each stands for. The layout is one table in each
	// tree's .cpp, and every pass over the tree reads this instead of a hand-kept list of fields.
	std::map<ibClassID, wxTreeItemId> m_groups;

	wxTreeItemId Group(const ibClassID& clsid) const {
		const auto it = m_groups.find(clsid);
		return it != m_groups.end() ? it->second : wxTreeItemId();
	}

	bool m_initialized = false;

	// ⭐⭐ THE FOUR WAYS A ROW IS MADE, written once. All three trees carried them verbatim: the
	// root, a group row for a metatype, a group row that IS a metaobject (a tabular section shows
	// its columns under itself), and a plain object row. Each takes its picture from the type or
	// the object and hands the row the payload that says what it stands for.
	wxTreeItemId AppendRootItem(const ibClassID& clsid, const wxString& name = wxEmptyString) const {
		const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
		wxASSERT(typeCtor);
		wxImageList* imageList = m_treeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(typeCtor->GetClassIcon());
		return m_treeCtrl->AddRoot(name.IsEmpty() ? typeCtor->GetClassName() : name,
			imageIndex, imageIndex, nullptr);
	}

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const ibClassID& clsid, const wxString& name = wxEmptyString) const {
		const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
		wxASSERT(typeCtor);
		wxImageList* imageList = m_treeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(typeCtor->GetClassIcon());
		return m_treeCtrl->AppendItem(parent, name.IsEmpty() ? typeCtor->GetClassName() : name,
			imageIndex, imageIndex, new ibTreeItemClsid(clsid));
	}

	wxTreeItemId AppendGroupItem(const wxTreeItemId& parent,
		const ibClassID& clsid, ibValueMetaObject* metaObject) const {
		wxImageList* imageList = m_treeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		return m_treeCtrl->AppendItem(parent, metaObject->GetName(),
			imageIndex, imageIndex, new ibTreeItemClsidObject(clsid, metaObject));
	}

	wxTreeItemId AppendItem(const wxTreeItemId& parent, ibValueMetaObject* metaObject) const {
		wxImageList* imageList = m_treeCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		return m_treeCtrl->AppendItem(parent, metaObject->GetName(),
			imageIndex, imageIndex, new ibTreeItemObject(metaObject));
	}

	// HUB — a command node and, recursively, its sub-commands: a group command holds commands the
	// way a subsystem holds subsystems. In the .cpp, where the full command type is known.
	//
	// ⚠ VIRTUAL, and one of the few things here that honestly is. The configuration's navigator has
	// a SEARCH BOX, so its version also drops a command that neither matched nor has a matching
	// command under it — a question the external trees cannot ask, having nothing to search.
	virtual wxTreeItemId AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command);

	// ⭐⭐ THE CONTROL, HELD HERE. Each tree builds its own and hands it over — and that is the whole
	// of what the shared cycle needed from a subclass.
	//
	// 🛑 THERE WERE NINE `Do*` VIRTUALS INSTEAD, IN ALL THREE TREES, WORD FOR WORD (Max, 2026-09-01,
	// three times: *"a pile of incomprehensible Do*, junk names, duplicated behaviour"*). Six of
	// them were one line on this pointer — set a label, read the selection, select, find a row, hand
	// back the control — and the other three were a SECOND NAME for a method the tree already had:
	// `DoClearTree() { ClearTree(); }`. An adapter over nothing, and `DoEraseItem` did not erase.
	//
	// What is genuinely a subclass's own is below: what it fills itself from, what it clears, what
	// it closes. Those keep the names they always had.
	wxTreeCtrl* m_treeCtrl = nullptr;

	virtual void ClearTree() = 0;
	virtual void FillData() = 0;

	// ONE ROW, DRAWN. What a row looks like is a tree's own — a tabular section opens as a group,
	// a command shows the commands under it — so this is asked rather than written here.
	virtual wxTreeItemId FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item,
		bool select = true, bool scroll = true) = 0;

	// Close what the row stood for — its open editors and its children's. It does NOT remove the
	// row; the stage handler does that, straight on the control.
	virtual void EraseItem(const wxTreeItemId& item) = 0;

	// ⭐ WHERE A NEW OBJECT GOES, and what it will be. Three questions about one row — which row
	// stands for a metatype, WHICH metatype, and which metaobject owns it — each in a form that
	// asks it of the SELECTION and a form that asks it of a given row.
	//
	// 🛑 ALL SIX WERE WRITTEN OUT IN ALL THREE TREES, word for word, over a control the base now
	// holds (Max, 2026-09-01: *"this can move too"*). Nothing in them was any tree's own.
	wxTreeItemId GetSelectionIdentifier() const {
		return m_treeCtrl != nullptr
			? GetSelectionIdentifier(m_treeCtrl->GetSelection()) : wxTreeItemId();
	}

	wxTreeItemId GetSelectionIdentifier(const wxTreeItemId& id) const {
		if (m_treeCtrl == nullptr)
			return wxTreeItemId();

		wxTreeItemId parentItem = id;
		while (parentItem != nullptr) {
			if (dynamic_cast<ibTreeDataClsid*>(m_treeCtrl->GetItemData(parentItem)) != nullptr)
				return parentItem;
			parentItem = m_treeCtrl->GetItemParent(parentItem);
		}
		return wxTreeItemId(nullptr);
	}

	ibClassID GetClassIdentifier() const {
		return GetClassIdentifier(GetSelectionIdentifier());
	}

	ibClassID GetClassIdentifier(const wxTreeItemId& id) const {
		if (m_treeCtrl == nullptr)
			return 0;

		const ibTreeDataClsid* data = dynamic_cast<ibTreeDataClsid*>(m_treeCtrl->GetItemData(id));
		return data != nullptr ? data->m_clsid : 0;
	}

	ibValueMetaObject* GetMetaIdentifier() const {
		return GetMetaIdentifier(GetSelectionIdentifier());
	}

	// THE NEAREST METAOBJECT AT OR ABOVE a group row — what a new object gets created under.
	// ⚠ An inner `wxTreeItemData*` used to sit in the loop, shadowing the outer one (C4456) and
	// answering nothing: GetMetaObject already returns null for a row that carries no metaobject.
	ibValueMetaObject* GetMetaIdentifier(const wxTreeItemId& id) const {
		if (m_treeCtrl == nullptr)
			return nullptr;
		if (dynamic_cast<ibTreeDataClsid*>(m_treeCtrl->GetItemData(id)) == nullptr)
			return nullptr;

		wxTreeItemId parentItem = id;
		while (parentItem != nullptr) {
			ibValueMetaObject* parent = GetMetaObject(parentItem);
			if (parent != nullptr) return parent;
			parentItem = m_treeCtrl->GetItemParent(parentItem);
		}
		return nullptr;
	}

	// Every open editor re-gathers — a form's command navigator lists what the metadata holds.
	void NotifyDocuments() const;

	// Close every editor opened over this metadata — the answer to the `Closed` stage, and to
	// leaving a configuration. It was written out three times, word for word, with nothing in it
	// that was any one tree's own.
	void CloseDocuments();

	// Close every editor opened over this metadata — the answer to the `Closed` stage. Written out
	// three times, word for word, with nothing in it that was any tree's own: what it asks is
	// "whose metadata is this document showing", and the answer is the same question in all three.

	// What a row stands for, or null. Not a question for a subclass: every tree stores it in the
	// same payload (ibTreeDataObject), so the same two lines answered it three times over.
	ibValueMetaObject* GetMetaObject(const wxTreeItemId& item) const {
		if (m_treeCtrl == nullptr || !item.IsOk())
			return nullptr;

		const ibTreeDataObject* data = dynamic_cast<ibTreeDataObject*>(m_treeCtrl->GetItemData(item));
		return data != nullptr ? data->m_metaObject : nullptr;
	}

	// …and these are the shared ones, on the control above.
	//
	// The row carrying this metaobject. Invalid when it has none: a real state, not an error — a
	// group the search filter dropped has no row, and neither has an object under a branch that was
	// never expanded. The two-argument form is the walk itself, from a row downwards.
	wxTreeItemId FindItemByMetaObject(const ibValueMetaObject* object) const;
	wxTreeItemId FindItemByMetaObject(const wxTreeItemId& from, const ibValueMetaObject* object) const;

	// The control's own name for it. ⚠ Not `SetLabel`: this class is a wxPanel, and an overload
	// would hide wxWindow::SetLabel for every caller of the window's own label.
	void SetItemText(const wxTreeItemId& item, const wxString& text) {
		if (m_treeCtrl != nullptr) m_treeCtrl->SetItemText(item, text);
	}

	ibValueMetaObject* GetSelectedObject() const {
		return m_treeCtrl != nullptr ? GetMetaObject(m_treeCtrl->GetSelection()) : nullptr;
	}

	// ⚠ EVENTS STAY ON, and that is deliberate. Selecting is what makes the object inspector follow
	// and the row read as ACTIVE — switch them off and an object with no editor window of its own is
	// added and then sits there unselected (Max, 2026-09-01: *"the tree does not activate it, if it
	// has no editing window"*).
	void SelectItem(const wxTreeItemId& item) {
		if (m_treeCtrl == nullptr) return;
		m_treeCtrl->SelectItem(item);
		m_treeCtrl->Expand(item);
		m_treeCtrl->EnsureVisible(item);
	}

	// ⭐⭐ WHAT WAS OPEN STAYS OPEN. A refresh destroys every row and builds it again, and the
	// expanded state lives IN the rows — so Common folded shut on every property write (Max,
	// 2026-09-01: *"Common keeps collapsing"*). It never showed before because the tree was only
	// rebuilt on a load or a search; now it is rebuilt whenever the metadata says it changed.
	//
	// ⚠ CAPTURED AS A PATH OF LABELS, not as rows or payloads: a group row carries a clsid, a
	// metaobject row carries a pointer, and `Common` carries NOTHING at all — three kinds to
	// special-case, when the one thing every row has is its text and its place under its parent.


public:

	// ⭐⭐ THE METAOBJECT'S OWN PART OF THE CONTEXT MENU — see ibMetaMenuItem, and the tree's own
	// block follows it in PrepareContextMenu.
	//
	// The metaobject says WHAT it offers; this says what that looks like. The line between groups is
	// put here, where the kind changes, because a separator is a drawing decision and the backend
	// was making it. The ICON is asked of the item's metaobject, or taken from the item when it
	// names none.
	//
	// 🛑 IT WAS `AppendOpenItems` — from when every item was "open something", which stopped being
	// true when the modal editors joined them (Max, 2026-09-01: *"the name sticks out"*).
	//
	// ⭐ AND THE IDS ARE WX'S OWN. `NewControlId` gives one that cannot collide with a hand-written
	// one — they are negative, below wxID_AUTO_LOWEST — and the menu item owns it from the moment it
	// is appended (wxMenuItem holds a wxWindowIDRef), so nothing here reserves, tracks or releases
	// anything. That is what retired twenty per-metatype enumerations that almost all began at 19000
	// and were unique only within one class; see the note on ID_METATREE_LAST below for the day that
	// cost a wrong dialog.
	//
	// ⭐⭐ EACH ITEM CARRIES ITS OWN ACTION, bound on the menu — so between building the menu and
	// clicking it there is no state at all. See the note on the body for what that replaced.
	//
	// Returns what CollectContextMenu returned: TRUE when the standard New / Edit / Remove / Properties
	// block does not apply to this row.
	bool AppendMetaMenu(wxMenu* menu, ibValueMetaObject* metaObject);



	// One refresh per burst — see MetaDataChanged.


public:

	virtual void Activate() override;

	virtual void SetReadOnly(bool readOnly = true) { m_bReadOnly = readOnly; }
	virtual bool IsEditable() const { return !m_bReadOnly; }

	virtual void Modify(bool modify);

	virtual bool OpenObjectForm(ibValueMetaObject* metaObject) override;

#pragma region __predefined_values_h__
	virtual void EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* obj) {}

	// The other modal editor over a metaobject — the config root's start page. Both are here rather
	// than only on ibConfigurationTree because the notifier forwards to THIS type: a tree that has
	// no such editor answers by doing nothing, which is the honest answer for an external file.
	virtual void EditHomePage(ibValueMetaObjectConfiguration* obj) {}
#pragma endregion

	virtual ibMetaDocument* GetDocument(ibValueMetaObject* metaObject) const override;

	// ⭐⭐ SHOW ME THIS MODULE, AT THIS LINE — the tree's own verb, and universal: every tree has a
	// metadata, so every tree can answer it (Max, 2026-09-01: *"the tree gets a universal Edit, the
	// way it WAS"*). It opens the module's editor and puts the caret on the line; `setRunLine` is
	// what tells an arrow from a jump.
	//
	// ⚠ ITS OWN METADATA, which is the whole reason this belongs here. A module named by the
	// debugger belongs either to the open configuration or to an external data processor opened as
	// a FILE — and each of those is a tree with its own container, so the answer is whichever tree
	// is asked.
	virtual void EditModule(const ibGuid& moduleName, int line, bool setRunLine = true) override;

	// The default-form combo an EXTERNAL tree carries beside its rows. Nothing in the configuration
	// navigator, which has no such combo — but the notifier forwards it to every tree alike.
	virtual void UpdateChoiceSelection() {}

	// GetMetaData is declared by ibMetaTreeAbstract — it is one of the three things the debugger
	// reuses, so it belongs to the interface rather than to this class. Subclasses still override it
	// covariantly (ibConfigurationTree returns ibMetaDataConfigurationBase*).

protected:

	class ibTreeItemClsid : public wxTreeItemData,
		public ibTreeDataClsid {
	public:
		ibTreeItemClsid(const ibClassID& clsid) : ibTreeDataClsid(clsid) {}
	};

	class ibTreeItemObject : public wxTreeItemData, public ibTreeDataObject {
	public:
		ibTreeItemObject(ibValueMetaObject* metaObject) : ibTreeDataObject(metaObject) {}
	};

	class ibTreeItemClsidObject : public wxTreeItemData,
		public ibTreeDataObject, public ibTreeDataClsid {
	public:
		ibTreeItemClsidObject(const ibClassID& clsid, ibValueMetaObject* metaObject) :
			ibTreeDataObject(metaObject), ibTreeDataClsid(clsid)
		{
		}
	};

	void CreateToolBar(wxWindow* parent);


	enum
	{
		ID_METATREE_NEW = 15000,
		ID_METATREE_EDIT,
		ID_METATREE_DELETE,
		ID_METATREE_PROPERTY,

		ID_METATREE_UP,
		ID_METATREE_DOWM,

		ID_METATREE_SORT,

		// The two texts an object carries. Separate items because they are separate readers:
		// help is what a USER meets on F1, notes are the engineering intent and never leave
		// the designer.
		ID_METATREE_HELP,
		ID_METATREE_NOTES,

		// 🛑 WHERE THIS RANGE ENDS, SAID OUT LOUD. A derived tree used to anchor its own ids on
		// `ID_METATREE_SORT + 1` — a SIBLING, not the end — so the day two entries were appended
		// here they landed exactly on top of Insert and Replace, and asking for help opened the
		// "replace report" file dialog instead. The overlap was silent: two enums, no compiler on
		// earth to notice, and the symptom pointed at the wrong subsystem entirely.
		//
		// Anchor derived ranges on THIS, never on the last member you happen to see.
		ID_METATREE_LAST,
	};

protected:

	// Initialised HERE rather than in each constructor: there are three of them, one of them
	// (wxWindow*, int) never set m_searchTree, and the destructor calls Destroy() on it.
	wxSearchCtrl* m_searchTree = nullptr;
	wxAuiToolBar* m_metaTreeToolbar = nullptr;
	// ⭐⭐ THE SUBSCRIPTION — DECLARED HERE, DEFINED IN THE MODULE, and so reachable from nowhere
	// else (Max, 2026-09-01: *"it is not accessible from outside at all; you just define it as a
	// class, and that is it"*). It is what a metadata keeps a list of, so the backend never learns
	// that a watcher is a wxPanel; every method it has is one line to this tree.
	//
	// 🛑 IT WAS A PUBLIC CLASS IN THIS HEADER, with SetOwner / GetOwner / WatchMetaData / GetWatched
	// on it, held by four owners who handed one in — and one of them, the designer window, INHERITED
	// it to have one to hand over while overriding nothing at all. Nobody outside this file needs to
	// know it exists.
	class ibMetaTreeNotifier* m_notifier = nullptr;

	// …and whose list it is on. Swapped only by WatchMetaData; null means watching nothing, which
	// is the ordinary state of a navigator that has not been given a configuration yet.
	ibMetaData* m_watched = nullptr;

	ibMetaDocument* m_docParent = nullptr;

	bool			m_bReadOnly = false;
};

class ibConfigurationTree : public ibMetaTreeBase {
	wxDECLARE_DYNAMIC_CLASS(ibConfigurationTree);
private:

	wxTreeItemId m_treeCOMMON; //special tree

	// (No named field per group. Two callsites need a specific group — "Insert data processor /
	//  report" and its context menu — and they ask for it by metatype: Group(g_metaReportCLSID).
	//  A field per group meant twenty-three assignments to serve those two, which is the same
	//  hand-kept list the layout table exists to remove.)

private:

	mutable wxString m_strSearch;

	enum
	{
		// PAST THE BASE'S WHOLE RANGE — see ID_METATREE_LAST. Anchoring on a particular member of
		// it made these collide the moment the base grew.
		ID_METATREE_INSERT = ID_METATREE_LAST,
		ID_METATREE_REPLACE,
		ID_METATREE_SAVE,
	};

private:

	ibMetaDataConfigurationBase* m_metaData;

	class ibMetaTreeCtrl : public wxTreeCtrl {
		// NAMES ITSELF — it used to name the owner tree. The macro ignores its argument, so that
		// compiled and simply read as a lie; the matching wxIMPLEMENT in the .cpp has always been
		// the nested class. The same slip was in both external trees and was fixed with this one.
		wxDECLARE_DYNAMIC_CLASS(ibMetaTreeCtrl);

		class ibMetaTreeView : public ibMetaView
		{
		public:

			ibMetaTreeView(ibMetaTreeCtrl* tree) : m_ownerTree(tree) {}
			virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;

		private:
			ibMetaTreeCtrl* m_ownerTree;
		};

	private:
		ibConfigurationTree* m_ownerTree;
		ibMetaView* m_metaView;
	private:
		wxTreeItemId m_draggedItem;
	public:

		ibValueMetaObject* GetMetaObject(const wxTreeItemId& item) const {
			if (!item.IsOk()) return nullptr;
			ibTreeDataObject* data = dynamic_cast<ibTreeDataObject*>(GetItemData(item));
			if (data == nullptr) return nullptr;
			return data->m_metaObject;
		}

		void RefreshSelectedItem(bool scroll = true) {

			const wxTreeItemId& item = GetSelection();

			if (scroll)
				wxTreeCtrl::ScrollTo(item);

			wxTreeCtrl::Refresh();
			wxTreeCtrl::Update();
		}

		ibMetaTreeCtrl();
		ibMetaTreeCtrl(ibConfigurationTree* parent);
		virtual ~ibMetaTreeCtrl();

		// this function is called to compare 2 items and should return -1, 0
		// or +1 if the first item is less than, equal to or greater than the
		// second one. The base class version performs alphabetic comparison
		// of item labels (GetText)
		// ⚠ ASK THE MIXIN, not the concrete node class. `ibTreeItemObject` is the payload of a
		// PLAIN row; a row that is also a group (a tabular section, a section) carries
		// `ibTreeItemClsidObject` instead, and a cast to the concrete class missed it — so
		// sorting the Tables group did nothing here while the external trees, which have always
		// asked the mixin, reordered the tabular sections. Sorting a group's contents is what the
		// button means, so all three now agree on the external trees' behaviour.
		virtual int OnCompareItems(const wxTreeItemId& item1,
			const wxTreeItemId& item2) {
			int ret = wxStrcmp(GetItemText(item1), GetItemText(item2));
			ibTreeDataObject* data1 = dynamic_cast<ibTreeDataObject*>(GetItemData(item1));
			ibTreeDataObject* data2 = dynamic_cast<ibTreeDataObject*>(GetItemData(item2));
			if (data1 != nullptr && data2 != nullptr && ret > 0) {
				ibValueMetaObject* metaObject1 = data1->m_metaObject;
				ibValueMetaObject* metaObject2 = data2->m_metaObject;
				ibValueMetaObject* parent = metaObject1->GetParent();
				wxASSERT(parent);
				return parent->ChangeChildPosition(metaObject2,
					parent->GetChildPosition(metaObject1)
				) ? ret : wxNOT_FOUND;
			}
			return ret;
		}

		//events:
		void OnLeftDClick(wxMouseEvent& event);
		void OnLeftUp(wxMouseEvent& event);
		void OnLeftDown(wxMouseEvent& event);
		void OnRightUp(wxMouseEvent& event);
		void OnRightDClick(wxMouseEvent& event);
		void OnRightDown(wxMouseEvent& event);
		void OnKeyUp(wxKeyEvent& event);
		void OnKeyDown(wxKeyEvent& event);
		void OnMouseMove(wxMouseEvent& event);

		void OnBeginDrag(wxTreeEvent& event);
		void OnEndDrag(wxTreeEvent& event);

		void OnStartSearch(wxCommandEvent& event);
		void OnCancelSearch(wxCommandEvent& event);

		void OnItemActivated(wxTreeEvent& event);
		void OnCreateItem(wxCommandEvent& event);
		void OnEditItem(wxCommandEvent& event);
		void OnRemoveItem(wxCommandEvent& event);
		void OnPropertyItem(wxCommandEvent& event);

		void OnUpItem(wxCommandEvent& event);
		void OnDownItem(wxCommandEvent& event);

		void OnSortItem(wxCommandEvent& event);

		void OnInsertItem(wxCommandEvent& event);
		void OnReplaceItem(wxCommandEvent& event);
		void OnSaveItem(wxCommandEvent& event);


		void OnCopyItem(wxCommandEvent& event);
		void OnPasteItem(wxCommandEvent& event);

		// The two texts a metaobject carries — see the note where the menu items are appended.
		void OnEditHelp(wxCommandEvent& event);
		void OnEditNotes(wxCommandEvent& event);

		void OnSetFocus(wxFocusEvent& event);

		void OnSelecting(wxTreeEvent& event);
		void OnSelected(wxTreeEvent& event);

		void OnCollapsing(wxTreeEvent& event);
		void OnExpanding(wxTreeEvent& event);

	protected:

		wxDECLARE_EVENT_TABLE();
	};

	ibMetaTreeCtrl* m_metaTreeCtrl;

private:


	// FILL ONE GROUP OF AN OBJECT — append what answers the search, then drop the group itself when
	// the search left it empty. This is what makes the search box a filter rather than a top-level
	// lookup: the test was written out at every one of these loops and left COMMENTED OUT in all of
	// them, so a search could find a catalog by name but never an attribute, a form or a template.
	template <typename TArray>
	wxTreeItemId AppendObjectGroup(const wxTreeItemId& parent, const ibClassID& groupClsid,
		const wxString& label, const TArray& objects) {
		const wxTreeItemId group = AppendGroupItem(parent, groupClsid, label);
		for (auto object : objects) {
			if (!object->IsAcceptedByParent())   // gone, or a kind its owner does not host — one question
				continue;
			if (!MatchesSearch(object))
				continue;
			AppendItem(group, object);
		}
		if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(group)) {
			m_metaTreeCtrl->Delete(group);
			return wxTreeItemId();
		}
		return group;
	}

	// TABULAR SECTIONS — the one group whose rows are groups themselves: each table shows its own
	// columns. A table survives a search if IT matched or one of its columns did.
	template <typename TArray>
	wxTreeItemId AppendTableGroup(const wxTreeItemId& parent, const ibClassID& tableClsid,
		const wxString& label, const TArray& tables) {
		const wxTreeItemId group = AppendGroupItem(parent, tableClsid, label);
		for (auto metaTable : tables) {
			// A PREDEFINED section is not shown, by the same rule and the same question as the attributes
			// below: its owner does not accept that clsid as a child, so it is not something a person put
			// there and there is nothing to do with it in the tree. The chart of accounts' analytics kinds
			// are edited on the account, not declared here.
			if (!metaTable->IsAcceptedByParent())
				continue;
			const wxTreeItemId hTable = AppendGroupItem(group, g_metaAttributeCLSID, metaTable);
			for (auto attribute : metaTable->GetAttributeArrayObject()) {
				if (!attribute->IsAcceptedByParent())
					continue;
				if (!MatchesSearch(attribute))
					continue;
				AppendItem(hTable, attribute);
			}
			if (!m_strSearch.IsEmpty() && !MatchesSearch(metaTable)
				&& !m_metaTreeCtrl->HasChildren(hTable))
				m_metaTreeCtrl->Delete(hTable);
		}
		if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(group)) {
			m_metaTreeCtrl->Delete(group);
			return wxTreeItemId();
		}
		return group;
	}

	// COMMANDS — the rows nest, so the filtering is AppendCommandNode's own job; this only decides
	// whether the group is left standing.
	template <typename TArray>
	wxTreeItemId AppendCommandGroup(const wxTreeItemId& parent, const wxString& label,
		const TArray& commands) {
		const wxTreeItemId group = AppendGroupItem(parent, g_metaCommandCLSID, label);
		for (auto metaCommand : commands)
			AppendCommandNode(group, metaCommand);
		if (!m_strSearch.IsEmpty() && !m_metaTreeCtrl->HasChildren(group)) {
			m_metaTreeCtrl->Delete(group);
			return wxTreeItemId();
		}
		return group;
	}

	// …and this navigator's version also FILTERS: a command survives a search if it matched or a
	// command under it did. Nothing else here has a search box.
	virtual wxTreeItemId AppendCommandNode(const wxTreeItemId& parent, ibValueMetaObject* command) override;

	void ActivateItem(const wxTreeItemId& item);

	ibValueMetaObject* NewItem(const ibClassID& clsid, ibValueMetaObject* parent, bool runObject = true);
	ibValueMetaObject* CreateItem(bool showValue = true);



public:

	// The three configuration verbs — see the notifier (metaData.h). PUBLIC, because they
	// override public virtuals and because reaching them is the whole point: the designer's menu
	// items redirect into them and so does the assistant, through the interface. Hiding an
	// override of a public method only means the base pointer can do what the derived one cannot.

private:

	// The row carrying a metaobject, searched from `from` down. Invalid when the
	// object has no row — a real state: a group the search filter dropped has
	// none, and neither has an object under a branch never expanded.

	wxTreeItemId FillItem(ibValueMetaObject* metaItem, const wxTreeItemId& item, bool select = true, bool scroll = true);
	void EditItem();
	void RemoveItem();
	void EraseItem(const wxTreeItemId& item);
	void SelectItem();
	void PropertyItem();

	// The row is the EVENT's — see the note on the bodies. Passing the selection instead was an
	// assert waiting for a moment when nothing is selected.
	void Collapse(const wxTreeItemId& item);
	void Expand(const wxTreeItemId& item);

	void UpItem();
	void DownItem();

	void SortItem();

	void InsertItem();
	void ReplaceItem();
	void SaveItem();

	void PrepareReplaceMenu(wxMenu* menu);
	void PrepareContextMenu(wxMenu* menu, const wxTreeItemId& item);

	// The help / technical-text section, appended to WHICHEVER menu was built — the one the
	// default branch writes, and the one a metaobject writes for itself.
	void AppendTextsMenu(wxMenu* menu);

	void ShowContextMenu(wxWindow* eventSrc, const wxTreeItemId& item, const wxPoint& pos);

	// HOW A METAOBJECT UNFOLDS — the one dispatcher, used by the initial fill AND the create path.
	// Which existing kind a metatype renders AS (a chart of accounts as a catalog, an accounting
	// register as an accumulation register) is the only real knowledge in it.
	void ExpandMetaItem(ibValueMetaObject* metaItem, const wxTreeItemId& item);

	// Does this object answer the search box — asked in one place, case-insensitive, name OR synonym.
	bool MatchesSearch(const ibValueMetaObject* metaObject) const;

	// Close every editor opened from this navigator. Part of LEAVING a configuration — deliberately
	// not part of ClearTree, which a search runs on every keystroke.

	void AddInterfaceItem(ibValueMetaObject* obj, const wxTreeItemId& item);

	void AddCatalogItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddDocumentItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddEnumerationItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddDataProcessorItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddReportItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddInformationRegisterItem(ibValueMetaObject* obj, const wxTreeItemId& item);
	void AddAccumulationRegisterItem(ibValueMetaObject* obj, const wxTreeItemId& item);

	void FillData();


	void UpdateToolbar(ibValueMetaObject* obj, const wxTreeItemId& item);

protected:

	// Nothing forwards from here any more — the base holds the control and asks this tree only for
	// what is genuinely its own (ClearTree / FillData / EraseItem / CloseDocuments /
	// GetMetaObject), by the names it already had.


public:

	bool RenameMetaObject(ibValueMetaObject* metaObject, const wxString& newName);

#pragma region __predefined_values_h__
	virtual void EditPredefinedValues(ibValueMetaObjectRecordDataHierarchyMutableRef* obj);
#pragma endregion

#pragma region __home_page_h__
	virtual void EditHomePage(ibValueMetaObjectConfiguration* obj);
#pragma endregion

	// ⭐ COVARIANT, so the window's Save / Apply / Rollback act on THIS navigator's configuration
	// and never on `activeMetaData` — several are open at once (a file, the database, one being
	// compared), and *the active one* is not the same question as *mine*.
	virtual ibMetaDataConfigurationBase* GetMetaData() const { return m_metaData; }

	ibConfigurationTree();
	ibConfigurationTree(wxWindow* parent, int id = wxID_ANY);
	ibConfigurationTree(ibMetaDocument* docParent, wxWindow* parent, int id = wxID_ANY);
	virtual ~ibConfigurationTree();

	void InitTree();

	bool Load(ibMetaDataConfigurationBase* metadata = nullptr);
	bool Save();

	void Search(const wxString& strSearch);

	void ActivateTree();
	void ClearTree();
};

#endif 