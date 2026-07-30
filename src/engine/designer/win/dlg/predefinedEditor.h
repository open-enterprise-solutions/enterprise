#ifndef __PREDEFINED_EDITOR__
#define __PREDEFINED_EDITOR__

#include <wx/aui/auibar.h>

#include "backend/metaCollection/partial/commonObject.h"
#include "frontend/win/ctrls/dataview/dataview.h"

class ibDialogPredefinedEditor : public wxDialog {

	enum {
		wxID_TOOL_ADD = wxID_HIGHEST + 1,
		wxID_TOOL_COPY,
		wxID_TOOL_EDIT,
		wxID_TOOL_DELETE,
	};

	typedef ibValueMetaObjectRecordDataHierarchyMutableRef::ibPredefinedValueObject
		ibPredefinedValueObject;

	class ibDataViewPredefinedTreeStore : public ibDataViewModel
	{
	public:

		ibDataViewPredefinedTreeStore(ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObjectHierarchy)
			: ibDataViewModel(), m_valueMetaObjectHierarchy(valueMetaObjectHierarchy) {
		}


		// and implement some others by forwarding them to our own ones
		virtual void GetValue(wxVariant& variant,
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

		virtual bool SetValue(const wxVariant& variant,
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

			const ibPredefinedValueObject* predefined =
				static_cast<const ibPredefinedValueObject*>(item.GetID());
			if (predefined == nullptr)
				return ibDataViewItem(nullptr);

			const wxObjectDataPtr<ibPredefinedValueObject> parent = predefined->GetPredefinedParent();
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
		void AppendPredefinedValue(const wxString& strPredefinedName,
			const wxString& strCode, const wxString& strDescription,
			bool valueIsFolder = false,
			const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>()) {
			m_valueMetaObjectHierarchy->AppendPredefinedValue(strPredefinedName,
				strCode, strDescription, valueIsFolder, valueParent);
		}

		void SetPredefinedValue(const ibGuid& predefinedGuid, const wxString& strPredefinedName,
			const wxString& strCode, const wxString& strDescription,
			bool valueIsFolder = false,
			const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>()) {
			m_valueMetaObjectHierarchy->SetPredefinedValue(predefinedGuid,
				strPredefinedName, strCode, strDescription, valueIsFolder, valueParent);
		}

		// Folders available as parent candidates — used by the item dialog
		// to populate its "Parent" wxChoice. `excludeGuid` removes the item
		// currently being edited so it can't become its own parent.
		std::vector<wxObjectDataPtr<ibPredefinedValueObject>>
			GetFolderCandidates(const ibGuid& excludeGuid = wxNullGuid) const {
			std::vector<wxObjectDataPtr<ibPredefinedValueObject>> out;
			for (const auto& object : m_valueMetaObjectHierarchy->GetPredefinedValueArray()) {
				if (!object->IsPredefinedFolder())
					continue;
				if (excludeGuid.isValid() && object->GetPredefinedGuid() == excludeGuid)
					continue;
				out.push_back(object);
			}
			return out;
		}

		void DeletePredefinedValue(const ibGuid& predefinedGuid) { m_valueMetaObjectHierarchy->DeletePredefinedValue(predefinedGuid); }

		//find predefined value
		wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const ibGuid& predefinedGuid) const { return m_valueMetaObjectHierarchy->FindPredefinedValue(predefinedGuid); }
		wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const wxString& predefinedName) const { return m_valueMetaObjectHierarchy->FindPredefinedValue(predefinedName); }

	private:

		ibValueMetaObjectRecordDataHierarchyMutableRef* m_valueMetaObjectHierarchy;
	};

	class ibDialogPredefinedItem : public wxDialog {
	public:

