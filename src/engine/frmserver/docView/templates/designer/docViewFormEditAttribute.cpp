////////////////////////////////////////////////////////////////////////////
//	Description : the form editor's attribute tree — the desktop's walk (designer/win/editor/visualEditor/
//	              visualEditorAttributeTree.cpp) on the server's form: its rows drawn beside the form, the rows under
//	              one given when the tab is asked (Composition), and the tree's doors (Attribute)
////////////////////////////////////////////////////////////////////////////

#include "docViewFormEdit.h"

#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject::Create (reference-as-source)
#include "backend/srcDataObject.h"                                  // ibSourceDataObject / ibSourceExplorer / ConvertToMetaIds
#include "backend/system/value/valueDataComposition.h"              // g_valueDataCompositionCLSID — drags as a gridbox
#include "backend/system/value/valueSpreadsheet.h"                  // g_valueSpreadsheetCLSID — so does a document
#include "backend/system/value/valueTable.h"                        // ibValueModelTable — a value-table attribute's columns
#include "core/serialize/dataBuilder.h"
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formAttribute.h"
#include "frmserver/visualView/ctrl/gridBox.h"                      // g_controlGridBoxCLSID
#include "frmserver/visualView/ctrl/tableBox.h"                     // g_controlTableBoxCLSID
#include "frmserver/visualView/ctrl/widgets.h"                      // g_controlCheckboxCLSID / g_controlTextCtrlCLSID
#include "protocol/protocol.h"

typedef ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo ibTableColumnInfo;

