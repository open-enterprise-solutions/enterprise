#include "roleEditor.h"

#include "frontend/docView/docView.h"                       // docManager — notify open editors of the change
#include "designer/docManager/templates/docViewMetaFile.h"  // ibMetaDocument — a config metaobject document
#include "backend/metaCollection/metaGroups.h"              // what a group is called, and where it stands

#include <algorithm>
#include <vector>

// (A block of thirteen group names stood here — "Catalogs", "Information registers" and the rest —
// left behind when this editor stopped listing metatypes and started asking them. Nothing read them,
// and the list they suggested was out of date: it ended at accounting registers, which is how a
// reader came to think calculation registers were missing from the editor. A group's caption is now
// the group's own answer, ibMetaGroupCaption.)

#define ICON_SIZE 16

ibRoleEditor::ibRoleEditor(wxWindow* parent,
	wxWindowID winid, ibValueMetaObject* metaObject) :
	wxSplitterWindow(parent, winid, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE), m_metaRole(metaObject)
{
	m_roleCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxTR_TWIST_BUTTONS);
	m_roleCtrl->SetDoubleBuffered(true);
	m_roleCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &ibRoleEditor::OnSelectedItem, this);

	//set image list
	m_roleCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_checkCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);
	m_checkCtrl->SetDoubleBuffered(true);
	m_checkCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibRoleEditor::OnCheckItem, this);

	InitRole();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* treeSizer = new wxBoxSizer(wxHORIZONTAL);
	treeSizer->Add(m_roleCtrl, 2, wxEXPAND);
	treeSizer->Add(m_checkCtrl, 1, wxEXPAND);
	mainSizer->Add(treeSizer, 1, wxEXPAND, 1);

	wxSplitterWindow::SetSashGravity(0.5);
	wxSplitterWindow::SplitVertically(m_roleCtrl, m_checkCtrl,
		250);

	m_roleCtrl->SelectItem(m_treeMETADATA);

	wxSplitterWindow::SetSizer(mainSizer);
	wxSplitterWindow::Layout();
}

ibRoleEditor::~ibRoleEditor() {
	m_roleCtrl->Unbind(wxEVT_TREE_SEL_CHANGED, &ibRoleEditor::OnSelectedItem, this);
	m_checkCtrl->Unbind(wxEVT_CHECKTREE_CHOICE, &ibRoleEditor::OnCheckItem, this);
}

void ibRoleEditor::OnCheckItem(wxTreeEvent& event)
{
	wxTreeItemRoleData* data = dynamic_cast<wxTreeItemRoleData*>(
		m_roleCtrl->GetItemData(event.GetItem())
		);
	if (data != nullptr) {
		ibRole* role = data->GetRole();
		wxASSERT(role);
		ibAccessObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);
		metaObject->SetRight(role, m_metaRole->GetMetaID(), event.GetExtraLong());

		// Access rights changed -> re-render every open editor so read-only state / command greying re-evaluates LIVE
		// against the new right (the view-only matryoshka reads these). SKIP ONLY the role BEING EDITED — matched by
		// its metaID: SetRight flipped a right FLAG on THIS role, its own tree needs no rebuild (that would drop the
		// checked row). Every OTHER open doc updates and preserves its current row on rebuild (RefreshRole/Interface).
		const ibMetaID editedId = m_metaRole->GetMetaID();
		for (auto& doc : docManager->GetDocumentsVector()) {
			ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
			if (metaDoc == nullptr)
				continue;
			const ibValueMetaObject* docMeta = metaDoc->GetMetaObject();
			if (docMeta == nullptr || docMeta->GetMetaID() != editedId)
				metaDoc->UpdateAllViews();
		}
	}

	event.Skip();
}

void ibRoleEditor::OnSelectedItem(wxTreeEvent& event) {
	ibTreeItemObject* data = dynamic_cast<ibTreeItemObject*>(
		m_roleCtrl->GetItemData(event.GetItem())
		);
	m_checkCtrl->Freeze();
	m_checkCtrl->DeleteAllItems();
	if (data != nullptr) {
		ibAccessObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);
		wxTreeItemId root = m_checkCtrl->AddRoot(wxEmptyString);
		for (unsigned int idx = 0; idx < metaObject->GetRoleCount(); idx++) {
			ibRole* role = metaObject->GetRole(idx);
			wxASSERT(role);
			wxTreeItemId newItem = m_checkCtrl->AppendItem(root, role->GetLabel(), wxNOT_FOUND, wxNOT_FOUND, new wxTreeItemRoleData(role));
			m_checkCtrl->SetItemState(newItem, m_metaRole->IsEditable() ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
			m_checkCtrl->Check(newItem, metaObject->AccessRight(role, m_metaRole->GetMetaID()));
		}
	}
	m_checkCtrl->Thaw();
	event.Skip();
}

