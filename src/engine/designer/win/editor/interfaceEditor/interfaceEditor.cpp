#include "interfaceEditor.h"

#include "frontend/docView/docView.h"                       // docManager — notify open editors of the change
#include "designer/docManager/templates/docViewMetaFile.h"  // ibMetaDocument — a config metaobject document

#define ICON_SIZE 16

ibInterfaceEditor::ibInterfaceEditor(wxWindow* parent,
	wxWindowID winid, ibValueMetaObject* metaObject) :
	wxWindow(parent, winid, wxDefaultPosition, wxDefaultSize), m_metaInterface(metaObject)
{
	m_interfaceCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxCR_EMPTY_CHECK | wxTR_TWIST_BUTTONS);
	m_interfaceCtrl->SetDoubleBuffered(true);
	m_interfaceCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibInterfaceEditor::OnCheckItem, this);

	//set image list
	m_interfaceCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	InitInterface();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_interfaceCtrl, 1, wxEXPAND);

	m_interfaceCtrl->SelectItem(m_treeMETADATA);

	wxWindow::SetSizer(mainSizer);
	wxWindow::Layout();
}

void ibInterfaceEditor::OnCheckItem(wxTreeEvent& event)
{
	wxTreeItemMetaData* data = dynamic_cast<wxTreeItemMetaData*>(
		m_interfaceCtrl->GetItemData(event.GetItem())
		);
	if (data != nullptr) {
		ibInterfaceObject* metaObject = data->GetMetaObject();
		wxASSERT(metaObject);
		metaObject->SetInterface(m_metaInterface->GetMetaID(), event.GetExtraLong());

		// Section composition changed -> re-render every open form so its command navigator re-gathers the section's
		// content LIVE (a just-included / excluded item shows or hides without reopening the form). SKIP ONLY the
		// section BEING EDITED — matched by its metaID: SetInterface flipped a membership FLAG, THIS section's tree
		// needs no rebuild (that would drop the checked row). Every OTHER open doc updates and preserves its own row.
		const ibMetaID editedId = m_metaInterface->GetMetaID();
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


#include "frontend/artProvider/artProvider.h"

void ibInterfaceEditor::InitInterface()
{
	m_groups.clear();

	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(g_metaCommonMetadataCLSID);
	wxASSERT(typeCtor);

	wxImageList* imageList = m_interfaceCtrl->GetImageList();
	int imageIndex = imageList->Add(typeCtor->GetClassIcon());
	m_treeMETADATA = m_interfaceCtrl->AddRoot(_("Configuration"), imageIndex, imageIndex, new wxTreeItemMetaData(activeMetaData->GetCommonMetaObject()));

	m_interfaceCtrl->SetItemBold(m_treeMETADATA);
}

void ibInterfaceEditor::ClearInterface() {

	// Nothing to enumerate: the groups are whatever FillData created last time, and it
	// creates them from the metadata. Wiping the tree wipes them with it.
	m_interfaceCtrl->DeleteAllItems();
	InitInterface();
}

wxTreeItemId ibInterfaceEditor::GroupFor(const ibClassID& clsid)
{
	auto found = m_groups.find(clsid);
	if (found != m_groups.end())
		return found->second;

	// FROM THE TYPE REGISTRY — the icon and the caption a metatype registered for itself.
	// There is no table of names here to fall out of step with the designer tree's.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor == nullptr)
		return m_treeMETADATA;

	wxImageList* imageList = m_interfaceCtrl->GetImageList();
	wxASSERT(imageList);
	const int imageIndex = imageList->Add(typeCtor->GetClassIcon());

	const wxTreeItemId group = m_interfaceCtrl->AppendItem(
		m_treeMETADATA, typeCtor->GetClassName(), imageIndex, imageIndex, nullptr);

	m_groups.emplace(clsid, group);
	return group;
}

void ibInterfaceEditor::FillData()
{
	const ibMetaData* metaData = m_metaInterface->GetMetaData();
	wxASSERT(metaData);
	const ibValueMetaObject* commonObject = metaData->GetCommonMetaObject();
	wxASSERT(commonObject);

	m_interfaceCtrl->SetItemText(m_treeMETADATA, commonObject->GetName());

	// ASKED, NOT LISTED. Every metaobject that says it can be checked into a section
	// appears, under a group named by its own metatype.
	//
	// This used to be a dozen near-identical blocks — one per metatype, each with its own
	// pre-created branch, its own caption and its own loop — and a metatype added later was
	// simply absent from the section editor until somebody noticed. Now a new kind answers
	// IsInterfaceAllowed() for itself and shows up; one that should not be there says no
	// and never appears.
	for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {
		if (object == nullptr || object->IsDeleted())
			continue;
		if (!object->IsInterfaceAllowed())
			continue;

		AppendItem(GroupFor(object->GetClassType()), object);
	}

	m_interfaceCtrl->ExpandAll();
	m_interfaceCtrl->Enable(m_metaInterface->IsEnabled());
}
