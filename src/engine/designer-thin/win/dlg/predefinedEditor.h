#ifndef __PREDEFINED_EDITOR__
#define __PREDEFINED_EDITOR__

#include <wx/aui/auibar.h>

#include "frmclient/backend/metaCollection/partial/commonObject.h"
#include "gui/univ/dataview/dataview.h"

class ibTextCtrl;

class ibDialogPredefinedEditor : public wxDialog {

	enum {
		wxID_TOOL_ADD = wxID_HIGHEST + 1,
		wxID_TOOL_COPY,
		wxID_TOOL_EDIT,
		wxID_TOOL_DELETE,
	};

	typedef ibValueMetaObjectRecordDataHierarchyMutableRef::ibPredefinedItem   ibPredefinedItem;

	class ibDataViewPredefinedTreeStore : public ibDataViewModel
	{
	public:

		ibDataViewPredefinedTreeStore(ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObjectHierarchy)
			: ibDataViewModel(), m_valueMetaObjectHierarchy(valueMetaObjectHierarchy) {
		}


		// and implement some others by forwarding them to our own ones
		virtual void GetValue(ibVariant& variant,
			const ibDataViewItem& item, unsigned int col) const override;

		// return true if the given item has a value to display in the given
		// column: this is always true except for container items which by default
		// only show their label in the first column (but see HasContainerColumns())
		virtual bool HasValue(const ibDataViewItem& item, unsigned col) const override
		{
			if (HasContainerColumns(item))
				return false;
			return true;
		}

		virtual bool SetValue(const ibVariant& variant,
			const ibDataViewItem& item, unsigned int col) override {
			return false;
		}

		virtual bool GetAttr(const ibDataViewItem& item, unsigned int col,
			ibDataViewItemAttr& attr) const override {

			return false;
		}

		virtual bool IsEnabled(const ibDataViewItem& item, unsigned int col) const override
		{
			return m_valueMetaObjectHierarchy->IsEditable();
		}

		virtual ibDataViewItem GetParent(const ibDataViewItem& item) const override {
			// the invisible root node has no parent
			if (!item.IsOk())
				return ibDataViewItem(nullptr);

			const ibPredefinedItem* predefined =
				static_cast<const ibPredefinedItem*>(item.GetID());
			if (predefined == nullptr)
				return ibDataViewItem(nullptr);

			const wxObjectDataPtr<ibPredefinedItem> parent = predefined->GetPredefinedParent();
			return parent != nullptr ? ibDataViewItem(parent.get()) : ibDataViewItem(nullptr);
		}

		virtual bool IsContainer(const ibDataViewItem& item) const override;

		// define current parent for hierarchical view — single-shot
		// fetch on the new contract; non-paged, so Next/Prev stay at
		// the base default (return 0).
		virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
			const ibDataViewItem& anchor, int count,
			ibDataViewItemArray& out) const override;

		// override sorting to always sort branches ascendingly
		virtual bool HasDefaultCompare() const override { return true; }

		virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
			unsigned int col, bool ascending) const override {

			// items must be different
			wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
				id2 = wxPtrToUInt(item2.GetID());
			return ascending ? id1 - id2 : id2 - id1;
		}

		virtual bool IsListModel() const { return false; }
		virtual bool IsVirtualListModel() const { return false; }

		//is predefined value? 
		bool HasPredefinedValue(const ibGuid& predefinedGuid) const { return m_valueMetaObjectHierarchy->HasPredefinedValue(predefinedGuid); }
		bool HasPredefinedValue(const wxString& strPredefinedName) const { return m_valueMetaObjectHierarchy->HasPredefinedValue(strPredefinedName); }

		//append predefined value
		bool AppendPredefinedValue(const ibPredefinedItemDescription& item) { return m_valueMetaObjectHierarchy->AppendPredefinedValue(item); }
		bool SetPredefinedValue(const ibPredefinedItemDescription& item) { return m_valueMetaObjectHierarchy->SetPredefinedValue(item); }

		const ibPredefinedDescription& GetPredefinedDesc() const { return m_valueMetaObjectHierarchy->GetPredefinedDesc(); }

		// The items an item may sit under — the item dialog's "Parent" list: the folders, where the kind keeps folders
		// apart, every item otherwise; `excludeGuid` — the item edited, which cannot sit under itself.
		std::vector<wxObjectDataPtr<ibPredefinedItem>>
			GetParentCandidates(const ibGuid& excludeGuid = wxNullGuid) const {
			std::vector<wxObjectDataPtr<ibPredefinedItem>> out;
			for (const auto& object : m_valueMetaObjectHierarchy->GetPredefinedValueArray()) {
				if (GetPredefinedDesc().m_folders && !object->IsPredefinedFolder())
					continue;
				if (excludeGuid.isValid() && object->GetPredefinedGuid() == excludeGuid)
					continue;
				out.push_back(object);
			}
			return out;
		}

		void DeletePredefinedValue(const ibGuid& predefinedGuid) { m_valueMetaObjectHierarchy->DeletePredefinedValue(predefinedGuid); }

		//find predefined value
		wxObjectDataPtr<ibPredefinedItem> FindPredefinedValue(const ibGuid& predefinedGuid) const { return m_valueMetaObjectHierarchy->FindPredefinedValue(predefinedGuid); }
		wxObjectDataPtr<ibPredefinedItem> FindPredefinedValue(const wxString& predefinedName) const { return m_valueMetaObjectHierarchy->FindPredefinedValue(predefinedName); }

	private:

		ibValueMetaObjectRecordDataHierarchyMutableRef* m_valueMetaObjectHierarchy;
	};

	// ⭐⭐ ONE WINDOW FOR AN ITEM OF ANY KIND, BUILT FROM WHAT THE KIND SAYS (ibPredefinedDescription) — as the virtual table
	// parameters window is built from what a source takes. Its fields as the kind keeps them (a folder where folders are
	// told apart, a parent where items stand under one another, a code where there is one); a row per attribute, its
	// editor chosen by what the attribute holds (a closed set — a list; a flag — a tick; a type — the type picker; the
	// rest typed); a table per section whose rows the item owns. It knows no kind: a kind described tomorrow shows here.
	class ibDialogPredefinedItem : public wxDialog {
	public:

		// `itemGuid` — the item it writes (none: a new one); `source` — the item it starts from (none: an empty one): the
		// item itself to edit it, another to copy that.
		ibDialogPredefinedItem(ibDialogPredefinedEditor* owner, const ibGuid& itemGuid = wxNullGuid,
			const ibPredefinedItem* source = nullptr);

		void SetReadOnly(bool r = true);

	protected:

		void OnCommandOK(wxCommandEvent& event);
		void OnCommandCancel(wxCommandEvent& event) { event.Skip(); }

	private:

		// ONE ATTRIBUTE'S ROW — its description, the editor it got, and the value it holds: as the item declared it, or
		// as the person picked it (a type), kept as it came where the editor shows it unchanged.
		struct ibAttributeEditor {
			const ibPredefinedAttributeDescription* m_attribute = nullptr;
			wxCheckBox* m_check = nullptr;              // a flag
			ibTextCtrl* m_picked = nullptr;             // a value picked through its "...": a type, or one of the closed set
			wxTextCtrl* m_text = nullptr;               // a value typed
			std::optional<ibPredefinedCellDescription> m_held;
		};

		// ONE SECTION'S TABLE — whether the item declares its rows, the rows, and what each row held as it came.
		struct ibSectionEditor {
			const ibPredefinedTableDescription* m_table = nullptr;
			wxCheckBox* m_declared = nullptr;
			ibDataViewListCtrl* m_rows = nullptr;
			wxButton* m_add = nullptr;
			wxButton* m_remove = nullptr;
			wxButton* m_up = nullptr;
			wxButton* m_down = nullptr;
			ibPredefinedRowDescriptionVector m_held;   // row N as it came; a row added holds nothing
		};

		ibDialogPredefinedEditor* GetOwner() const { return static_cast<ibDialogPredefinedEditor*>(m_parent); }

		// The row of an attribute, under `parent`, into `sizer` (two columns: its caption, its editor).
		void AppendAttribute(wxWindow* parent, wxSizer* sizer, const ibPredefinedAttributeDescription& attribute,
			const ibPredefinedRowDescription& row);
		// The table of a section, on `parent`.
		wxSizer* AppendSection(wxWindow* parent, const ibPredefinedTableDescription& table,
			const std::map<ibMetaID, ibPredefinedRowDescriptionVector>& tables);
		void EnableSection(ibSectionEditor& section);
		void PickType(ibAttributeEditor& editor);

		// What the window says — refused, with the person told why, where a required value is not given.
		bool TakeRow(ibPredefinedRowDescription& row) const;
		bool TakeTables(std::map<ibMetaID, ibPredefinedRowDescriptionVector>& tables) const;

		ibGuid m_itemGuid;
		ibPredefinedDescription m_described;
		bool m_readOnly = false;

		wxCheckBox* m_checkFolder = nullptr;
		wxChoice* m_choiceParent = nullptr;
		// m_parentOptions[i] matches choice entry i; index 0 is "<none>" = nullptr.
		std::vector<wxObjectDataPtr<ibPredefinedItem>> m_parentOptions;

		wxTextCtrl* m_textName = nullptr;
		wxTextCtrl* m_textCode = nullptr;
		wxTextCtrl* m_textDescription = nullptr;

		std::vector<std::unique_ptr<ibAttributeEditor>> m_attributes;
		std::vector<std::unique_ptr<ibSectionEditor>> m_sections;

		wxButton* m_sdbSizerOK = nullptr;
	};

