#include "docViewRoleEdit.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaGroups.h"   // what a group is called, and where it stands
#include "backend/metaCollection/metaSectionObject.h"   // a section's sub-sections
#include "backend/objCtor.h"
#include "core/serialize/dataBuilder.h"
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — a row's icon

#include <algorithm>
#include <vector>

// ----------------------------------------------------------------------------
// ibRoleRightsView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibRoleRightsView, ibMetaView);

void ibRoleRightsView::OnDraw(ibDataNode& frame)
{
	const ibValueMetaObject* const metaRole = GetDocument()->GetMetaObject();
	if (metaRole == nullptr)
		return;

	// Read-only where the role may not be edited — the desktop's editor disabled its check tree (FillData: Enable).
	ibDataNode& state = frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibView::ibCheckTree::ReadOnly, !metaRole->IsEnabled());

	FillData(frame.Child(ibProtocol::ibView::ibCheckTree::Rows));
}

bool ibRoleRightsView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	ibValueMetaObject* const metaRole = GetDocument()->GetMetaObject();
	if (metaRole == nullptr || event != ibProtocol::ibEvent::ibKind::Toggle || !metaRole->IsEditable())
		return false;

	// The object at any depth — a sub-section has rights of its own; the configuration's row is its common object.
	ibMetaData* const metaData = metaRole->GetMetaData();
	ibValueMetaObject* const metaObject = metaData != nullptr
		? metaData->FindAnyObjectByFilter(args.GetMetaId(), true) : nullptr;   // NodeId: the node's identity, not a field
	const ibRole* const role = metaObject != nullptr
		? metaObject->GetRole(args.GetValue<wxString>(ibProtocol::ibView::ibCheckTree::Name)) : nullptr;
	if (role == nullptr)
		return false;

	// The desktop's OnCheckItem. Its pass over the open documents (UpdateAllViews, so read-only state and command
	// greying re-evaluate against the new right) is not here: a view is drawn anew for every frame a client is sent.
	metaObject->SetRight(role, metaRole->GetMetaID(), args.GetValue<bool>(ibProtocol::ibView::ibCheckTree::Checked));
	return true;
}

ibDataNode& ibRoleRightsView::GroupFor(ibDataNode& root, const ibClassID& clsid)
{
	// THE ICON FROM THE TYPE REGISTRY, THE CAPTION FROM THE GROUP'S OWN ANSWER (ibMetaGroupCaption) — the same one the
	// configuration tree shows.
	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor == nullptr)
		return root;

	// A group row is named by its place: it carries no object.
	ibDataNode& group = root.AddChild(0, -static_cast<ibMetaID>(root.Children().size() + 1));
	const wxString caption = ibMetaGroupCaption(clsid);
	group.SetValue(ibProtocol::ibView::ibCheckTree::Title, caption.IsEmpty() ? typeCtor->GetClassName() : caption);
	group.SetValue(ibProtocol::ibView::ibCheckTree::Picture, ibBackendPicture::SendPicture(clsid));
	return group;
}

ibDataNode& ibRoleRightsView::AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject)
{
	ibDataNode& item = parent.AddChild(0, metaObject->GetMetaID());
	item.SetValue(ibProtocol::ibView::ibCheckTree::Title, metaObject->GetName());
	item.SetValue(ibProtocol::ibView::ibCheckTree::Picture, ibBackendPicture::SendPicture(metaObject->GetClassType()));
	AppendRoles(item, metaObject);
	return item;
}

void ibRoleRightsView::AppendRoles(ibDataNode& item, const ibValueMetaObject* metaObject)
{
	const ibValueMetaObject* const metaRole = GetDocument()->GetMetaObject();

	// Each right greyed where the role may not be edited, as the desktop's check tree had them (UNCHECKED_DISABLED).
	ibDataNode& roles = item.Child(ibProtocol::ibView::ibCheckTree::Roles);
	for (unsigned int idx = 0; idx < metaObject->GetRoleCount(); idx++) {
		const ibRole* role = metaObject->GetRole(idx);
		wxASSERT(role);
		ibDataNode& right = roles.AddChild(0, 0);
		right.SetValue(ibProtocol::ibView::ibCheckTree::Name, role->GetName());
		right.SetValue(ibProtocol::ibView::ibCheckTree::Title, role->GetLabel());
		right.SetValue(ibProtocol::ibView::ibCheckTree::Checked, metaObject->AccessRight(role, metaRole->GetMetaID()));
		right.SetValue(ibProtocol::ibView::ibCheckTree::ReadOnly, !metaRole->IsEditable());
	}
}

void ibRoleRightsView::AddInterfaceItem(ibValueMetaObject* metaObject, ibDataNode& parent)
{
	const ibValueMetaObjectSection* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectSection>();
	wxASSERT(metaObjectValue);

	// Each sub-section written whole — its own sub-sections with it — before the next is added beside it.
	for (auto commonInterface : metaObjectValue->GetInterfaceArrayObject()) {

		if (commonInterface->IsDeleted())
			continue;

		AddInterfaceItem(commonInterface,
			AppendItem(parent, commonInterface));
	}
}

void ibRoleRightsView::FillData(ibDataNode& root)
{
	const ibMetaData* metaData = GetDocument()->GetMetaObject()->GetMetaData();
	wxASSERT(metaData);
	const ibValueMetaObject* commonObject = metaData->GetCommonMetaObject();
	wxASSERT(commonObject);

	// The configuration's row is its common object's — its rights are the configuration's own.
	root.SetMetaId(commonObject->GetMetaID());
	root.SetValue(ibProtocol::ibView::ibCheckTree::Title, commonObject->GetName());
	root.SetValue(ibProtocol::ibView::ibCheckTree::Picture, ibBackendPicture::SendPicture(g_metaCommonMetadataCLSID));
	AppendRoles(root, commonObject);

	// ASKED, NOT LISTED — does this object HAVE any rights? One with none has nothing to grant or deny, so it has no row
	// (GetRoleCount); in the order of its group's declared place (ibMetaGroupOrder), a stable sort leaving each group's
	// own objects as the metadata gives them.
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

	// ⚠ A ROW IS WRITTEN WHOLE BEFORE ITS NEXT SIBLING — see the section editor's (docViewInterfaceEdit.cpp): the groups
	// gathered first, in the order their first objects come, each written with its objects before the next.
	std::vector<std::pair<ibClassID, std::vector<ibValueMetaObject*>>> groups;
	for (ibValueMetaObject* object : rightful) {
		const ibClassID clsid = object->GetClassType();
		auto found = std::find_if(groups.begin(), groups.end(),
			[&clsid](const auto& group) { return group.first == clsid; });
		if (found == groups.end())
			found = groups.emplace(groups.end(), clsid, std::vector<ibValueMetaObject*>());
		found->second.push_back(object);
	}

	for (const auto& entry : groups) {
		ibDataNode& group = GroupFor(root, entry.first);
		for (ibValueMetaObject* object : entry.second) {
			ibDataNode& item = AppendItem(group, object);

			// A section nests: sub-sections are rights-bearing in their own right, and they live under their parent
			// rather than beside it.
			if (object->GetClassType() == g_metaSectionCLSID)
				AddInterfaceItem(object, item);
		}
	}
}

// ----------------------------------------------------------------------------
// ibRoleRightsDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibRoleRightsDocument, ibMetaDocument);
