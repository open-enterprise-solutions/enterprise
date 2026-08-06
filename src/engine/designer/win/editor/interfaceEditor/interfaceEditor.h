#ifndef __INTERFACE_EDITOR_H__
#define __INTERFACE_EDITOR_H__

#include <wx/treectrl.h>
#include <wx/splitter.h>

#include "backend/metadataConfiguration.h"

#include "frontend/win/theme/luna_toolbarart.h"
#include "frontend/win/ctrls/checktree.h"

class ibInterfaceEditor : public wxWindow {

	wxTreeItemId m_treeMETADATA;

	// One group per metatype, created on demand by GroupFor and captioned from the type
	// registry — the fourteen named branches this class used to declare are gone with the
	// blocks that filled them.
	std::map<ibClassID, wxTreeItemId> m_groups;

	ibValueMetaObject* m_metaInterface;

	class wxTreeItemMetaData : public wxTreeItemData {
		ibInterfaceObject* m_metaObject; // element type
	public:
		wxTreeItemMetaData(ibInterfaceObject* metaObject) : m_metaObject(metaObject) {}
		ibInterfaceObject* GetMetaObject() const { return m_metaObject; }
	};

	ibCheckTree* m_interfaceCtrl;

	ibInterfaceObject* m_keepSelObj = nullptr;   // the metaobject selected before a RefreshInterface — restored after
	wxTreeItemId m_keepSelNode;                  // its node in the rebuilt tree (captured during AppendItem)

protected:

	void OnCheckItem(wxTreeEvent& event);

private:

	// A group per metatype, created on first use.
	wxTreeItemId GroupFor(const ibClassID& clsid);

	wxTreeItemId AppendItem(const wxTreeItemId& parent,
		ibValueMetaObject* metaObject) {
		wxImageList* imageList = m_interfaceCtrl->GetImageList();
		wxASSERT(imageList);
		const int imageIndex = imageList->Add(metaObject->GetIcon());
		// Bare NAME (GetName) — the item already sits UNDER its type group ("Documents" / "Data processors" / …), so the
		// metatype-qualified GetFullName ("Document.Document1") just repeats the group. Mirrors the role editor's tree.
		wxTreeItemId createItem = m_interfaceCtrl->AppendItem(parent, metaObject->GetName(), imageIndex, imageIndex, new wxTreeItemMetaData(metaObject));
		m_interfaceCtrl->SetItemState(createItem,
			metaObject->IsSetInterface(m_metaInterface->GetMetaID()) ?
			metaObject->IsEditable() ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED :
			metaObject->IsEditable() ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED
		);
		if (metaObject == m_keepSelObj)   // the row that was selected before this rebuild — remember it for restore
			m_keepSelNode = createItem;

		return createItem;
	}

	void InitInterface();
	void ClearInterface();

	void FillData();

public:

	void RefreshInterface() {
		// Preserve the current row across the full rebuild — capture the selected metaobject by identity, re-select it
		// after refilling. A section edited elsewhere skips its OWN doc (OnCheckItem); only OTHER open section editors
		// rebuild here, and each keeps its row (the check state itself is re-read from the metaobject either way).
		m_keepSelObj = nullptr;
		if (const wxTreeItemId sel = m_interfaceCtrl->GetSelection(); sel.IsOk())
			if (const wxTreeItemMetaData* d = dynamic_cast<wxTreeItemMetaData*>(m_interfaceCtrl->GetItemData(sel)))
				m_keepSelObj = d->GetMetaObject();
		m_keepSelNode = wxTreeItemId();

		// ONE FRAME — same reason as the form's attribute tree: the rebuild tears the tree
		// down and refills it, and un-frozen that is visible as a collapse followed by a
		// re-appear. Events are muted for the churn (DeleteAllItems / SelectItem raise
		// native selection and focus events nobody wants mid-rebuild).
		m_interfaceCtrl->Freeze();
		m_interfaceCtrl->SetEvtHandlerEnabled(false);

		ClearInterface();
		FillData();
		if (m_keepSelNode.IsOk())
			m_interfaceCtrl->SelectItem(m_keepSelNode);       // a metaobject row — restored by identity

		m_interfaceCtrl->SetEvtHandlerEnabled(true);
		m_interfaceCtrl->Thaw();
	}

	void SetReadOnly(bool readOnly = true) {
		m_interfaceCtrl->Enable(!readOnly);
	}

	ibInterfaceEditor(wxWindow* parent,
		wxWindowID winid = wxID_ANY,
		ibValueMetaObject* metaObject = nullptr
	);
};

#endif 