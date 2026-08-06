#ifndef __COMMON_ATTRIBUTE_EDITOR_H__
#define __COMMON_ATTRIBUTE_EDITOR_H__

// THE COMPOSITION OF A COMMON ATTRIBUTE — which objects carry it.
//
// Same shape as the section editor next door (a checkable tree of metaobjects), with
// one difference that is the point of the file: the tree is NOT a written-out list of
// branches. The section editor names every group by hand — Catalogs, Documents,
// Reports, Information registers, … — so a metatype added later is invisible there
// until somebody remembers to add a line.
//
// Here the tree is built by ASKING: every metaobject that answers
// IsCompositionAllowed() appears, grouped under its own type, and the group's
// caption comes from the type registry. A metatype that starts carrying common
// attributes shows up on its own; one that stops, disappears. Nothing to keep in step.
//
// Checking a box is not a note about intent — it puts a real attribute inside that
// object (and unchecking removes it), which is why this calls SetComposition rather
// than flipping a flag itself.

#include <wx/treectrl.h>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaCommonAttributeObject.h"

#include "frontend/win/ctrls/checktree.h"

class ibCommonAttributeCompositionEditor : public wxWindow {

	wxTreeItemId m_treeMETADATA;

	// The declaration being edited.
	ibValueMetaObjectCommonAttribute* m_metaCommonAttribute;

	class wxTreeItemMetaData : public wxTreeItemData {
		ibValueMetaObject* m_metaObject;
	public:
		wxTreeItemMetaData(ibValueMetaObject* metaObject) : m_metaObject(metaObject) {}
		ibValueMetaObject* GetMetaObject() const { return m_metaObject; }
	};

	ibCheckTree* m_compositionCtrl;

	ibValueMetaObject* m_keepSelObj = nullptr;
	wxTreeItemId m_keepSelNode;

protected:

	void OnCheckItem(wxTreeEvent& event);

private:

	// A group per metatype, created on first use and captioned from the type registry —
	// the same source the designer tree reads, so the words match without repeating them.
	wxTreeItemId GroupFor(const ibClassID& clsid);

	wxTreeItemId AppendItem(const wxTreeItemId& parent, ibValueMetaObject* metaObject);

	void InitComposition();
	void ClearComposition();
	void FillData();

	std::map<ibClassID, wxTreeItemId> m_groups;

public:

	void RefreshComposition() {
		m_keepSelObj = nullptr;
		if (const wxTreeItemId sel = m_compositionCtrl->GetSelection(); sel.IsOk())
			if (const wxTreeItemMetaData* d = dynamic_cast<wxTreeItemMetaData*>(m_compositionCtrl->GetItemData(sel)))
				m_keepSelObj = d->GetMetaObject();
		m_keepSelNode = wxTreeItemId();

		// ONE FRAME, not a teardown followed by a re-appear. The rebuild empties the tree
		// and refills it, and without this the user watches it collapse and grow back —
		// the same pattern the form's attribute tree uses (visualEditorAttributeTree.cpp).
		// The event handler goes with it: DeleteAllItems and SelectItem raise native
		// selection and focus events, and nothing downstream wants them mid-churn.
		m_compositionCtrl->Freeze();
		m_compositionCtrl->SetEvtHandlerEnabled(false);

		ClearComposition();
		FillData();
		if (m_keepSelNode.IsOk())
			m_compositionCtrl->SelectItem(m_keepSelNode);

		m_compositionCtrl->SetEvtHandlerEnabled(true);
		m_compositionCtrl->Thaw();
	}

	void SetReadOnly(bool readOnly = true) {
		m_compositionCtrl->Enable(!readOnly);
	}

	ibCommonAttributeCompositionEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		ibValueMetaObject* metaObject = nullptr
	);
};

#endif // !__COMMON_ATTRIBUTE_EDITOR_H__
