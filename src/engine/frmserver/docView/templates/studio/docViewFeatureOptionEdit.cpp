#include "docViewFeatureOptionEdit.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaGroups.h"   // what a group is called, and where it stands
#include "backend/objCtor.h"
#include "core/serialize/dataBuilder.h"
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — a row's icon
#include "protocol/protocolStudio.h"   // ibProtocol::ibView::ibCheckTree

#include <algorithm>
#include <vector>

// ----------------------------------------------------------------------------
// ibFeatureOptionMembersView implementation
// ----------------------------------------------------------------------------

void ibFeatureOptionMembersView::OnDraw(ibDataNode& frame)
{
	const ibValueMetaObject* const metaOption = GetDocument()->GetMetaObject();
	if (metaOption == nullptr)
		return;

	// Read-only where the application may not be edited — the desktop's editor disabled its tree (SetReadOnly by
	// ibDOC_READONLY).
	ibDataNode& state = frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibView::ibCheckTree::ReadOnly, !metaOption->IsEditable());

	FillData(frame.Child(ibProtocol::ibView::ibCheckTree::Rows));
}

bool ibFeatureOptionMembersView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	ibValueMetaObject* const metaOption = GetDocument()->GetMetaObject();
	if (metaOption == nullptr || event != ibProtocol::ibEvent::ibKind::Toggle)
		return false;

	// A member at any depth — a field of a tabular section is ticked on its own.
	ibMetaData* const metaData = metaOption->GetMetaData();
	ibValueMetaObject* const metaObject = metaData != nullptr
		? metaData->FindAnyObjectByFilter(args.GetMetaId(), true) : nullptr;   // NodeId: the node's identity, not a field
	if (metaObject == nullptr || !metaObject->IsFeatureOptionAllowed() || !metaObject->IsEditable())
		return false;

	// Written into the member — it keeps the options it belongs to (the desktop's OnCheckItem). Nothing else in the
	// studio changes with it, so no other open editor has anything to re-read.
	metaObject->SetFeatureOption(metaOption->GetMetaID(), args.GetValue<bool>(ibProtocol::ibView::ibCheckTree::Checked));
	return true;
}

ibDataNode& ibFeatureOptionMembersView::GroupFor(ibDataNode& root, const ibClassID& clsid)
{
	// THE ICON FROM THE TYPE REGISTRY, THE CAPTION FROM THE GROUP'S OWN ANSWER (ibMetaGroupCaption) — the one the
	// application tree, the role editor and the section editor show.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor == nullptr)
		return root;

	// A group row is named by its place: it carries no object.
	ibDataNode& group = root.AddChild(0, -static_cast<ibMetaID>(root.Children().size() + 1));
	const ibString caption = ibMetaGroupCaption(clsid);
	group.SetValue(ibProtocol::ibView::ibCheckTree::Title, caption.IsEmpty() ? typeCtor->GetClassName() : caption);
	group.SetValue(ibProtocol::ibView::ibCheckTree::Picture, ibBackendPicture::SendPicture(clsid));
	return group;
}

ibDataNode& ibFeatureOptionMembersView::AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject)
{
	const ibValueMetaObject* const metaOption = GetDocument()->GetMetaObject();

	ibDataNode& item = parent.AddChild(0, metaObject->GetMetaID());
	item.SetValue(ibProtocol::ibView::ibCheckTree::Title, metaObject->GetName());
	item.SetValue(ibProtocol::ibView::ibCheckTree::Picture, ibBackendPicture::SendPicture(metaObject->GetClassType()));
	item.SetValue(ibProtocol::ibView::ibCheckTree::Checked, metaObject->IsInFeatureOption(metaOption->GetMetaID()));
	item.SetValue(ibProtocol::ibView::ibCheckTree::ReadOnly, !metaObject->IsEditable());
	return item;
}

void ibFeatureOptionMembersView::AppendChildren(ibDataNode& parent, ibValueMetaObject* metaObject)
{
	// Each child written whole — its own children with it — before the next is added beside it.
	for (ibValueMetaObject* child : metaObject->GetAnyArrayObject()) {
		if (child == nullptr || child->IsDeleted() || !child->IsFeatureOptionAllowed())
			continue;

		AppendChildren(AppendItem(parent, child), child);
	}
}

void ibFeatureOptionMembersView::FillData(ibDataNode& root)
{
	const ibMetaData* metaData = GetDocument()->GetMetaObject()->GetMetaData();
	if (metaData == nullptr)
		return;
	const ibValueMetaObject* rootObject = metaData->GetRootMetaObject();
	ibASSERT(rootObject);

	// The root wears the APPLICATION's icon, as the section and shared attribute editors'; the desktop's kept the
	// word "Application" on it.
	root.SetMetaId(rootObject->GetMetaID());
	root.SetValue(ibProtocol::ibView::ibCheckTree::Title, ibString(_("Application")));
	root.SetValue(ibProtocol::ibView::ibCheckTree::Picture, ibBackendPicture::SendPicture(g_metaApplicationMetadataCLSID));

	// ASKED, NOT LISTED: every object that says it may belong (IsFeatureOptionAllowed), in the order of its group's
	// declared place (ibMetaGroupOrder); a stable sort leaves each group's own objects as the metadata gives them.
	std::vector<ibValueMetaObject*> allowed;
	for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {
		if (object == nullptr || object->IsDeleted() || !object->IsFeatureOptionAllowed())
			continue;
		allowed.push_back(object);
	}
	std::stable_sort(allowed.begin(), allowed.end(),
		[](const ibValueMetaObject* a, const ibValueMetaObject* b) {
			return ibMetaGroupOrder(a->GetClassType()) < ibMetaGroupOrder(b->GetClassType());
		});

	// ⚠ A ROW IS WRITTEN WHOLE BEFORE ITS NEXT SIBLING — see the section editor's (docViewInterfaceEdit.cpp): the groups
	// gathered first, in the order their first objects come, each written with its objects before the next.
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
			AppendChildren(AppendItem(group, object), object);
	}
}

// ----------------------------------------------------------------------------
// ibFeatureOptionMembersDocument implementation
// ----------------------------------------------------------------------------
