#include "commonAttributeEditor.h"

#include "frontend/docView/docView.h"                       // docManager — notify open editors
#include "designer/docManager/templates/docViewMetaFile.h"  // ibMetaDocument

#define ICON_SIZE 16

ibCommonAttributeCompositionEditor::ibCommonAttributeCompositionEditor(wxWindow* parent,
	wxWindowID winid, ibValueMetaObject* metaObject) :
	wxWindow(parent, winid, wxDefaultPosition, wxDefaultSize),
	m_metaCommonAttribute(dynamic_cast<ibValueMetaObjectCommonAttribute*>(metaObject))
{
	m_compositionCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxCR_EMPTY_CHECK | wxTR_TWIST_BUTTONS);
	m_compositionCtrl->SetDoubleBuffered(true);
	m_compositionCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibCommonAttributeCompositionEditor::OnCheckItem, this);

	m_compositionCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	InitComposition();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_compositionCtrl, 1, wxEXPAND);

	m_compositionCtrl->SelectItem(m_treeMETADATA);

	wxWindow::SetSizer(mainSizer);
	wxWindow::Layout();
}

void ibCommonAttributeCompositionEditor::OnCheckItem(wxTreeEvent& event)
{
	ibTreeItemObject* data = dynamic_cast<ibTreeItemObject*>(
		m_compositionCtrl->GetItemData(event.GetItem())
		);

	if (data != nullptr && m_metaCommonAttribute != nullptr) {
		ibValueMetaObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);

		// THE ATTRIBUTE ITSELF MOVES, not a flag: checking creates the copy inside that
		// object, unchecking deletes it.
		m_metaCommonAttribute->SetCompositionObject(metaObject, event.GetExtraLong() != 0);

		// The object that just gained (or lost) an attribute is open somewhere — its
		// attribute list has to re-read. Skip THIS document: its own tree keeps the row
		// that was just checked.
		const ibMetaID editedId = m_metaCommonAttribute->GetMetaID();
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

void ibCommonAttributeCompositionEditor::InitComposition()
{
	m_groups.clear();

	// The root wears the CONFIGURATION's icon, same as the section editor's — it is the
	// same thing being looked at, and a bare root reads as an unfinished tree.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(g_metaCommonMetadataCLSID);
	wxASSERT(typeCtor);

	wxImageList* imageList = m_compositionCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(typeCtor->GetClassIcon());

	m_treeMETADATA = m_compositionCtrl->AddRoot(_("Configuration"), imageIndex, imageIndex);
	m_compositionCtrl->SetItemBold(m_treeMETADATA);
}

void ibCommonAttributeCompositionEditor::ClearComposition()
{
	m_compositionCtrl->DeleteAllItems();
	InitComposition();
}

wxTreeItemId ibCommonAttributeCompositionEditor::GroupFor(const ibClassID& clsid)
{
	auto found = m_groups.find(clsid);
	if (found != m_groups.end())
		return found->second;

	// FROM THE TYPE REGISTRY — the icon and the caption a metatype registered for itself.
	// No table of names here to drift from the designer tree's.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor == nullptr)
		return m_treeMETADATA;

	wxImageList* imageList = m_compositionCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(typeCtor->GetClassIcon());

	const wxTreeItemId group = m_compositionCtrl->AppendItem(
		m_treeMETADATA, typeCtor->GetClassName(), imageIndex, imageIndex, nullptr);

	m_groups.emplace(clsid, group);
	return group;
}

wxTreeItemId ibCommonAttributeCompositionEditor::AppendItem(const wxTreeItemId& parent, ibValueMetaObject* metaObject)
{
	wxImageList* imageList = m_compositionCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(metaObject->GetIcon());

	const wxTreeItemId createItem = m_compositionCtrl->AppendItem(
		parent, metaObject->GetName(), imageIndex, imageIndex, new ibTreeItemObject(metaObject));

	const bool carried = m_metaCommonAttribute != nullptr
		&& m_metaCommonAttribute->IsCompositionObject(metaObject);

	m_compositionCtrl->SetItemState(createItem,
		carried
		? (metaObject->IsEditable() ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED)
		: (metaObject->IsEditable() ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED)
	);

	if (metaObject == m_keepSelObj)
		m_keepSelNode = createItem;

	return createItem;
}

void ibCommonAttributeCompositionEditor::FillData()
{
	if (m_metaCommonAttribute == nullptr)
		return;

	const ibMetaData* metaData = m_metaCommonAttribute->GetMetaData();
	if (metaData == nullptr)
		return;

	// ASKED, NOT LISTED. Every metaobject that says it can carry one appears, under a
	// group named by its own metatype — so this editor never needs to learn about a
	// metatype that arrives later, and never offers one that cannot hold a column.
	for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {
		if (object == nullptr || object->IsDeleted())
			continue;
		if (!object->IsCompositionAllowed())
			continue;

		AppendItem(GroupFor(object->GetClassType()), object);
	}

	m_compositionCtrl->ExpandAll();
}
