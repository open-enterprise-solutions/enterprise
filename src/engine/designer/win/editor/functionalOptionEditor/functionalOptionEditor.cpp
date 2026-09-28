#include "functionalOptionEditor.h"

#include "backend/metaCollection/metaGroups.h"   // what a group is called, and where it stands

#include <algorithm>
#include <vector>

#define ICON_SIZE 16

ibFunctionalOptionEditor::ibFunctionalOptionEditor(wxWindow* parent,
	wxWindowID winid, ibValueMetaObject* metaObject) :
	wxWindow(parent, winid, wxDefaultPosition, wxDefaultSize),
	m_metaOption(metaObject)
{
	m_membersCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxCR_EMPTY_CHECK | wxTR_TWIST_BUTTONS);
	m_membersCtrl->SetDoubleBuffered(true);
	m_membersCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibFunctionalOptionEditor::OnCheckItem, this);

	m_membersCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	InitMembers();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_membersCtrl, 1, wxEXPAND);

	m_membersCtrl->SelectItem(m_treeMETADATA);

	wxWindow::SetSizer(mainSizer);
	wxWindow::Layout();
}

void ibFunctionalOptionEditor::OnCheckItem(wxTreeEvent& event)
{
	ibTreeItemObject* data = dynamic_cast<ibTreeItemObject*>(
		m_membersCtrl->GetItemData(event.GetItem())
		);

	if (data != nullptr && m_metaOption != nullptr) {
		ibValueMetaObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);

		// Written into the member — it keeps the options it belongs to. Nothing else in the designer changes
		// with it (the designer shows everything), so no other open editor has anything to re-read.
		metaObject->SetFunctionalOption(m_metaOption->GetMetaID(), event.GetExtraLong() != 0);
	}

	event.Skip();
}

void ibFunctionalOptionEditor::InitMembers()
{
	m_groups.clear();

	// The root wears the CONFIGURATION's icon, same as the section and common attribute editors'.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(g_metaCommonMetadataCLSID);
	wxASSERT(typeCtor);

	wxImageList* imageList = m_membersCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(typeCtor->GetClassIcon());

	m_treeMETADATA = m_membersCtrl->AddRoot(_("Configuration"), imageIndex, imageIndex);
	m_membersCtrl->SetItemBold(m_treeMETADATA);
}

void ibFunctionalOptionEditor::ClearMembers()
{
	m_membersCtrl->DeleteAllItems();
	InitMembers();
}

wxTreeItemId ibFunctionalOptionEditor::GroupFor(const ibClassID& clsid)
{
	auto found = m_groups.find(clsid);
	if (found != m_groups.end())
		return found->second;

	// THE ICON FROM THE TYPE REGISTRY, THE CAPTION FROM THE GROUP'S OWN ANSWER (ibMetaGroupCaption) — the
	// one the configuration tree, the role editor and the section editor show.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor == nullptr)
		return m_treeMETADATA;

	wxImageList* imageList = m_membersCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(typeCtor->GetClassIcon());

	const wxString caption = ibMetaGroupCaption(clsid);
	const wxTreeItemId group = m_membersCtrl->AppendItem(m_treeMETADATA,
		caption.IsEmpty() ? typeCtor->GetClassName() : caption, imageIndex, imageIndex, nullptr);

	m_groups.emplace(clsid, group);
	return group;
}

wxTreeItemId ibFunctionalOptionEditor::AppendItem(const wxTreeItemId& parent, ibValueMetaObject* metaObject)
{
	wxImageList* imageList = m_membersCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(metaObject->GetIcon());

	const wxTreeItemId createItem = m_membersCtrl->AppendItem(
		parent, metaObject->GetName(), imageIndex, imageIndex, new ibTreeItemObject(metaObject));

	const bool member = m_metaOption != nullptr
		&& metaObject->IsInFunctionalOption(m_metaOption->GetMetaID());

	m_membersCtrl->SetItemState(createItem,
		member
		? (metaObject->IsEditable() ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED)
		: (metaObject->IsEditable() ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED)
	);

	if (metaObject == m_keepSelObj)
		m_keepSelNode = createItem;

	return createItem;
}

void ibFunctionalOptionEditor::AppendChildren(const wxTreeItemId& parent, ibValueMetaObject* metaObject)
{
	for (ibValueMetaObject* child : metaObject->GetAnyArrayObject()) {
		if (child == nullptr || child->IsDeleted() || !child->IsFunctionalOptionAllowed())
			continue;

		AppendChildren(AppendItem(parent, child), child);
	}
}

void ibFunctionalOptionEditor::FillData()
{
	if (m_metaOption == nullptr)
		return;

	const ibMetaData* metaData = m_metaOption->GetMetaData();
	if (metaData == nullptr)
		return;

	// ASKED, NOT LISTED. Every object that says it may belong appears under a group named by its own
	// metatype, with the fields and tables inside it that say the same.
	// …AND IN THE ORDER THE CONFIGURATION IS READ IN — sorted by the group's declared place
	// (ibMetaGroupOrder), as the role and section editors do; a stable sort leaves each group's own
	// objects as the metadata gives them.
	std::vector<ibValueMetaObject*> allowed;
	for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {
		if (object == nullptr || object->IsDeleted() || !object->IsFunctionalOptionAllowed())
			continue;
		allowed.push_back(object);
	}
	std::stable_sort(allowed.begin(), allowed.end(),
		[](const ibValueMetaObject* a, const ibValueMetaObject* b) {
			return ibMetaGroupOrder(a->GetClassType()) < ibMetaGroupOrder(b->GetClassType());
		});

	for (ibValueMetaObject* object : allowed)
		AppendChildren(AppendItem(GroupFor(object->GetClassType()), object), object);

	// The groups open, the objects closed: every field of every object at once would bury the objects.
	m_membersCtrl->Expand(m_treeMETADATA);
	for (const auto& group : m_groups)
		m_membersCtrl->Expand(group.second);
}
