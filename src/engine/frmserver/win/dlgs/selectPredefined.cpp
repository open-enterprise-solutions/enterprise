////////////////////////////////////////////////////////////////////////////
//	Description : the designer's choice of a DECLARED value — see selectPredefined.h
////////////////////////////////////////////////////////////////////////////

#include "selectPredefined.h"

#include <algorithm>
#include <set>
#include <vector>

#include "backend/backend_mainFrame.h"                                   // ibBackendDocFrame::Request — the client's window
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"          // a reference built on a declared guid
#include "backend/metaCollection/partial/declaredPresentation.h"         // `CatalogRef.Goods` — written once
#include "backend/session/session.h"
#include "backend/system/value/composition/valueComposerField.h"         // the technical type the references are chosen by
#include "core/serialize/dataBuilder.h"
#include "frmserver/visualView/ctrl/frame.h"                             // ibControlFrame::ChoiceProcessing
#include "frmserver/visualView/ctrl/typeControl.h"                       // ShowSelectType — the type list already in the product
#include "protocol/protocol.h"

// The window's own parts — nothing outside this file has business with a branch or a value's number.
namespace {

// The predefined value lives INSIDE the hierarchy metaobject — named once here rather than spelled
// out at every use.
using ibPredefinedValue = ibValueMetaObjectRecordDataHierarchyMutableRef::ibPredefinedValueObject;

// THE ANSWER IS A NUMBER: which type, and which value of it, kept here under the number the client's tree names it by.
// A null guid IS the empty reference of that type, so every answer is the same kind and the window has one road out.
struct ibDesignerRefItem {
	const ibValueMetaObjectRecordDataRef* m_ref;
	ibGuid m_guid;
	ibString m_name;
};

// One value under `at`, numbered; what it holds goes under it.
ibDataNode& ibAppendValue(ibDataNode& at, std::vector<ibDesignerRefItem>& values,
	const ibValueMetaObjectRecordDataRef* recordRef, const ibGuid& guid, const ibString& name)
{
	ibDataNode& value = at.AddChild(0, 0);
	value.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Name, name);
	value.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Id, static_cast<std::int32_t>(values.size()));
	values.push_back(ibDesignerRefItem{ recordRef, guid, name });
	return value;
}

// WHAT ONE TYPE OFFERS: its empty reference, then everything it declares, nested as it is declared.
void ibFillOneTypeBranch(ibDataNode& branch, std::vector<ibDesignerRefItem>& values,
	const ibValueMetaObjectRecordDataRef* recordRef)
{
	if (recordRef == nullptr)
		return;

	// ⭐ EVERY REFERENCE TYPE HAS THIS ONE, not only the kinds that declare predefined values: a
	// DOCUMENT declares none and its empty reference is still a legitimate thing to say (Max,
	// 2026-08-28: "documents open too — a document just has only the reference").
	ibAppendValue(branch, values, recordRef, wxNullGuid, ibT("EmptyRef"));

	// ⭐ AN ENUMERATION IS A REFERENCE LIKE ANY OTHER — "all references — enumerations, documents,
	// catalogs — go through this form" (Max, 2026-08-28). Its members are DECLARED, which is exactly
	// the shape this window is for: no data is needed to name them.
	if (const auto* enumeration = dynamic_cast<const ibValueMetaObjectRecordDataEnumRef*>(recordRef)) {
		for (const ibValueMetaObjectEnum* member : enumeration->GetEnumObjectArray())
			if (member != nullptr)
				ibAppendValue(branch, values, recordRef, member->GetGuid(), member->GetName());
	}

	if (const auto* hierarchy =
			dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(recordRef)) {

		// ⭐ FOLDERS NEST, AND SO DO THEIR CHILDREN — a predefined item names its parent and that
		// parent may itself sit under another folder ("there can be folders, and under a folder
		// items of a subordinate folder — all of it selectable"). The array is FLAT and says nothing
		// about who comes first, so each folder is written with what lies under it, from the top: a node
		// of the question is not a place to come back to — the next one appended beside it may move it.
		std::vector<const ibPredefinedValue*> items;
		for (const auto& item : hierarchy->GetPredefinedValueArray())
			if (item)
				items.push_back(item.get());
		const std::set<const ibPredefinedValue*> declared(items.begin(), items.end());

		// A parent that is not itself predefined leaves its children homeless — they still belong to
		// the type, so they stand at the top rather than disappearing.
		const auto folderOf = [&declared](const ibPredefinedValue* item) -> const ibPredefinedValue* {
			const ibPredefinedValue* under = item->GetPredefinedParent().get();
			return declared.count(under) != 0 ? under : nullptr;
		};
		const auto writeUnder = [&](auto&& self, ibDataNode& at, const ibPredefinedValue* folder) -> void {
			for (const ibPredefinedValue* item : items) {
				if (folderOf(item) != folder)
					continue;
				ibDataNode& value = ibAppendValue(at, values, recordRef, item->GetPredefinedGuid(), item->GetPredefinedName());
				self(self, value, item);
			}
		};
		writeUnder(writeUnder, branch, nullptr);
	}
}