void ibRoleEditor::AddInterfaceItem(ibValueMetaObject* metaObject, const wxTreeItemId& hParentID)
{
	ibValueMetaObjectSection* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectSection>();
	wxASSERT(metaObject);

	for (auto commonInterface : metaObjectValue->GetInterfaceArrayObject()) {

		if (commonInterface->IsDeleted())
			continue;

		AddInterfaceItem(commonInterface,
			AppendItem(hParentID, commonInterface));
	}
}


#include "frontend/artProvider/artProvider.h"

void ibRoleEditor::InitRole()
{
	m_groups.clear();

	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(g_metaCommonMetadataCLSID);
	wxASSERT(typeCtor);

	wxImageList* imageList = m_roleCtrl->GetImageList();
	int imageIndex = imageList->Add(typeCtor->GetClassIcon());
	m_treeMETADATA = m_roleCtrl->AddRoot(_("Configuration"), imageIndex, imageIndex,
		new ibTreeItemObject(activeMetaData->GetCommonMetaObject()));

	m_roleCtrl->SetItemBold(m_treeMETADATA);
}

void ibRoleEditor::ClearRole() {

	// The groups are whatever FillData created last time, from the metadata — wiping the
	// tree wipes them with it, so there is nothing to enumerate here.
	m_roleCtrl->DeleteAllItems();
	InitRole();
}

wxTreeItemId ibRoleEditor::GroupFor(const ibClassID& clsid)
{
	auto found = m_groups.find(clsid);
	if (found != m_groups.end())
		return found->second;

	// THE ICON FROM THE TYPE REGISTRY, THE CAPTION FROM THE GROUP'S OWN ANSWER (ibMetaGroupCaption)
	// — the same one the configuration tree shows. This editor used to put the metatype's REGISTERED
	// NAME here, so a branch read "CalculationRegister" where the tree read "Calculation registers".
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor == nullptr)
		return m_treeMETADATA;

	wxImageList* imageList = m_roleCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(typeCtor->GetClassIcon());

	const wxString caption = ibMetaGroupCaption(clsid);
	const wxTreeItemId group = m_roleCtrl->AppendItem(m_treeMETADATA,
		caption.IsEmpty() ? typeCtor->GetClassName() : caption, imageIndex, imageIndex, nullptr);

	m_groups.emplace(clsid, group);
	return group;
}

void ibRoleEditor::FillData()
{
	const ibMetaData* metaData = m_metaRole->GetMetaData();
	wxASSERT(metaData);
	const ibValueMetaObject* commonObject = metaData->GetCommonMetaObject();
	wxASSERT(commonObject);

	m_roleCtrl->SetItemText(m_treeMETADATA, commonObject->GetName());

	// ASKED, NOT LISTED — and the question here is the one this editor exists for: does
	// this object HAVE any rights? An object with no rights has nothing to grant or deny,
	// so it has no row; one that declares even a single right appears, under a group named
	// by its own metatype.
	//
	// This replaces a dozen near-identical blocks, one per metatype, each with a
	// pre-created branch of its own. A metatype that declares rights now shows up here on
	// its own — previously it was invisible to the role editor until somebody added a
	// block, which is a silent way to leave part of a configuration unprotected.
	// …AND IN THE ORDER THE CONFIGURATION IS READ IN. A group is made when its first object arrives,
	// so the branches used to stand in whatever order the metadata happened to be walked — catalogs
	// after registers, registers among the charts. Sorted by the group's declared place
	// (ibMetaGroupOrder), the editor reads like the tree; a stable sort keeps each group's own
	// objects in the order the metadata gives them.
	std::vector<ibValueMetaObject*> rightful;
	for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {
		if (object == nullptr || object->IsDeleted())
			continue;
		if (object->GetRoleCount() == 0)
			continue;
		rightful.push_back(object);
	}
	std::stable_sort(rightful.begin(), rightful.end(),
		[](const ibValueMetaObject* a, const ibValueMetaObject* b) {
			return ibMetaGroupOrder(a->GetClassType()) < ibMetaGroupOrder(b->GetClassType());
		});

	for (ibValueMetaObject* object : rightful) {
		const wxTreeItemId item = AppendItem(GroupFor(object->GetClassType()), object);

		// A section nests: sub-sections are rights-bearing in their own right, and they
		// live under their parent rather than beside it.
		if (object->GetClassType() == g_metaSectionCLSID)
			AddInterfaceItem(object, item);
	}

	m_roleCtrl->ExpandAll();
	m_checkCtrl->Enable(m_metaRole->IsEnabled());
}