		ibDialogPredefinedItem(wxWindow* parent,
			const ibGuid& itemGuid = wxNullGuid,
			const wxObjectDataPtr<ibPredefinedValueObject>& currentParent = wxObjectDataPtr<ibPredefinedValueObject>(),
			bool isFolder = false,
			const wxString& strPredefinedName = wxT(""),
			const wxString& strCode = wxT(""),
			const wxString& strDescription = wxT("")) :
			wxDialog(parent, wxID_ANY, _("Predefined value"), wxDefaultPosition, wxDefaultSize),
			m_itemGuid(itemGuid)
		{
			wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

			const int kLabel = FromDIP(85);
			const int kPad   = FromDIP(5);

			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

			// --- Folder checkbox (top so it's easy to spot) ---
			wxBoxSizer* sizerFolder = new wxBoxSizer(wxHORIZONTAL);
			m_staticTextKind = new wxStaticText(this, wxID_ANY, _("Kind"), wxDefaultPosition, wxSize(kLabel, -1));
			sizerFolder->Add(m_staticTextKind, 0, wxALL, kPad);
			m_checkFolder = new wxCheckBox(this, wxID_ANY, _("Folder (group)"));
			m_checkFolder->SetValue(isFolder);
			sizerFolder->Add(m_checkFolder, 1, wxALL, kPad);
			mainSizer->Add(sizerFolder, 0, wxEXPAND);

			// --- Parent dropdown (folders only) ---
			wxBoxSizer* sizerParent = new wxBoxSizer(wxHORIZONTAL);
			m_staticTextParent = new wxStaticText(this, wxID_ANY, _("Parent"), wxDefaultPosition, wxSize(kLabel, -1));
			sizerParent->Add(m_staticTextParent, 0, wxALL, kPad);

			m_choiceParent = new wxChoice(this, wxID_ANY);
			m_choiceParent->Append(_("<none>"));
			m_parentOptions.clear();
			m_parentOptions.push_back(wxObjectDataPtr<ibPredefinedValueObject>()); // index 0 → no parent

			int selectParentIdx = 0;
			if (ibDialogPredefinedEditor* owner = static_cast<ibDialogPredefinedEditor*>(GetParent())) {
				for (auto& folder : owner->GetFolderCandidates(m_itemGuid)) {
					m_parentOptions.push_back(folder);
					m_choiceParent->Append(folder->GetPredefinedName());
					if (currentParent != nullptr &&
						folder->GetPredefinedGuid() == currentParent->GetPredefinedGuid())
						selectParentIdx = static_cast<int>(m_parentOptions.size()) - 1;
				}
			}
			m_choiceParent->SetSelection(selectParentIdx);
			sizerParent->Add(m_choiceParent, 1, wxALL, kPad);
			mainSizer->Add(sizerParent, 0, wxEXPAND);

			// --- Name ---
			wxBoxSizer* sizerName = new wxBoxSizer(wxHORIZONTAL);
			m_staticTextName = new wxStaticText(this, wxID_ANY, _("Name"), wxDefaultPosition, wxSize(kLabel, -1));
			sizerName->Add(m_staticTextName, 0, wxALL, kPad);
			m_textName = new wxTextCtrl(this, wxID_ANY, strPredefinedName);
			sizerName->Add(m_textName, 1, wxALL, kPad);
			mainSizer->Add(sizerName, 0, wxEXPAND);

			// --- Code ---
			wxBoxSizer* sizerCode = new wxBoxSizer(wxHORIZONTAL);
			m_staticCode = new wxStaticText(this, wxID_ANY, _("Code"), wxDefaultPosition, wxSize(kLabel, -1));
			sizerCode->Add(m_staticCode, 0, wxALL, kPad);
			m_textCode = new wxTextCtrl(this, wxID_ANY, strCode);
			sizerCode->Add(m_textCode, 1, wxALL, kPad);
			mainSizer->Add(sizerCode, 0, wxEXPAND);

			// --- Description ---
			wxBoxSizer* sizerDescription = new wxBoxSizer(wxHORIZONTAL);
			m_staticTextDescription = new wxStaticText(this, wxID_ANY, _("Description"), wxDefaultPosition, wxSize(kLabel, -1));
			sizerDescription->Add(m_staticTextDescription, 0, wxALL, kPad);
			m_textDescription = new wxTextCtrl(this, wxID_ANY, strDescription);
			sizerDescription->Add(m_textDescription, 1, wxALL, kPad);
			mainSizer->Add(sizerDescription, 0, wxEXPAND);

			// --- OK / Cancel ---
			m_sdbSizer = new wxStdDialogButtonSizer();
			m_sdbSizerOK = new wxButton(this, wxID_OK);
			m_sdbSizerOK->Bind(wxEVT_BUTTON, &ibDialogPredefinedItem::OnCommandOK, this);
			m_sdbSizer->AddButton(m_sdbSizerOK);
			m_sdbSizerCancel = new wxButton(this, wxID_CANCEL);
			m_sdbSizerCancel->Bind(wxEVT_BUTTON, &ibDialogPredefinedItem::OnCommandCancel, this);
			m_sdbSizer->AddButton(m_sdbSizerCancel);
			m_sdbSizer->Realize();

			mainSizer->Add(m_sdbSizer, 0, wxEXPAND | wxALL, kPad);

			wxDialog::SetSizer(mainSizer);
			mainSizer->SetSizeHints(this);
			const wxSize minSize = FromDIP(wxSize(380, -1));
			if (GetSize().x < minSize.x)
				SetSize(minSize.x, GetSize().y);

			wxDialog::Layout();
			wxDialog::Centre(wxBOTH);

			wxIcon dlg_icon;
			dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_metaAttributeCLSID));