// EVERY ADMITTED REFERENCE TYPE IS A BRANCH — the composite case is not a second window but one more
// branch, which is what makes "choose a declared value" a single question however many types the
// parameter admits.
void ibFillDesignerRefTree(ibDataNode& request, std::vector<ibDesignerRefItem>& values,
	const std::vector<const ibValueMetaObjectRecordDataRef*>& types)
{
	ibDataNode& branches = request.Child(ibProtocol::ibRequest::ibPredefinedSelector::Types);
	for (const ibValueMetaObjectRecordDataRef* recordRef : types) {
		ibDataNode& branch = branches.AddChild(0, 0);
		branch.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Title, ibDeclaredTypeName(recordRef));
		ibFillOneTypeBranch(branch, values, recordRef);
	}
}

} // namespace

// ⭐⭐ THE DESIGNER'S OWN ROAD TO A REFERENCE VALUE — one window, no data behind it. The whole of it
// is described in the header; what follows is how the two questions are asked.
bool ibShowPredefinedSelector(ibControlFrame* ownerValue, const ibTypeDescription& declared,
	const ibMetaData* metaData)
{
	if (ownerValue == nullptr || metaData == nullptr)
		return false;

	// ⭐⭐ THE REFERENCES COLLAPSE INTO ONE LINE. What the type list offers is the declaration's own
	// PRIMITIVES — a date is typed in, and that road is unchanged — plus a single technical entry
	// standing for every reference it admits (`CompositionPredefinedValue`, see valueComposerField.h:
	// "such a technical type is introduced precisely so that all those references can be selected by
	// it" — Max, 2026-08-28). Asked through the window the product already has, not a list of ours.
	std::vector<const ibValueMetaObjectRecordDataRef*> types;
	ibTypeDescription offered;

	for (const ibClassID& clsid : declared.GetClsidList()) {
		// A FAMILY (`CatalogRef`, `AnyRef`) stands for every reference it admits: each is a branch of its own, as if
		// it had been declared by hand - the way every picker opens a barrier (clsid_is_any, clsid_admits). The
		// family itself is in no configuration's image, so asked as a type it named nothing and none were offered.
		if (IsReference(clsid) && clsid_is_any(clsid)) {
			for (const ibCtorMetaValueType* member : metaData->GetListCtorsByType(ibCtorObjectMetaType_Reference)) {
				const auto* recordRef = clsid_admits(clsid, member->GetClassType())
					? dynamic_cast<const ibValueMetaObjectRecordDataRef*>(member->GetMetaObject()) : nullptr;
				if (recordRef != nullptr && std::find(types.begin(), types.end(), recordRef) == types.end())
					types.push_back(recordRef);
			}
			continue;
		}
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(clsid);
		const auto* recordRef = (so != nullptr && so->GetMetaTypeCtor() == ibCtorObjectMetaType_Reference)
			? dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject()) : nullptr;
		if (recordRef == nullptr)
			offered.AppendMetaType(clsid);
		else if (std::find(types.begin(), types.end(), recordRef) == types.end())
			types.push_back(recordRef);
	}

	if (types.empty())
		return false;   // nothing referenceable is declared — the ordinary roads answer alone

	offered.AppendMetaType(g_compositionPredefinedCLSID);

	const ibClassID clsid = ibTypeControlFactory::ShowSelectType(metaData, offered);
	if (clsid == 0)
		return false;   // the list was closed

	// ⚠ A PRIMITIVE ENDS THE CONVERSATION HERE — the type is settled and the cell now holds an empty
	// value of it, which is typed into or opened again for the calendar behind a date. Said to the cell
	// as every answer the client brings back is (ChoiceProcessing), so a window waiting on it hears it.
	if (clsid != g_compositionPredefinedCLSID) {
		if (!metaData->IsRegisterCtor(clsid))
			return false;
		ibValue settled = metaData->CreateObject(clsid);
		ownerValue->ChoiceProcessing(settled);
		return true;
	}

	// …AND THE TECHNICAL ENTRY ASKS THE SECOND QUESTION: which of the values the configuration
	// declares this is — an empty reference, a predefined element, an enumeration member — with every
	// admitted reference type standing as its own branch, in the client's window.
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	std::vector<ibDesignerRefItem> values;
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::PredefinedSelector));
	ibFillDesignerRefTree(request, values, types);

	ibDataNode response;
	if (!frame->Request(request, response) || !response.FindField(ibProtocol::ibRequest::ibPredefinedSelector::Id))
		return false;   // the window was closed
	const std::int32_t id = response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibPredefinedSelector::Id);
	if (id < 0 || static_cast<size_t>(id) >= values.size())
		return false;
	const ibDesignerRefItem& chosen = values[static_cast<size_t>(id)];

	// ⭐⭐ WHAT IS WRITTEN IS THE DECLARATION, NOT A LIVE REFERENCE. A reference object is runtime — a
	// session, a register, a row to read — and this value is going into a DESCRIPTION that is saved
	// with the configuration and read back while the next load is still building the tree. So the
	// choice is stored as what it is: this metaobject, this declared value, and the name it reads by.
	// The runtime reference is made from it at execution, by the running composer
	// (ibMaterializeCompositionValue).
	const ibString written = ibDeclaredTypeName(chosen.m_ref) + ibT(".") + chosen.m_name;
	ibValue value(new ibValueCompositionPredefined(chosen.m_ref->GetMetaID(), chosen.m_guid, written));
	ownerValue->ChoiceProcessing(value);
	return true;
}
