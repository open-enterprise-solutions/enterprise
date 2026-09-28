#ifndef __FUNCTIONAL_OPTION_EDITOR_H__
#define __FUNCTIONAL_OPTION_EDITOR_H__

// THE MEMBERS OF A FUNCTIONAL OPTION — what is not shown while it is off.
//
// Same shape as the common attribute's editor next door (commonAttributeEditor.h): a checkable tree of
// metaobjects, built by ASKING rather than from a written-out list of branches. Every object that answers
// IsFunctionalOptionAllowed() appears under a group named by its own metatype — and, one step further than
// there, the fields and tables inside it that answer the same: a field is hidden on its own, not only with
// its object.
//
// Checking a box writes the membership INTO that object (SetFunctionalOption) — the object keeps the options
// it belongs to, the way it keeps its sections.

#include <wx/treectrl.h>

#include "backend/metadataConfiguration.h"

#include "frontend/win/ctrls/checktree.h"

class ibFunctionalOptionEditor : public wxWindow {

	wxTreeItemId m_treeMETADATA;

	// The option being edited.
	ibValueMetaObject* m_metaOption;

	class ibTreeItemObject : public wxTreeItemData {
		ibValueMetaObject* m_metaObject;
	public:
		ibTreeItemObject(ibValueMetaObject* metaObject) : m_metaObject(metaObject) {}
		ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	};

	ibCheckTree* m_membersCtrl;

	ibValueMetaObject* m_keepSelObj = nullptr;
	wxTreeItemId m_keepSelNode;

protected:

	void OnCheckItem(wxTreeEvent& event);

private:

	// A group per metatype, created on first use and captioned from the type registry —
	// the same source the designer tree reads, so the words match without repeating them.
	wxTreeItemId GroupFor(const ibClassID& clsid);

	wxTreeItemId AppendItem(const wxTreeItemId& parent, ibValueMetaObject* metaObject);

	// The fields and tables inside an object that may belong on their own — at every depth, so a column of a
	// tabular section can be ticked without its table.
	void AppendChildren(const wxTreeItemId& parent, ibValueMetaObject* metaObject);

	void InitMembers();
	void ClearMembers();
	void FillData();

	std::map<ibClassID, wxTreeItemId> m_groups;

public:

	void RefreshMembers() {
		m_keepSelObj = nullptr;
		if (const wxTreeItemId sel = m_membersCtrl->GetSelection(); sel.IsOk())
			if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_membersCtrl->GetItemData(sel)))
				m_keepSelObj = d->GetMetaObject();
		m_keepSelNode = wxTreeItemId();

		// ONE FRAME, not a teardown followed by a re-appear — see commonAttributeEditor.h.
		m_membersCtrl->Freeze();
		m_membersCtrl->SetEvtHandlerEnabled(false);

		ClearMembers();
		FillData();
		if (m_keepSelNode.IsOk())
			m_membersCtrl->SelectItem(m_keepSelNode);

		m_membersCtrl->SetEvtHandlerEnabled(true);
		m_membersCtrl->Thaw();
	}

	void SetReadOnly(bool readOnly = true) {
		m_membersCtrl->Enable(!readOnly);
	}

	ibFunctionalOptionEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		ibValueMetaObject* metaObject = nullptr
	);
};

#endif // !__FUNCTIONAL_OPTION_EDITOR_H__
