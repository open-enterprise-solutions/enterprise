#include "docViewInterfaceEdit.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaGroups.h"   // what a group is called, and where it stands
#include "backend/objCtor.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientFrame.h"   // SendPicture — a row's icon

#include <algorithm>
#include <vector>

// ----------------------------------------------------------------------------
// ibInterfaceCompositionView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibInterfaceCompositionView, ibMetaView);

void ibInterfaceCompositionView::OnDraw(ibDataNode& frame)
{
	const ibValueMetaObject* const metaInterface = GetDocument()->GetMetaObject();
	if (metaInterface == nullptr)
		return;

	// Read-only where the section may not be edited — the desktop's editor disabled its tree (FillData: Enable).
	ibDataNode& state = frame.Child(ibProtocolName::State);
	state.SetValue(ibProtocolName::ReadOnly, !metaInterface->IsEnabled());

	FillData(frame.Child(ibProtocolName::Rows));
}

bool ibInterfaceCompositionView::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	ibValueMetaObject* const metaInterface = GetDocument()->GetMetaObject();
	if (metaInterface == nullptr || event != ibProtocolEvent::Toggle)
		return false;

	// The ticked object is the args' NodeId — the node's identity on the wire (jsonProvider reads it into the metaId),
	// not one of its fields.
	ibMetaData* const metaData = metaInterface->GetMetaData();
	ibValueMetaObject* const metaObject = metaData != nullptr
		? metaData->FindAnyObjectByFilter(args.GetMetaId()) : nullptr;
	if (metaObject == nullptr || !metaObject->IsInterfaceAllowed() || !metaObject->IsEditable())
		return false;

	// The desktop's OnCheckItem. Its pass over the open documents (UpdateAllViews, so an open form's navigator re-gathers
	// the section) is not here: a view is drawn anew for every frame a client is sent.
	metaObject->SetInterface(metaInterface->GetMetaID(), args.GetValue<bool>(ibProtocolName::Checked));
	return true;
}

ibDataNode& ibInterfaceCompositionView::GroupFor(ibDataNode& root, const ibClassID& clsid)
{
	// THE ICON FROM THE TYPE REGISTRY, THE CAPTION FROM THE GROUP'S OWN ANSWER (ibMetaGroupCaption) — the same one the
	// configuration tree shows.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor == nullptr)
		return root;

	// A group row is named by its place: it carries no object.
	ibDataNode& group = root.AddChild(0, -static_cast<ibMetaID>(root.Children().size() + 1));
	const wxString caption = ibMetaGroupCaption(clsid);
	group.SetValue(ibProtocolName::Title, caption.IsEmpty() ? typeCtor->GetClassName() : caption);
	group.SetValue(ibProtocolName::Picture, ibClientFrame::SendPicture(clsid));
	return group;
}

ibDataNode& ibInterfaceCompositionView::AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject)
{
	const ibValueMetaObject* const metaInterface = GetDocument()->GetMetaObject();

	// Bare NAME (GetName) — the item already sits UNDER its type group, as on the desktop's.
	ibDataNode& item = parent.AddChild(0, metaObject->GetMetaID());
	item.SetValue(ibProtocolName::Title, metaObject->GetName());
	item.SetValue(ibProtocolName::Picture, ibClientFrame::SendPicture(metaObject->GetClassType()));
	item.SetValue(ibProtocolName::Checked, metaObject->IsSetInterface(metaInterface->GetMetaID()));
	item.SetValue(ibProtocolName::ReadOnly, !metaObject->IsEditable());
	return item;
}

void ibInterfaceCompositionView::FillData(ibDataNode& root)
{
	const ibMetaData* metaData = GetDocument()->GetMetaObject()->GetMetaData();
	wxASSERT(metaData);
	const ibValueMetaObject* commonObject = metaData->GetCommonMetaObject();
	wxASSERT(commonObject);

	root.SetMetaId(commonObject->GetMetaID());
	root.SetValue(ibProtocolName::Title, commonObject->GetName());
	root.SetValue(ibProtocolName::Picture, ibClientFrame::SendPicture(g_metaCommonMetadataCLSID));

	// ASKED, NOT LISTED: every metaobject that says it can be checked into a section (IsInterfaceAllowed), in the order
	// of its group's declared place (ibMetaGroupOrder); a stable sort leaves each group's own objects as the metadata
	// gives them.
	std::vector<ibValueMetaObject*> allowed;
	for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {
		if (object == nullptr || object->IsDeleted())
			continue;
		if (!object->IsInterfaceAllowed())
			continue;
		allowed.push_back(object);
	}
	std::stable_sort(allowed.begin(), allowed.end(),
		[](const ibValueMetaObject* a, const ibValueMetaObject* b) {
			return ibMetaGroupOrder(a->GetClassType()) < ibMetaGroupOrder(b->GetClassType());
		});

	// ⚠ A ROW IS WRITTEN WHOLE BEFORE ITS NEXT SIBLING — a node holds its children by value, so a group's reference does
	// not survive the next group added beside it. So the groups are gathered first, in the order their first objects
	// come (the desktop's made each then), and each is written with its objects before the next.
	std::vector<std::pair<ibClassID, std::vector<ibValueMetaObject*>>> groups;
	for (ibValueMetaObject* object : allowed) {
		const ibClassID clsid = object->GetClassType();
		auto found = std::find_if(groups.begin(), groups.end(),
			[&clsid](const auto& group) { return group.first == clsid; });
		if (found == groups.end())
			found = groups.emplace(groups.end(), clsid, std::vector<ibValueMetaObject*>());
		found->second.push_back(object);
	}

	for (const auto& entry : groups) {
		ibDataNode& group = GroupFor(root, entry.first);
		for (ibValueMetaObject* object : entry.second)
			AppendItem(group, object);
	}
}

// ----------------------------------------------------------------------------
// ibInterfaceCompositionDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibInterfaceCompositionDocument, ibMetaDocument);