namespace {

// A clsid as the wire carries it — 64 bits do not fit a JSON number.
wxString ClsidText(const ibClassID& clsid)
{
	return wxString::Format(wxT("%llu"), static_cast<unsigned long long>(clsid));
}

// The control-class CLSID a node maps to (0 = not draggable). Mirrors BuildForm's type->control choice
// for ONE node: table / section -> Tablebox, boolean -> Checkbox, scalar / reference -> Textctrl. A
// composite OBJECT container (a non-table, non-reference metaobject: e.g. the main object attribute) has
// no single control -> not draggable; drag its fields instead. Returns the CLSID (kind-typed), not a
// class name: it is only the draggable gate now — the drop side re-resolves the class from the path.
ibClassID DropControlClass(bool isTable, const std::vector<ibMetaID>& refTypes, const ibTypeDescription& typeDesc)
{
	if (isTable)
		return g_controlTableBoxCLSID;
	// A composition and a spreadsheet document are draggable — both build a GRIDBOX on the drop side
	// (the box that shows a document, and a composition composes one).
	if (typeDesc.GetClsidCount() == 1 &&
		(typeDesc.ContainType(g_valueDataCompositionCLSID) || typeDesc.ContainType(g_valueSpreadsheetCLSID)))
		return g_controlGridBoxCLSID;
	if (typeDesc.GetClsidCount() == 1) {
		if (typeDesc.ContainType(ibValueTypes::TYPE_BOOLEAN))
			return g_controlCheckboxCLSID;
		if (typeDesc.ContainType(ibValueTypes::TYPE_STRING)
			|| typeDesc.ContainType(ibValueTypes::TYPE_NUMBER)
			|| typeDesc.ContainType(ibValueTypes::TYPE_DATE))
			return g_controlTextCtrlCLSID;
	}
	if (!refTypes.empty())              // reference (incl. composite) -> a select-field textctrl
		return g_controlTextCtrlCLSID;
	return 0;                           // composite object container -> not a single control
}

// Does this attribute have a composition to expand? A reference type expands metadata-only (targets
// present); an object / list type expands through its live source's explorer (when materialised). A
// primitive (neither) is a leaf. Cheap — the actual children are built lazily on first expand.
bool HasComposition(ibFormAttributeValue* entry, const std::vector<ibMetaID>& refTypes)
{
	if (!refTypes.empty())
		return true;
	if (ibSourceDataObject* src = entry != nullptr ? entry->GetSourceValue() : nullptr) {
		const ibSourceExplorer* explorer = src->GetSourceExplorer();
		return explorer != nullptr && explorer->GetHelperCount() > 0;
	}
	return false;
}

// ONE ROW — the desktop's tree item and what its item data held (the path from the attribute, the reference's
// targets, the view flags); `id` the attribute's on a root row, none under it.
ibDataNode& AppendRow(ibDataNode& parent, ibMetaID id, const wxString& label, const std::vector<ibSourceHop>& path,
	const std::vector<ibMetaID>& refTypes, bool tableSection, ibClassID dropControl, bool underList)
{
	ibDataNode& row = parent.AddChild(0, id);
	row.SetValue(ibProtocolName::Name, label);

	std::vector<ibDataValue> hops;
	for (const ibSourceHop& hop : path) {
		std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
		node->SetValue(ibProtocolName::Id, static_cast<s32>(hop.m_id));
		if (hop.m_type != g_valueUndefinedCLSID)
			node->SetValue(ibProtocolName::Type, ClsidText(hop.m_type));
		hops.push_back(ibDataValue::Child(node));
	}
	row.AddField(ibProtocolName::Path, ibDataValue::Array(std::move(hops)));

	if (!refTypes.empty()) {
		std::vector<ibDataValue> ids;
		for (const ibMetaID& target : refTypes)
			ids.push_back(ibDataValue::Int(target));
		row.AddField(ibProtocolName::Targets, ibDataValue::Array(std::move(ids)));
		row.SetValue(ibProtocolName::HasComposition, true);   // reference -> lazy [+]
	}
	if (tableSection)
		row.SetValue(ibProtocolName::TableSection, true);
	if (dropControl != 0)
		row.SetValue(ibProtocolName::DropControl, ClsidText(dropControl));
	if (underList)
		row.SetValue(ibProtocolName::UnderList, true);
	return row;
}

// Append a SOURCE's fields AND table sections as child rows under `parent`, each carrying the full
// binding path (prefix + column id) + its view flags. Shows EVERYTHING — unlike the picker, which
// filters to sections OR fields; the attribute tree is a full-composition view. A REFERENCE column gets
// a [+] and expands LAZILY (references are the only cyclic source — never eager). A table section
// is finite / non-cyclic, so its columns are built inline in this same pass (a reference column INSIDE
// it still expands lazily). `metaData` resolves reference targets; `suffix` disambiguates composite-
// reference branches by target name.
void AppendComposition(ibDataNode& parent, const std::vector<ibSourceHop>& prefix,
	const ibSourceExplorer& explorer, const ibMetaData* metaData, const wxString& suffix = wxEmptyString,
	bool parentIsList = false)
{
	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const ibSourceExplorer* col = explorer.GetHelper(i);
		if (col == nullptr)
			continue;
		std::vector<ibSourceHop> childPath = prefix;
		childPath.push_back({ col->GetSourceId() });   // this column's hop (scalar — a reference pins its branch when ITS children descend)
		// Branches come from what a value may BE, not from what is declared: a characteristic declares
		// one class no value carries, and its branches are the chart's own references.
		const std::vector<ibMetaID> refTypes = ibValueReferenceDataObject::ConvertToMetaIds(col->GetTypeValueDesc().GetClsidList(), metaData);
		// The tree node is the column's IDENTITY — show its NAME, not its Caption/synonym (a control's
		// header takes the synonym; the attribute tree stays name-keyed so a column is findable by name).
		const wxString base = col->GetSourceName();
		const wxString label = suffix.IsEmpty() ? base : base + wxT(" - ") + suffix;
		// A field UNDER a list / table section is a COLUMN of that table. It IS draggable: dropped onto a
		// tablebox bound to the SAME source it appends a bound column (CreateBoundControl's tablebox branch);
		// dropped anywhere else it is declined (ResolveDropControlClass rejects a table column) so no broken
		// scalar is created. Object / reference fields (parentIsList == false) still drop as scalar controls.
		const ibClassID dropControl = DropControlClass(col->IsTableSection(), refTypes, col->GetTypeDesc());
		ibDataNode& child = AppendRow(parent, 0, label, childPath, refTypes, col->IsTableSection(), dropControl, parentIsList);
		if (refTypes.empty() && col->GetHelperCount() > 0) {
			// table section / nested composite: finite, non-cyclic -> build its columns inline now. A
			// section's columns are list-like (not standalone-bindable) -> parentIsList = true for them.
			AppendComposition(child, childPath, *col, metaData, wxEmptyString, true);
		}
	}
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// The rows
///////////////////////////////////////////////////////////////////////////////

void ibFormDesignView::FillAttributeTree(ibDataNode& node) const
{
	const ibMetaData* metaData = m_valueForm->GetMetaData();
	for (unsigned int idx = 0; idx < m_valueForm->GetAttributeCount(); idx++) {
		ibFormAttributeValue* entry = m_valueForm->GetAttribute(idx);
		const ibMetaID attrId = entry->GetId();
		// A reference-typed attribute expands metadata-only (through a reference-as-source); an
		// object / list attribute expands through its live source's explorer (when materialised).
		const std::vector<ibMetaID> refTypes = ibValueReferenceDataObject::ConvertToMetaIds(entry->GetTypeDesc().GetClsidList(), metaData);
		ibSourceDataObject* src = entry->GetSourceValue();
		const bool isTable = src != nullptr && src->IsTableSource();   // list-typed attribute -> a tablebox
		const ibClassID dropControl = DropControlClass(isTable, refTypes, entry->GetTypeDesc());
		ibDataNode& row = AppendRow(node, attrId, entry->GetName(), std::vector<ibSourceHop>{ { attrId } }, refTypes, isTable,
			dropControl, false);   // ROOT: holder + head-of-path
		if (entry->IsMain())
			row.SetValue(ibProtocolName::Main, true);
		if (HasComposition(entry, refTypes))
			row.SetValue(ibProtocolName::HasComposition, true);   // [+]; built lazily on expand
		// A value table grows COLUMNS on the attribute side (the desktop's "Add column", value-table only): what the row
		// adds in itself, as a class's Menu says it.
		if (entry->GetBindValue()->GetClassType() == g_valueTableCLSID) {
			ibDataNode& item = row.Child(ibProtocolName::Menu).AddChild(0, 0);
			item.SetValue(ibProtocolName::Name, wxString(ibProtocolName::Column));
			item.SetValue(ibProtocolName::Title, _("Add column"));
		}
	}
}

ibPropertyObject* ibFormDesignView::FindColumnById(ibFormAttributeValue* entry, ibMetaID id)
{
	// The value table the attribute holds, its column of that id — the column IS the property object (the desktop's
	// SelectColumnInInspector).
	ibValueModelTable* valueTable = nullptr;
	if (entry == nullptr || !entry->GetBindValue()->ConvertToValue(valueTable) || valueTable == nullptr)
		return nullptr;
	ibValueModel::ibValueModelColumnCollection* const cols = valueTable->GetColumnCollection();
	ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* const ci =
		cols != nullptr ? cols->GetColumnByID(static_cast<unsigned int>(id)) : nullptr;
	ibTableColumnInfo* colInfo = nullptr;
	return ci != nullptr && ci->ConvertToValue(colInfo) ? colInfo : nullptr;
}

void ibFormDesignView::ReadPath(const ibDataNode& node, ibSourceDescription& desc)
{
	desc.ClearSource();
	const ibDataValue* const hops = node.FindField(ibProtocolName::Path);
	if (hops == nullptr || hops->Kind() != ibDataKind::Array)
		return;
	for (const ibDataValue& hop : hops->AsArray()) {
		if (hop.Kind() != ibDataKind::Child || hop.AsChild() == nullptr)
			continue;
		unsigned long long type = 0;
		hop.AsChild()->GetValue<wxString>(ibProtocolName::Type).ToULongLong(&type);
		desc.AppendSource(static_cast<ibSourceId>(hop.AsChild()->GetValue<s32>(ibProtocolName::Id)),
			type != 0 ? static_cast<ibClassID>(type) : g_valueUndefinedCLSID);
	}
}

bool ibFormDesignView::FetchComposition(const ibDataNode& request, ibDataNode& response)
{
	// The row as the client holds it — what the desktop's item data held.
	ibSourceDescription desc;
	ReadPath(request, desc);
	const std::vector<ibSourceHop>& path = desc.GetPath();
	std::vector<ibMetaID> refTypes;
	if (const ibDataValue* const targets = request.FindField(ibProtocolName::Targets)) {
		if (targets->Kind() == ibDataKind::Array)
			for (const ibDataValue& id : targets->AsArray())
				refTypes.push_back(static_cast<ibMetaID>(id.AsInt()));
	}
	const bool underList = request.GetValue<bool>(ibProtocolName::UnderList);
	if (path.empty())
		return false;

	// Lazy composition build — the payoff of the picker being a tree: a node ships with a single [+]; its real
	// children are built HERE, once. References stay lazy (never eager) so a self / cyclic reference (A.B.A…) can't
	// blow the stack; a node already filled inline (a table section) is never asked about.
	const ibMetaData* metaData = m_valueForm->GetMetaData();
	if (metaData == nullptr)
		return false;

	ibDataNode& rows = response.Child(ibProtocolName::Rows);
	if (!refTypes.empty()) {
		// Reference node: descend into each TARGET type via an empty reference-as-source (metadata-only,
		// no live row needed) — the design-time twin of the runtime value hop. A COMPOSITE reference
		// (several targets) expands every branch, target-name-disambiguated.
		const bool multi = refTypes.size() > 1;
		for (const ibMetaID& target : refTypes) {
			ibValue refValue = ibValueReferenceDataObject::Create(metaData, target);   // empty typed reference (ref-counted)
			ibSourceDataObject* refSource = nullptr;
			refValue.ConvertToValue(refSource);
			if (refSource == nullptr)
				continue;
			// PIN this reference hop to the branch we descend into — the child fields carry it, so the DRAG /
			// serialized path coerces the composite reference to THIS target (else it read back <not selected>).
			std::vector<ibSourceHop> childPrefix = path;
			childPrefix.back().m_type = refValue.GetClassType();
			if (const ibSourceExplorer* explorer = refSource->GetSourceExplorer())
				AppendComposition(rows, childPrefix, *explorer, metaData,
					multi ? refSource->GetSourceCaption() : wxString(wxEmptyString), underList);
		}
	}
	else if (ibFormAttributeValue* entry = path.size() == 1 ? m_valueForm->FindAttributeById(path.front().m_id) : nullptr) {
		// Root attribute of an object / list type: expand its live source's explorer (fields + sections).
		// A LIST root's children are COLUMNS (not standalone sources) -> mark them under-list.
		if (ibSourceDataObject* src = entry->GetSourceValue())
			if (const ibSourceExplorer* explorer = src->GetSourceExplorer())
				AppendComposition(rows, path, *explorer, metaData, wxEmptyString, src->IsTableSource());
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// The doors
///////////////////////////////////////////////////////////////////////////////

bool ibFormDesignView::FetchAttribute(const ibDataNode& request, ibDataNode& response)
{
	// The desktop's tree's handlers, on the form the client holds — not undoable here: the client takes the form answered
	// as an edit of its own.
	const ibMetaID id = request.GetValue<s32>(ibProtocolName::Attribute);
	ibFormAttributeValue* const entry = id != 0 ? m_valueForm->FindAttributeById(id) : nullptr;
	if (id != 0 && entry == nullptr)
		return false;

	// A value table's column of it — the row under the attribute (the desktop's SelectColumnInInspector).
	const ibMetaID columnId = request.GetValue<s32>(ibProtocolName::Column);
	ibPropertyObject* const column = columnId != 0 ? FindColumnById(entry, columnId) : nullptr;
	if (columnId != 0 && column == nullptr)
		return false;

	ibFormAttributeValue* shown = entry;
	ibMetaID shownColumn = columnId;
	switch (static_cast<ibProtocolFormDesignerCommand>(request.GetValue<s32>(ibProtocolName::Command))) {
	case ibProtocolFormDesignerCommand::Create:
		// Never empty: a unique name and the default type (String), the type changed in the inspector after — the
		// desktop's OnAdd, the new one shown.
		shown = m_valueForm->AttachAttribute(m_valueForm->MakeAttribute(m_valueForm->MakeUniqueAttributeName()));
		break;
	case ibProtocolFormDesignerCommand::Paste: {
		// The node the client copied of its form's Attributes — the whole entry, its description and its held value (a
		// dynamic list's Source, Settings), as a form save writes it; a fresh NON-main entry of it, wired in.
		const ibDataNode* const copied = request.FindChild(ibProtocolName::Value);
		if (copied == nullptr)
			return false;
		ibDataNode node = *copied;
		RestoreProperties(node);
		shown = m_valueForm->PasteAttribute(node);
		break;
	}
	case ibProtocolFormDesignerCommand::Remove:
		// The controls bound to it now render broken / empty — drawn again with the form.
		if (entry == nullptr)
			return false;
		m_valueForm->DetachAttribute(entry);
		shown = nullptr;
		break;
	case ibProtocolFormDesignerCommand::SetMain:
		// None given — no main (its controls get their own command bar back); the source moved to the new main, the
		// controls drawn again with it.
		m_valueForm->SetMainAttribute(entry);
		break;
	case ibProtocolFormDesignerCommand::AddColumn: {
		// The desktop's OnAddColumn: a column of the value table the attribute holds — its columns live in the value and
		// are saved with the attribute; named uniquely among them (Column, Column1, …), String, the type changed in the
		// inspector after; the new one shown.
		ibValueModelTable* valueTable = nullptr;
		if (entry == nullptr || !entry->GetBindValue()->ConvertToValue(valueTable) || valueTable == nullptr)
			return false;
		ibValueModel::ibValueModelColumnCollection* const cols = valueTable->GetColumnCollection();
		if (cols == nullptr)
			return false;
		const wxString base = _("Column");
		wxString name = base;
		for (unsigned int suffix = 0;;) {
			bool taken = false;
			for (unsigned int c = 0; c < cols->GetColumnCount(); c++) {
				ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ci = cols->GetColumnInfo(c);
				if (ci != nullptr && ci->GetColumnName() == name) { taken = true; break; }
			}
			if (!taken)
				break;
			name = wxString::Format(wxT("%s%u"), base, ++suffix);
		}
		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* const added =
			cols->AddColumn(name, ibTypeDescription(g_valueStringCLSID), name);
		if (added == nullptr)
			return false;
		shownColumn = static_cast<ibMetaID>(added->GetColumnID());
		break;
	}
	case ibProtocolFormDesignerCommand::Set:
		if (entry == nullptr)
			return false;
		// A COLUMN'S — its own property, as the inspector's door sets it.
		if (column != nullptr) {
			if (!SetProperty(column, request))
				return false;
			break;
		}
		// THE NAME — through the form's rename, as the desktop's tree renamed it (OnEndLabelEdit): unique, and the
		// module's variable bound under it again. Text — the wire brings it back a field (RestoreProperties).
		if (request.GetValue<wxString>(ibProtocolName::Name) == ibProtocolName::Name) {
			if (!m_valueForm->RenameAttribute(entry, request.GetValue<wxString>(ibProtocolName::Value)))
				return false;
		}
		else if (!SetProperty(entry, request))
			return false;
		break;
	default:
		return false;
	}

	WriteForm(response);
	if (shown != nullptr)
		response.SetValue(ibProtocolName::Attribute, static_cast<s32>(shown->GetId()));
	if (shown != nullptr && shownColumn != 0)
		response.SetValue(ibProtocolName::Column, static_cast<s32>(shownColumn));
	return true;
}