public:

	// full ctor
	ibDialogPredefinedEditor(wxWindow* parent, ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObjectHierarchy)
		:
		wxDialog(parent, wxID_ANY, _("Predefined values editor"),
			wxDefaultPosition, wxSize(500, 300), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		m_tableModelStore(nullptr)
	{
		// wxRefCounter starts at 1, so `new` IS our reference — there is nothing to add. An IncRef
		// on top of it is a second reference nobody holds and nobody drops: AssociateModel takes
		// its own (released by ~wxDataViewCtrl), our dtor releases this one, and the count stopped
		// at 1. The model and its rows then outlived the dialog.
		m_tableModelStore = new ibDataViewPredefinedTreeStore(valueMetaObjectHierarchy);

		CreateDialogView();
	}

	~ibDialogPredefinedEditor()
	{
		if (m_tableModelStore)
		{
			m_tableModelStore->DecRef();   // discard old model, if any
		}
	}

	void CreateDialogView();

	//is predefined value? 
	bool HasPredefinedValue(const ibGuid& predefinedGuid) const { return m_tableModelStore->HasPredefinedValue(predefinedGuid); }
	bool HasPredefinedValue(const wxString& strPredefinedName) const { return m_tableModelStore->HasPredefinedValue(strPredefinedName); }

	//append predefined value
	bool AppendPredefinedValue(const ibPredefinedItemDescription& item) { return m_tableModelStore->AppendPredefinedValue(item); }
	bool SetPredefinedValue(const ibPredefinedItemDescription& item) { return m_tableModelStore->SetPredefinedValue(item); }

	const ibPredefinedDescription& GetPredefinedDesc() const { return m_tableModelStore->GetPredefinedDesc(); }

	void DeletePredefinedValue(const ibGuid& predefinedGuid) { m_tableModelStore->DeletePredefinedValue(predefinedGuid); }

	//find predefined value
	wxObjectDataPtr<ibPredefinedItem> FindPredefinedValue(const ibGuid& predefinedGuid) const { return m_tableModelStore->FindPredefinedValue(predefinedGuid); }
	wxObjectDataPtr<ibPredefinedItem> FindPredefinedValue(const wxString& predefinedName) const { return m_tableModelStore->FindPredefinedValue(predefinedName); }

	// Parent picker helper — forwards to the tree store.
	std::vector<wxObjectDataPtr<ibPredefinedItem>>
		GetParentCandidates(const ibGuid& excludeGuid = wxNullGuid) const {
		return m_tableModelStore->GetParentCandidates(excludeGuid);
	}

protected:

	void OnContextMenu(ibDataViewEvent& event);
	void OnCommandMenu(wxCommandEvent& event);
	void OnItemActivated(ibDataViewEvent& event);

private:

	wxAuiToolBar* m_toolbarTableEditor;

	wxAuiToolBarItem* m_toolAdd;
	wxAuiToolBarItem* m_toolCopy;
	wxAuiToolBarItem* m_toolEdit;
	wxAuiToolBarItem* m_toolDelete;

	ibDataViewCtrl* m_tableEditor;

	ibDataViewPredefinedTreeStore* m_tableModelStore;
};

#endif