			wxDialog::SetIcon(dlg_icon);
			wxDialog::SetFocus();
		}

		void SetReadOnly(bool r = true)
		{
			m_checkFolder->Enable(!r);
			m_choiceParent->Enable(!r);

			m_textName->Enable(!r);
			m_textCode->Enable(!r);
			m_textDescription->Enable(!r);

			m_sdbSizerOK->Enable(!r);
		}

	protected:

#include "backend/stringUtils.h"

		void OnCommandOK(wxCommandEvent& event)
		{
			const wxString& strName = m_textName->GetValue();
			const wxString& strCode = m_textCode->GetValue();
			const wxString& strDescription = m_textDescription->GetValue();

			if (strName.IsEmpty() || strCode.IsEmpty() || strDescription.IsEmpty()) {
				wxMessageBox(_("All key fields are not filled in!"));
				return;
			}

			if (stringUtils::CheckCorrectName(strName) >= 0) {
				wxMessageBox(_("The \"name\" field is filled in incorrectly!"));
				return;
			}

			const bool isFolder = m_checkFolder->GetValue();
			const int parentIdx = m_choiceParent->GetSelection();
			const wxObjectDataPtr<ibPredefinedValueObject> parentValue =
				(parentIdx > 0 && parentIdx < static_cast<int>(m_parentOptions.size()))
				? m_parentOptions[parentIdx]
				: wxObjectDataPtr<ibPredefinedValueObject>();

			if (m_itemGuid.isValid()) {

				const wxObjectDataPtr<ibPredefinedValueObject>& predefinedValue =
					GetOwner()->FindPredefinedValue(strName);

				if (predefinedValue != nullptr && predefinedValue->GetPredefinedGuid() != m_itemGuid) {
					wxMessageBox(_("The \"name\" field has not unique!"));
					return;
				}

				GetOwner()->SetPredefinedValue(
					m_itemGuid,
					strName,
					strCode,
					strDescription,
					isFolder,
					parentValue
				);
			}
			else {

				if (GetOwner()->HasPredefinedValue(strName)) {
					wxMessageBox(_("The \"name\" field has not unique!"));
					return;
				}

				GetOwner()->AppendPredefinedValue(
					strName,
					strCode,
					strDescription,
					isFolder,
					parentValue
				);
			}

			event.Skip();
		}

		void OnCommandCancel(wxCommandEvent& event) { event.Skip(); }

	private:

		ibDialogPredefinedEditor* GetOwner() const { return static_cast<ibDialogPredefinedEditor*>(m_parent); }

		ibGuid m_itemGuid;

		wxStaticText* m_staticTextKind;
		wxCheckBox* m_checkFolder;
		wxStaticText* m_staticTextParent;
		wxChoice* m_choiceParent;
		// m_parentOptions[i] matches choice entry i; index 0 is "<none>" = nullptr.
		std::vector<wxObjectDataPtr<ibPredefinedValueObject>> m_parentOptions;

		wxStaticText* m_staticTextName;
		wxTextCtrl* m_textName;
		wxStaticText* m_staticCode;
		wxTextCtrl* m_textCode;
		wxStaticText* m_staticTextDescription;
		wxTextCtrl* m_textDescription;
		wxStdDialogButtonSizer* m_sdbSizer;
		wxButton* m_sdbSizerOK;
		wxButton* m_sdbSizerCancel;
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
	void AppendPredefinedValue(const wxString& strPredefinedName,
		const wxString& strCode, const wxString& strDescription,
		bool valueIsFolder = false,
		const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>()) {
		m_tableModelStore->AppendPredefinedValue(strPredefinedName,
			strCode, strDescription, valueIsFolder, valueParent);
	}

	void SetPredefinedValue(const ibGuid& predefinedGuid, const wxString& strPredefinedName,
		const wxString& strCode, const wxString& strDescription,
		bool valueIsFolder = false,
		const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>()) {
		m_tableModelStore->SetPredefinedValue(predefinedGuid,
			strPredefinedName, strCode, strDescription, valueIsFolder, valueParent);
	}

	void DeletePredefinedValue(const ibGuid& predefinedGuid) { m_tableModelStore->DeletePredefinedValue(predefinedGuid); }

	//find predefined value
	wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const ibGuid& predefinedGuid) const { return m_tableModelStore->FindPredefinedValue(predefinedGuid); }
	wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const wxString& predefinedName) const { return m_tableModelStore->FindPredefinedValue(predefinedName); }

	// Parent picker helper — forwards to the tree store.
	std::vector<wxObjectDataPtr<ibPredefinedValueObject>>
		GetFolderCandidates(const ibGuid& excludeGuid = wxNullGuid) const {
		return m_tableModelStore->GetFolderCandidates(excludeGuid);
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