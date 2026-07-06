#include "advpropSource.h"

#include "backend/propertyManager/property/variant/variantSource.h"   // ibVariantDataSource + ibBackendFormAttribute (via backend_type.h)
#include "backend/system/value/valueTable.h"

#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/propertyEditor.h"

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGDataSourceProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGDataSourceProperty, wxPGProperty, TextCtrlAndButton)

// register frontend property 
class ibPropertySourceLoader
{
public:
	ibPropertySourceLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGDataSourceProperty, ibPropertySource::ms_propertySource);
	}
}g_sourceLoader;

ibPGDataSourceProperty::ibPGDataSourceProperty(const ibPropertyObject* property, const wxString& label, const wxString& strName,
	const wxVariant& value) : wxPGProperty(label, strName)
{
	ibVariantDataSource* dataSource = property_cast(value, ibVariantDataSource);
	wxASSERT(dataSource);

	const ibBackendTypeSourceFactory* typeFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	wxASSERT(typeFactory);
	m_typeSelector = new ibPGTypeProperty(property, typeFactory != nullptr ? typeFactory->GetFilterDataType() : ibSelectorDataType::ibSelectorDataType_reference, _("Type"), wxT("type"), dataSource->CloneSourceAttribute());
	AddPrivateChild(m_typeSelector);

	//m_flags |= wxPGFlags::ReadOnly;
	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGDataSourceProperty::ValueToString(wxVariant& variant,
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return variant.GetString();
}

bool ibPGDataSourceProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	if (text.IsEmpty()) {
		ibVariantDataSource* dataSource = property_cast(variant, ibVariantDataSource);
		if (dataSource != nullptr) {
			dataSource->SetSource(wxNOT_FOUND);
			return true;
		}
		return false;
	}
	return text.IsEmpty();
}

void ibPGDataSourceProperty::RefreshChildren()
{
	const ibVariantDataSource* dataSource = property_cast(m_value, ibVariantDataSource);
	if (dataSource != nullptr) {
		// No source picked -> show NO Type at all: the control's type now comes FROM the source, so an
		// unset source has no meaningful type to display. A bound source shows its resolved leaf type.
		const bool hasSource = !dataSource->IsEmptySource();
		m_typeSelector->Hide(!hasSource);
		// The clone self-resolves the leaf type through the source EXPLORER (CloneSourceAttribute ->
		// RefreshTypeFromSource, pull-on-get like the Type side) — no separate priming call. Cloning from the bare
		// leaf id (CloneSourceAttribute(leafId)) would instead rebuild the type config-wide via metadata
		// (FindAnyObjectByFilter) and mis-type a value-table / dynamic-list column, whose RAM id has no metaobject.
		m_typeSelector->SetValue(dataSource->CloneSourceAttribute());
	}
	m_typeSelector->SetFlagRecursively(wxPGFlags::ReadOnly, dataSource != nullptr ? !dataSource->IsPropAllowed() : false);
	ibPGDataSourceProperty::SetExpanded(true);
}

wxVariant ibPGDataSourceProperty::ChildChanged(wxVariant& thisValue, int childIndex, wxVariant& childValue) const
{
	ibVariantDataSource* dataSource = property_cast(thisValue, ibVariantDataSource);
	if (dataSource != nullptr && childIndex == 0) {
		ibVariantDataAttributeSource* attrSource = property_cast(childValue, ibVariantDataAttributeSource);
		if (attrSource != nullptr) {
			ibVariantDataSource* cloneDataSource = dataSource->Clone();
			if (!m_typeSelector->HasFlag(wxPGFlags::ReadOnly)) cloneDataSource->SetSource(wxNOT_FOUND);
			cloneDataSource->SetSourceAttribute(attrSource);
			return cloneDataSource;
		}
	}
	return wxVariant();
}

#include <wx/treectrl.h>

#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/query/queryable.h"   // ibBackendQueryable / ibBackendQueryableHolder — family-blind dot-walk target

