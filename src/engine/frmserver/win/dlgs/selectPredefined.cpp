////////////////////////////////////////////////////////////////////////////
//	Description : PredefinedSelector — the designer's declared-value picker over the protocol
////////////////////////////////////////////////////////////////////////////

#include "selectPredefined.h"

#include <algorithm>
#include <map>
#include <vector>

#include "backend/appData.h"
#include "backend/backend_mainFrame.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/declaredPresentation.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/session/session.h"
#include "backend/system/value/composition/valueComposerField.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/visualView/ctrl/frame.h"
#include "protocol/protocol.h"

namespace {

using ibPredefinedValue = ibValueMetaObjectRecordDataHierarchyMutableRef::ibPredefinedValueObject;

// One type's declared values as nested protocol children: EmptyRef, then members / predefined tree.
// Built by index so AddChild cannot invalidate a parent pointer mid-pass (m_children is a vector).
void WriteOneTypeValues(ibDataNode& branch, const ibValueMetaObjectRecordDataRef* recordRef)
{
	if (recordRef == nullptr)
		return;

	ibDataNode& empty = branch.AddChild(0, 0);
	empty.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Name, wxString(ibRefMember::EmptyRef));
	empty.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Guid, wxString());

	if (const auto* enumeration = dynamic_cast<const ibValueMetaObjectRecordDataEnumRef*>(recordRef)) {
		for (const ibValueMetaObjectEnum* member : enumeration->GetEnumObjectArray()) {
			if (member == nullptr)
				continue;
			ibDataNode& leaf = branch.AddChild(0, 0);
			leaf.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Name, member->GetName());
			leaf.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Guid, ibGuid(member->GetGuid()).str());
		}
	}

	if (const auto* hierarchy =
			dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(recordRef)) {

		struct Row {
			wxString name;
			wxString guid;
			int      parent = -1;   // index in rows, or -1 for the type root
		};
		std::vector<Row> rows;
		std::map<const ibPredefinedValue*, int> indexOf;
		for (const auto& item : hierarchy->GetPredefinedValueArray()) {
			if (!item)
				continue;
			indexOf[item.get()] = static_cast<int>(rows.size());
			rows.push_back(Row{ item->GetPredefinedName(), item->GetPredefinedGuid().str(), -1 });
		}
		for (const auto& item : hierarchy->GetPredefinedValueArray()) {
			if (!item)
				continue;
			const ibPredefinedValue* under = item->GetPredefinedParent().get();
			if (under == nullptr)
				continue;
			const auto found = indexOf.find(under);
			if (found != indexOf.end())
				rows[static_cast<size_t>(indexOf[item.get()])].parent = found->second;
		}

		const auto writeUnder = [&](auto&& self, ibDataNode& parent, int parentIdx) -> void {
			for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
				if (rows[static_cast<size_t>(i)].parent != parentIdx)
					continue;
				ibDataNode& leaf = parent.AddChild(0, 0);
				leaf.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Name, rows[static_cast<size_t>(i)].name);
				leaf.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Guid, rows[static_cast<size_t>(i)].guid);
				self(self, leaf, i);
			}
		};
		writeUnder(writeUnder, branch, -1);
	}
}

// Collect admitted reference metaobjects and primitive clsids from the declaration.
void CollectDeclared(const ibTypeDescription& declared, const ibMetaData* metaData,
	std::vector<const ibValueMetaObjectRecordDataRef*>& types, ibTypeDescription& primitives)
{
	if (metaData == nullptr)
		return;

	for (const ibClassID& clsid : declared.GetClsidList()) {
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
			primitives.AppendMetaType(clsid);
		else if (std::find(types.begin(), types.end(), recordRef) == types.end())
			types.push_back(recordRef);
	}
}

} // namespace

bool ibShowPredefinedSelector(ibControlFrame* ownerValue, const ibTypeDescription& declared,
	const ibMetaData* metaData)
{
	if (ownerValue == nullptr || metaData == nullptr || !appData->DesignerMode())
		return false;

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	std::vector<const ibValueMetaObjectRecordDataRef*> types;
	ibTypeDescription primitives;
	CollectDeclared(declared, metaData, types, primitives);
	if (types.empty())
		return false;   // nothing referenceable — the ordinary roads answer alone

	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<s32>(ibProtocol::ibRequest::ibKind::PredefinedSelector));
	request.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Caption, _("Select a predefined value"));

	ibDataNode& typesNode = request.Child(ibProtocol::ibRequest::ibPredefinedSelector::Types);
	for (const ibValueMetaObjectRecordDataRef* recordRef : types) {
		ibDataNode& branch = typesNode.AddChild(0, 0);
		branch.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Title, ibDeclaredTypeName(recordRef));
		branch.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::MetaId, static_cast<s32>(recordRef->GetMetaID()));
		WriteOneTypeValues(branch, recordRef);
	}

	s32 primId = 0;
	ibDataNode& primsNode = request.Child(ibProtocol::ibRequest::ibPredefinedSelector::Primitives);
	for (const ibClassID& clsid : primitives.GetClsidList()) {
		ibDataNode& item = primsNode.AddChild(0, 0);
		item.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Id, primId++);
		const ibCtorAbstractType* ctor = metaData->GetAvailableCtor(clsid);
		item.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Caption,
			ctor != nullptr ? ctor->GetClassName() : wxString() << clsid);
		item.SetValue(ibProtocol::ibRequest::ibPredefinedSelector::Type, wxString() << clsid);
	}

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	// A primitive settled — empty value of that type, typed into on the next click.
	if (response.FindField(ibProtocol::ibRequest::ibPredefinedSelector::Type) != nullptr) {
		unsigned long long parsed = 0;
		if (!response.GetValue<wxString>(ibProtocol::ibRequest::ibPredefinedSelector::Type).ToULongLong(&parsed)
				|| parsed == 0 || !metaData->IsRegisterCtor(parsed))
			return false;
		ibValue settled = metaData->CreateObject(static_cast<ibClassID>(parsed));
		ownerValue->ChoiceProcessing(settled);
		return true;
	}

	// A declared value — stored as CompositionPredefined, materialised at run time.
	if (response.FindField(ibProtocol::ibRequest::ibPredefinedSelector::MetaId) == nullptr)
		return false;   // cancelled

	const ibMetaID metaId = static_cast<ibMetaID>(response.GetValue<s32>(ibProtocol::ibRequest::ibPredefinedSelector::MetaId));
	const ibGuid guid(response.GetValue<wxString>(ibProtocol::ibRequest::ibPredefinedSelector::Guid));
	wxString written = response.GetValue<wxString>(ibProtocol::ibRequest::ibPredefinedSelector::Written);
	if (written.IsEmpty()) {
		const ibValueMetaObjectRecordDataRef* recordRef = nullptr;
		for (const ibValueMetaObjectRecordDataRef* candidate : types) {
			if (candidate != nullptr && candidate->GetMetaID() == metaId) {
				recordRef = candidate;
				break;
			}
		}
		if (recordRef == nullptr)
			return false;
		const wxString leaf = response.GetValue<wxString>(ibProtocol::ibRequest::ibPredefinedSelector::Name);
		written = ibDeclaredTypeName(recordRef) + wxT(".") +
			(leaf.IsEmpty() ? wxString(ibRefMember::EmptyRef) : leaf);
	}

	ibValue chosen(new ibValueCompositionPredefined(metaId, guid, written));
	ownerValue->ChoiceProcessing(chosen);
	return true;
}