wxPGEditorDialogAdapter* ibPGDataSourceProperty::GetEditorDialog() const
{
	enum {
		icon_attribute = 0,
		icon_table
	};

	class ibPGEditorDataSourceDialogAdapter : public wxPGEditorDialogAdapter {

		wxString MakeTypeString(const ibMetaData* metaData, const ibTypeDescription& typeDesc) const {
			wxString strDescr;
			for (auto clsid : typeDesc.GetClsidList()) {
				if (metaData->IsRegisterCtor(clsid) && strDescr.IsEmpty()) {
					strDescr = metaData->GetNameObjectFromID(clsid);
				}
				else if (metaData->IsRegisterCtor(clsid)) {
					strDescr = strDescr + wxT(", ") + metaData->GetNameObjectFromID(clsid);
				}
			}
			if (strDescr.IsEmpty()) return wxT("<empty>");
			return strDescr;
		}

		class ibTreeItemDataSource : public wxTreeItemData {
			const wxString m_nameProp;
			const ibSourceId m_id;
			const bool m_tableSection;
			const std::vector<ibSourceHop> m_prefixPath;        // ancestor hop chain (id + pinned type; empty = top level)
			const std::vector<ibMetaID> m_refTypes;             // referenced TARGET type metaIDs (composite) => lazily expand via an empty reference-as-source
			bool m_loaded = false;                              // referenced children already built?
		public:
			ibTreeItemDataSource(const wxString& nameProp, const ibSourceId& id, bool tableSection,
				std::vector<ibSourceHop> prefixPath = {},
				std::vector<ibMetaID> refTypes = {})
				: wxTreeItemData(), m_nameProp(nameProp), m_id(id), m_tableSection(tableSection),
				m_prefixPath(std::move(prefixPath)), m_refTypes(std::move(refTypes)) {};

			const wxString& GetPropName() const { return m_nameProp; }
			const ibSourceId& GetID() const { return m_id; }
			const bool IsTableSection() const { return m_tableSection; }
			const std::vector<ibSourceHop>& GetPrefixPath() const { return m_prefixPath; }
			const std::vector<ibMetaID>& GetRefTypes() const { return m_refTypes; }
			bool HasRef() const { return !m_refTypes.empty(); }
			bool IsLoaded() const { return m_loaded; }
			void SetLoaded() { m_loaded = true; }

			// A node's children inherit its full chain: ancestor prefix + this node's own hop, TYPED. `hopType`
			// PINS a reference to the target branch descended into (undefined for a plain container hop).
			std::vector<ibSourceHop> ChildPrefix(const ibClassID& hopType) const {
				std::vector<ibSourceHop> child = m_prefixPath;
				child.push_back({ m_id, hopType });
				return child;
			}

			// The whole binding address for this node: ancestor prefix + this node (leaf).
			ibSourceDescription GetSourceDesc() const {
				ibSourceDescription desc;
				for (const ibSourceHop& hop : m_prefixPath) desc.AppendSource(hop.m_id, hop.m_type);
				desc.AppendSource(m_id);   // leaf column — a scalar imposes no type
				return desc;
			}
		};

		// A reference type-list -> the SOURCE QUERYABLES of every type it can point at. FAMILY-BLIND:
		// a reference clsid resolves through its type ctor's queryable HOLDER (the SAME chain the L3
		// dot-walk uses — ibBackendQueryable::ResolveReferenceTarget), never a metaobject family. A
		// composite attribute has several reference types; the picker expands all of them so the user
		// can reach a field of any branch (each field keeps its own type-specific metaId, so the runtime
		// renders it only when the live value is of that type). Serves BOTH a metaobject attribute's
		// clsid list AND a queryable explorer column's clsid list — one dot-walk for either picker.
		static std::vector<ibMetaID> GetReferenceTypes(const std::vector<ibClassID>& clsids, const ibMetaData* metaData) {
			// ONE clsid -> target resolution, shared with the structure dot-walk: an EMPTY reference of each
			// target (reference-as-source) vends its explorer on expand. Lives in backend so the picker and
			// WalkColumns can't drift apart.
			return ibValueReferenceDataObject::ConvertToMetaIds(clsids, metaData);   // the static lives on the reference now
		}

		// Lazy dot-expansion: drop the dummy [+], then append the referenced TYPE's columns via the HOP --
		// an EMPTY (typed) reference of each target type (reference-as-source) vends its explorer, held in an
		// ibValue so it outlives the read. Composite reference => every target branch, name-disambiguated.
		// Self / cyclic refs are safe (built only on demand). ONE mechanism: the design-time twin of the value-hop.
		static void ExpandReference(wxTreeCtrl* tc, const ibMetaData* metaData, ibTreeItemDataSource* data, const wxTreeItemId& node) {
			if (data == nullptr || !data->HasRef() || data->IsLoaded() || metaData == nullptr)
				return;
			data->SetLoaded();
			tc->DeleteChildren(node);   // drop the dummy
			const bool multi = data->GetRefTypes().size() > 1;   // composite: disambiguate fields by target name
			for (const ibMetaID& refType : data->GetRefTypes()) {
				// PIN this reference hop to the target we descend into — the child fields carry it, so a later
				// walk coerces the composite reference to THIS branch instead of guessing. Recorded ALWAYS (even
				// single-target): the reference could be widened to composite later without re-picking.
				const std::vector<ibSourceHop> childPrefix = data->ChildPrefix(reference_to_clsid(refType));
				ibValue refValue = ibValueReferenceDataObject::Create(metaData, refType);   // empty typed reference (ref-counted)
				ibSourceDataObject* refSource = nullptr;
				refValue.ConvertToValue(refSource);
				if (refSource != nullptr) {
					const ibSourceExplorer* refExplorer = refSource->GetSourceExplorer();
					if (refExplorer != nullptr)
						AppendExplorerColumns(tc, node, *refExplorer, childPrefix, metaData,
							multi ? refSource->GetSourceCaption() : wxString(wxEmptyString));
				}
			}
		}

		// Append a SOURCE's columns as child nodes under `parent`, each carrying `prefix` (the path to
		// the parent source) so a node's whole address is prefix + its column id. A reference column
		// gets a dummy [+] for the family-blind dot-walk. Table-section children are skipped — a column
		// binds a scalar/reference field, not a nested tabular section. Shared by every root of the
		// table-column picker (the bound table AND each header object above it).
		static void AppendExplorerColumns(wxTreeCtrl* tc, const wxTreeItemId& parent, const ibSourceExplorer& explorer,
			const std::vector<ibSourceHop>& prefix, const ibMetaData* metaData, const wxString& suffix = wxEmptyString) {
			for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
				const ibSourceExplorer* col = explorer.GetHelper(i);
				if (col->IsTableSection())
					continue;
				std::vector<ibMetaID> refTypes = GetReferenceTypes(col->GetClsidList(), metaData);
				const wxString label = suffix.IsEmpty() ? col->GetSourceName() : col->GetSourceName() + wxT(" - ") + suffix;
				ibTreeItemDataSource* itemData = new ibTreeItemDataSource(label, col->GetSourceId(), false, prefix, refTypes);
				wxTreeItemId newItem = tc->AppendItem(parent, label, icon_attribute, icon_attribute, itemData);
				if (!refTypes.empty())
					tc->AppendItem(newItem, wxEmptyString);   // dummy -> [+] dot-walk
			}
		}

		// The metaobject an attribute's Type lives in (object / reference / list →
		// its composite metaobject); null for a primitive. Pure METADATA, no
		// transient source object — mirrors ibVariantDataSource::WalkPath's TypeMeta,
		// so the picker enumerates exactly what the resolve later walks. A reference
		// Type resolves to the REFERENCED metaobject (its fields, read-only by rule).
		static const ibValueMetaObjectCompositeData* TypeMeta(const ibMetaData* metaData, const ibTypeDescription& typeDesc) {
			if (metaData == nullptr || typeDesc.GetClsidCount() != 1) return nullptr;
			const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(typeDesc.GetFirstClsid());
			const ibValueMetaObjectCompositeData* meta = nullptr;
			if (typeCtor != nullptr) typeCtor->ConvertToMetaValue(meta);
			return meta;
		}

		// Is this Type a TABLE source — per the class factory (IsTableValue by CLSID), the SAME
		// gate the whole engine uses (formObject / formAttribute / FilterSource). Covers a
		// metaobject List AND a queryable-based dynamic list (no metaobject). The ONE place the
		// picker asks "table or attribute?" — root, fields, and the no-metaobject leaf all use it.
		static bool IsTableType(const ibMetaData* metaData, const ibTypeDescription& typeDesc) {
			if (metaData == nullptr || typeDesc.GetClsidCount() != 1) return false;
			const ibCtorAbstractType* ctor = metaData->GetAvailableCtor(typeDesc.GetFirstClsid());
			return ctor != nullptr && ctor->IsTableValue();
		}

		// Find the child node of `parent` whose stored sourceId matches — used to walk a
		// saved dotted path on re-open and land on the same leaf.
		static wxTreeItemId FindChildBySourceId(wxTreeCtrl* tc, const wxTreeItemId& parent, const ibSourceId& id) {
			wxTreeItemIdValue cookie;
			for (wxTreeItemId child = tc->GetFirstChild(parent, cookie); child.IsOk(); child = tc->GetNextChild(parent, cookie)) {
				ibTreeItemDataSource* data = dynamic_cast<ibTreeItemDataSource*>(tc->GetItemData(child));
				if (data != nullptr && data->GetID() == id)
					return child;
			}
			return wxTreeItemId();
		}

		wxImageList* GetSourceImageList() const {
			wxImageList* list = new wxImageList(icon_size, icon_size);
			list->Add(ibValue::GetIconGroup());
			list->Add(ibValueModelTable::GetIconGroup());
			return list;
		}

		bool ProcessAttribute(wxPropertyGrid* pg, wxPGProperty* dlgProp, const ibBackendTypeSourceFactory* typeFactory, ibVariantDataSource* srcData) {

			const ibMetaID& dataSource = srcData != nullptr ? srcData->GetSource() : wxNOT_FOUND;

			const ibMetaData* metaData = typeFactory->GetMetaData();
			if (metaData == nullptr) return false;

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice source"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;

			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			const bool is_tableBox = typeFactory->GetFilterSourceDataType() == ibSourceDataType::ibSourceDataType_table;

			// HIDE_ROOT: a hidden root holds one top-level node PER available source
			// (a tree has a single real root — we cannot AddRoot per source).
			wxTreeCtrl* tc = new wxTreeCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

			// Make an state image list containing small icons
			tc->AssignImageList(GetSourceImageList());

			rowsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			tc->SetDoubleBuffered(true);
			tc->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
				dlg->Move(pg->GetGoodEditorDialogPosition(dlgProp, dlg->GetSize()));
			}

			tc->SetFocus();

			// Each form attribute is the GATE: path[0] = attribute id, then the binding walks the
			// attribute's TYPE. The FIRST level is metaobject-walked because it must be UNIVERSAL —
			// it covers DataProcessors / Reports, which have NO queryable (queryables are DB-only). The
			// dot-walk DEEPER is family-blind via queryable (reference targets are always DB objects).
			// Display uses the attribute's REAL name + Type.
			struct PickEntry { ibSourceId head; ibBackendFormAttributeValue* holder; wxString name; };
			std::vector<PickEntry> entries;
			std::vector<ibBackendFormAttributeValue*> holders;
			typeFactory->GetSourceList(holders);
			for (ibBackendFormAttributeValue* holder : holders) {
				if (holder != nullptr)
					entries.push_back({ holder->GetId(), holder, holder->GetName() });
			}

			const wxTreeItemId hiddenRoot = tc->AddRoot(wxEmptyString);   // one hidden root; sources are its children
			wxTreeItemId rootItem;   // hoisted: re-open positioning (below) walks the saved path from it
			for (const auto& entry : entries) {
				const ibSourceId headAttrId = entry.head;
				const std::vector<ibSourceHop> basePrefix{ { headAttrId } };   // every hop under this attribute leads with its id (type undefined)
				const wxString rootLabel = entry.name + wxT(" (") + MakeTypeString(metaData, entry.holder->GetTypeDesc()) + wxT(")");

				// The attribute's TYPE metaobject — where its fields live. Null for a PRIMITIVE
				// (→ a scalar LEAF) OR for a queryable-based TABLE (a dynamic list: HAS no
				// metaobject but IS a whole table). Tell them apart by the class factory's
				// IsTableValue: a table LEAF binds the whole list [id] (a tablebox can pick it;
				// its columns are chosen separately in ProcessTableColumn), a primitive LEAF is
				// a scalar binding.
				const ibValueMetaObjectCompositeData* metaObject = TypeMeta(metaData, entry.holder->GetTypeDesc());
				if (metaObject == nullptr) {
					const bool leafIsTable = IsTableType(metaData, entry.holder->GetTypeDesc());
					const int leafIcon = leafIsTable ? icon_table : icon_attribute;
					ibTreeItemDataSource* leafData = new ibTreeItemDataSource(rootLabel, headAttrId, leafIsTable);
					rootItem = tc->AppendItem(hiddenRoot, rootLabel, leafIcon, leafIcon, leafData);
					if (leafIsTable) tc->SetItemBold(rootItem);
					if (headAttrId == dataSource) tc->SelectItem(rootItem);
					continue;
				}

				// Root = the attribute itself (binds [id]). For a LIST-typed attribute the root IS
				// a table — a tablebox binds the whole list [attrId] by picking it; otherwise the
				// root is non-table (a tablebox picks only its tabular-section children, a scalar
				// control its fields). Without this a list attribute can't be bound to a tablebox.
				const bool rootIsTable = IsTableType(metaData, entry.holder->GetTypeDesc());

				ibTreeItemDataSource* rootData = new ibTreeItemDataSource(rootLabel, headAttrId, rootIsTable);
				rootItem = tc->AppendItem(hiddenRoot, rootLabel, icon_attribute, icon_attribute, rootData);
				if (headAttrId == dataSource) tc->SelectItem(rootItem);

				// The attribute's fields/sections come from its SOURCE EXPLORER -- the same self-describing structure
				// WalkSource + the runtime value-hop walk, UNIVERSAL (covers DataProcessors / Reports with no queryable).
				// No metaobject walk: is_tableBox picks the tabular-section nodes, a scalar control the field nodes; a
				// reference field expands lazily (ExpandReference). ONE mechanism, end to end.
				ibSourceDataObject* attrSource = entry.holder->GetSourceValue();
				// This is the COMPOSITE branch (metaObject != nullptr) — the source should be a materialised
				// typed object. A null here means the holder was not Refresh()'d before the picker enumerated
				// it, so the composite root would render CHILDLESS. Catch it in debug; skip safely in release.
				wxASSERT_MSG(attrSource != nullptr, wxT("composite picker attribute has no materialised source (holder not Refresh()'d?)"));
				if (attrSource == nullptr)
					continue;
				const ibSourceExplorer* explorerPtr = attrSource->GetSourceExplorer();
				if (explorerPtr == nullptr)
					continue;
				const ibSourceExplorer& explorer = *explorerPtr;
				for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
					const ibSourceExplorer* col = explorer.GetHelper(i);
					if (is_tableBox != col->IsTableSection())
						continue;   // tablebox -> only tabular sections; scalar control -> only fields
					const ibSourceId colId = col->GetSourceId();
					if (is_tableBox) {
						const wxString tableLabel = col->GetSourceSynonym();
						ibTreeItemDataSource* tableData = new ibTreeItemDataSource(tableLabel, colId, true, basePrefix);
						wxTreeItemId tableItem = tc->AppendItem(rootItem, tableLabel, icon_table, icon_table, tableData);
						tc->SetItemBold(tableItem);
						if (colId == dataSource) tc->SelectItem(tableItem);
					}
					else {
						const wxString label = col->GetSourceSynonym() + wxT(" (") + MakeTypeString(metaData, col->GetTypeDesc()) + wxT(")");
						std::vector<ibMetaID> refTypes = GetReferenceTypes(col->GetClsidList(), metaData);
						const bool fieldIsTable = IsTableType(metaData, col->GetTypeDesc());
						const int fieldIcon = fieldIsTable ? icon_table : icon_attribute;
						ibTreeItemDataSource* fieldData = new ibTreeItemDataSource(label, colId, fieldIsTable, basePrefix, refTypes);
						wxTreeItemId fieldItem = tc->AppendItem(rootItem, label, fieldIcon, fieldIcon, fieldData);
						if (fieldIsTable) tc->SetItemBold(fieldItem);
						if (!refTypes.empty()) tc->AppendItem(fieldItem, wxEmptyString);
						if (colId == dataSource) tc->SelectItem(fieldItem);
					}
				}
				tc->Expand(rootItem);
			}

			// Lazy dot-expansion: a reference node carries a single dummy [+]; on first expand
			// ExpandReference drops it and builds the referenced type's fields. NEVER eager — a
			// self/cyclic reference (A.B.A...) would blow the stack (so only the root is shown up
			// front, the rest opens on demand). The SAME handler the table-column picker uses.
			tc->Bind(wxEVT_TREE_ITEM_EXPANDING, [tc, metaData](wxTreeEvent& evt) {
				ExpandReference(tc, metaData, dynamic_cast<ibTreeItemDataSource*>(tc->GetItemData(evt.GetItem())), evt.GetItem());
			});

			// Re-open positioning: walk the saved sourceId path (first hop .. leaf), expanding
			// each reference node (programmatic Expand fires the lazy build above) and
			// selecting the leaf — the user lands on it. A length-1 path lands on a top field.
			if (srcData != nullptr && hiddenRoot.IsOk()) {
				const std::vector<ibSourceHop>& chain = srcData->GetSourceDesc().GetPath();
				// Walk from the hidden root: chain[0] (head attribute id) selects the
				// source node, the rest descends into its fields.
				wxTreeItemId node = hiddenRoot;
				for (size_t i = 0; i < chain.size(); ++i) {
					wxTreeItemId child = FindChildBySourceId(tc, node, chain[i].m_id);
					if (!child.IsOk())
						break;
					if (i + 1 < chain.size()) {
						tc->Expand(child);                      // build the next level
						node = child;
					}
					else {
						tc->SelectItem(child);
						tc->EnsureVisible(child);
					}
				}
			}

			int res = dlg->ShowModal();

			wxTreeItemId selItem = tc->GetSelection();
			if (selItem.IsOk()) {
				wxTreeItemData* dataItem = tc->GetItemData(selItem);
				if (dataItem != nullptr && res == wxID_OK) {
					ibTreeItemDataSource* item = dynamic_cast<ibTreeItemDataSource*>(dataItem);
					wxASSERT(item);
					if (is_tableBox == item->IsTableSection()) {
						ibVariantDataSource* variant = new ibVariantDataSource(typeFactory, item->GetID());
						variant->SetSourceDesc(item->GetSourceDesc());      // full path (length-1 for a top-level field)
						SetValue(variant);
					}
					else {
						dlg->Destroy();
						return false;
					}
				}
				else if (!dataItem) {
					dlg->Destroy();
					return false;
				}
			}

			dlg->Destroy();
			return res == wxID_OK
				&& selItem.IsOk();
		}

		bool ProcessTableColumn(wxPropertyGrid* pg, wxPGProperty* dlgProp, const ibBackendTypeSourceFactory* typeFactory, ibVariantDataSource* srcData) {

			const ibMetaData* metaData = typeFactory->GetMetaData();
			if (metaData == nullptr) return false;
			const ibSourceObject* typeSrc = typeFactory->GetSourceObject();
			if (typeSrc == nullptr) return false;

			// The PARENT tablebox as a source factory — its bound path prefixes every
			// column's path. (A column's GetSourceObject() is its owning tablebox.)
			const ibBackendTypeSourceFactory* parentFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(typeSrc);

			const ibClassID& clsid = typeSrc->GetSourceClassType();
			if (clsid == 0) return false;

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice source"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;

			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			wxTreeCtrl* tc = new wxTreeCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxSUNKEN_BORDER);

			tc->AssignImageList(GetSourceImageList());

			rowsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			tc->SetDoubleBuffered(true);
			tc->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
				dlg->Move(pg->GetGoodEditorDialogPosition(dlgProp, dlg->GetSize()));
			}

			tc->SetFocus();

			const ibCtorAbstractType* typeCtor = metaData->GetAvailableCtor(clsid);
			// HIDE_ROOT: a hidden root holds one TOP node per source — the bound table AND each header
			// object above it (Mode 2). A tree has a single real root, so sources are its children.
			const wxTreeItemId hiddenRoot = tc->AddRoot(wxEmptyString);
			std::vector<ibSourceHop> parentPath;    // the tablebox's own bound path [head, table]
			if (typeCtor != nullptr && typeCtor->IsTableValue()) {
				// GetSourceList vends (per #3) the current table AND the form's object attributes: the
				// BOUND table (its columns, row-relative) PLUS the HEADER object(s) above it (their
				// fields, constant per row — resolved on the front via the form, Mode 2). Each becomes a
				// root; both build their children from the source explorer (GetSourceExplorer). Table-vs-
				// object is told by clsid (IsTableType) — never a metaobject. The dot-walk is family-blind.
				std::vector<ibBackendFormAttributeValue*> holders;
				typeFactory->GetSourceList(holders);

				parentPath = parentFactory != nullptr ? parentFactory->GetSourceDesc().GetPath() : std::vector<ibSourceHop>{};
				const ibSourceId boundHead = parentFactory != nullptr ? parentFactory->GetSourceDesc().GetFirst() : wxNOT_FOUND;

					// The BOUND table the column lives in may be a NESTED tabular section (parentPath =
					// {head, ..., section}) — never a top-level form attribute, so it is absent from `holders`.
					// Walk the head holder's explorer down parentPath to that node and root its columns directly
					// (the row-relative fields this picker is FOR). Top-level list/section tables (size 1) are
					// rooted by the loop below via boundHead.
					if (parentPath.size() >= 2) {
						ibBackendFormAttributeValue* headHolder = typeFactory->FindSourceHolder(boundHead);
						ibSourceDataObject* headSrc = headHolder != nullptr ? headHolder->GetSourceValue() : nullptr;
						const ibSourceExplorer* boundNode = headSrc != nullptr ? headSrc->GetSourceExplorer() : nullptr;
						for (size_t i = 1; i < parentPath.size() && boundNode != nullptr; ++i)
							boundNode = boundNode->FindById(parentPath[i].m_id);
						if (boundNode != nullptr) {
							ibTreeItemDataSource* tableData = new ibTreeItemDataSource(boundNode->GetSourceSynonym(), wxNOT_FOUND, true);
							wxTreeItemId tableItem = tc->AppendItem(hiddenRoot, boundNode->GetSourceSynonym(), icon_table, icon_table, tableData);
							tc->SetItemBold(tableItem);
							AppendExplorerColumns(tc, tableItem, *boundNode, parentPath, metaData);
						}
					}

					std::vector<ibSourceId> seenRoots;   // GetSourceList is queried under two filters -> dedupe roots by attribute id

				for (ibBackendFormAttributeValue* holder : holders) {
					if (holder == nullptr)
						continue;
					bool alreadyRooted = false;
						for (const ibSourceId seen : seenRoots)
							if (seen == holder->GetId()) { alreadyRooted = true; break; }
						if (alreadyRooted)
							continue;   // GetSourceList vended this attribute under both filters
						seenRoots.push_back(holder->GetId());

						ibSourceDataObject* source = holder->GetSourceValue();
					if (source == nullptr) {
						// A PRIMITIVE form attribute (string / number / …) materialises no source, so it has no
						// sub-fields — but it IS selectable as a Mode-2 column: its whole value is constant per row.
						// Add it as a LEAF (its own id, no children) — the degenerate header-object case.
						ibTreeItemDataSource* leafData = new ibTreeItemDataSource(holder->GetName(), holder->GetId(), false);
						tc->AppendItem(hiddenRoot, holder->GetName(), icon_attribute, icon_attribute, leafData);
						continue;
					}
					const ibSourceExplorer* explorerPtr = source->GetSourceExplorer();
						if (explorerPtr == nullptr)
							continue;
						const ibSourceExplorer& explorer = *explorerPtr;

					if (IsTableType(metaData, holder->GetTypeDesc())) {
						// The BOUND table (the one this column's tablebox reads). Its columns are row-
						// relative, prefixed by the tablebox's own bound path. Skip any OTHER table.
						if (boundHead != wxNOT_FOUND && holder->GetId() != boundHead)
							continue;
						ibTreeItemDataSource* tableData = new ibTreeItemDataSource(holder->GetName(), wxNOT_FOUND, true);
						wxTreeItemId tableItem = tc->AppendItem(hiddenRoot, holder->GetName(), icon_table, icon_table, tableData);
						tc->SetItemBold(tableItem);
						AppendExplorerColumns(tc, tableItem, explorer, parentPath, metaData);
					}
					else {
						// A HEADER object ABOVE the table (Mode 2). Its fields are constant across the
						// table's rows; the column path roots at the header attribute id, and the renderer
						// resolves it through the form. The object node itself is not a column (id NOT_FOUND).
						ibTreeItemDataSource* headData = new ibTreeItemDataSource(holder->GetName(), wxNOT_FOUND, false);
						wxTreeItemId headItem = tc->AppendItem(hiddenRoot, holder->GetName(), icon_attribute, icon_attribute, headData);
						AppendExplorerColumns(tc, headItem, explorer, std::vector<ibSourceHop>{ { holder->GetId() } }, metaData);
					}
				}

				// Lazy reference expansion — the SAME handler as the scalar picker.
				tc->Bind(wxEVT_TREE_ITEM_EXPANDING, [tc, metaData](wxTreeEvent& evt) {
					ExpandReference(tc, metaData, dynamic_cast<ibTreeItemDataSource*>(tc->GetItemData(evt.GetItem())), evt.GetItem());
				});
			}

			// Expand every source root so its columns show; references stay collapsed ([+]) so a cyclic
			// ref can't blow the stack (no ExpandAll).
			wxTreeItemIdValue expandCookie;
			for (wxTreeItemId top = tc->GetFirstChild(hiddenRoot, expandCookie); top.IsOk(); top = tc->GetNextChild(hiddenRoot, expandCookie))
				tc->Expand(top);

			// Re-open: find the root whose prefix matches the saved path and descend to the leaf. The
			// tail start = that root's children-prefix length (read off a child — the bound table's bound
			// path, or a header object's single attribute id), so both root kinds re-open uniformly.
			if (srcData != nullptr && hiddenRoot.IsOk()) {
				const std::vector<ibSourceHop> chain = srcData->GetSourceDesc().GetPath();
				wxTreeItemIdValue rootCookie;
				for (wxTreeItemId top = tc->GetFirstChild(hiddenRoot, rootCookie); top.IsOk(); top = tc->GetNextChild(hiddenRoot, rootCookie)) {
					wxTreeItemIdValue childCookie;
					wxTreeItemId firstChild = tc->GetFirstChild(top, childCookie);
					// A top-level LEAF (a primitive attribute — no children): the node ITSELF is the source, its
						// own path is [id]. Select it when the saved chain matches (container roots descend below).
						if (!firstChild.IsOk()) {
							ibTreeItemDataSource* topData = dynamic_cast<ibTreeItemDataSource*>(tc->GetItemData(top));
							if (topData != nullptr && chain.size() == 1 && chain[0].m_id == topData->GetID()) {
								tc->SelectItem(top);
								tc->EnsureVisible(top);
								break;
							}
							continue;
						}
						ibTreeItemDataSource* fcData = dynamic_cast<ibTreeItemDataSource*>(tc->GetItemData(firstChild));
					if (fcData == nullptr)
						continue;
					const std::vector<ibSourceHop>& rootPrefix = fcData->GetPrefixPath();   // path TO this root's columns
					if (chain.size() <= rootPrefix.size())
						continue;
					bool prefixMatch = true;
					for (size_t k = 0; k < rootPrefix.size(); ++k)
						if (chain[k].m_id != rootPrefix[k].m_id) { prefixMatch = false; break; }
					if (!prefixMatch)
						continue;
					wxTreeItemId node = top;
					for (size_t i = rootPrefix.size(); i < chain.size(); ++i) {
						wxTreeItemId child = FindChildBySourceId(tc, node, chain[i].m_id);
						if (!child.IsOk())
							break;
						if (i + 1 < chain.size()) { tc->Expand(child); node = child; }
						else { tc->SelectItem(child); tc->EnsureVisible(child); }
					}
					break;   // matched root
				}
			}

			int res = dlg->ShowModal();

			wxTreeItemId selItem = tc->GetSelection();
			if (selItem.IsOk()) {
				wxTreeItemData* dataItem = tc->GetItemData(selItem);
				if (dataItem
					&& res == wxID_OK) {
					ibTreeItemDataSource* item = dynamic_cast<ibTreeItemDataSource*>(dataItem);
					wxASSERT(item);
					// Only a COLUMN (a real field id) is selectable — the root is the
					// parent table itself, not a column.
					if (item->GetID() == wxNOT_FOUND) {
						dlg->Destroy();
						return false;
					}
					// The node carries its FULL accumulated path — the tablebox prefix + the column +
					// any dot-walk hops: [head, table, column] for a plain column, [head, table,
					// column, field] for a reference field. Save it verbatim (the runtime resolves it).
					ibVariantDataSource* variant = new ibVariantDataSource(typeFactory, item->GetID());
					variant->SetSourceDesc(item->GetSourceDesc());
					SetValue(variant);
				}
				else if (dataItem == nullptr) {
					dlg->Destroy();
					return false;
				}
			}

			dlg->Destroy();
			return res == wxID_OK
				&& selItem.IsOk();
		}

	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGDataSourceProperty* dlgProp = wxDynamicCast(prop, ibPGDataSourceProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const ibBackendTypeSourceFactory* typeFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(dlgProp->GetPropertyObject());
			if (typeFactory == nullptr) return false;

			ibVariantDataSource* srcData = property_cast(dlgProp->GetValue(), ibVariantDataSource);
			if (typeFactory->GetFilterSourceDataType() == ibSourceDataType::ibSourceDataType_attribute || typeFactory->GetFilterSourceDataType() == ibSourceDataType::ibSourceDataType_table)
				return ProcessAttribute(pg, dlgProp, typeFactory, srcData);
			else if (typeFactory->GetFilterSourceDataType() == ibSourceDataType::ibSourceDataType_tableColumn)
				return ProcessTableColumn(pg, dlgProp, typeFactory, srcData);
			return false;
		}
	};

	return new ibPGEditorDataSourceDialogAdapter();
}