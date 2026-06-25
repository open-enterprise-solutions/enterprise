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
		const ibMetaID& id = dataSource->GetSource();
		if (id != wxNOT_FOUND) m_typeSelector->SetValue(dataSource->CloneSourceAttribute(id));
		else m_typeSelector->SetValue(dataSource->CloneSourceAttribute());
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
			const ibMetaID m_id;
			const bool m_tableSection;
			const std::vector<ibMetaID> m_prefixPath;           // ancestor metaId chain (empty = top level)
			const std::vector<const ibValueMetaObjectCompositeData*> m_refTypes;  // ALL referenced types (composite) => lazily expandable
			bool m_loaded = false;                              // referenced children already built?
		public:
			ibTreeItemDataSource(const wxString& nameProp, const ibMetaID& id, bool tableSection,
				std::vector<ibMetaID> prefixPath = {},
				std::vector<const ibValueMetaObjectCompositeData*> refTypes = {})
				: wxTreeItemData(), m_nameProp(nameProp), m_id(id), m_tableSection(tableSection),
				m_prefixPath(std::move(prefixPath)), m_refTypes(std::move(refTypes)) {};

			const wxString& GetPropName() const { return m_nameProp; }
			const ibMetaID& GetID() const { return m_id; }
			const bool IsTableSection() const { return m_tableSection; }
			const std::vector<ibMetaID>& GetPrefixPath() const { return m_prefixPath; }
			const std::vector<const ibValueMetaObjectCompositeData*>& GetRefTypes() const { return m_refTypes; }
			bool HasRef() const { return !m_refTypes.empty(); }
			bool IsLoaded() const { return m_loaded; }
			void SetLoaded() { m_loaded = true; }

			// A node's children inherit its full chain: ancestor prefix + this node's own id.
			std::vector<ibMetaID> ChildPrefix() const {
				std::vector<ibMetaID> child = m_prefixPath;
				child.push_back(m_id);
				return child;
			}

			// The whole binding address for this node: ancestor prefix + this node (leaf).
			ibSourceDescription GetSourceDesc() const {
				ibSourceDescription desc(m_prefixPath);
				desc.AppendSource(m_id);
				return desc;
			}
		};

		// A reference attribute -> the metaobjects of EVERY type it can point at. A composite
		// attribute has several reference types; the picker expands all of them so the user can
		// reach a field of any branch (each field keeps its own type-specific metaId, so the
		// runtime renders it only when the live value is of that type).
		static std::vector<const ibValueMetaObjectCompositeData*> GetReferencedTypes(const ibValueMetaObject* attrMeta, const ibMetaData* metaData) {
			std::vector<const ibValueMetaObjectCompositeData*> types;
			const ibValueMetaObjectAttributeBase* attr = dynamic_cast<const ibValueMetaObjectAttributeBase*>(attrMeta);
			if (attr == nullptr || metaData == nullptr) return types;
			for (const ibClassID& clsid : attr->GetTypeDesc().GetClsidList()) {
				const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
				if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference) {
					const ibValueMetaObjectCompositeData* refType = dynamic_cast<const ibValueMetaObjectCompositeData*>(typeCtor->GetMetaObject());
					if (refType != nullptr) types.push_back(refType);
				}
			}
			return types;
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

		// Find the child node of `parent` whose stored metaId matches — used to walk a
		// saved dotted path on re-open and land on the same leaf.
		static wxTreeItemId FindChildByMetaID(wxTreeCtrl* tc, const wxTreeItemId& parent, const ibMetaID& id) {
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

			// Each form attribute is the GATE: path[0] = attribute id, then the binding
			// walks the attribute's TYPE METADATA (clsid → metaobject) — NO transient
			// source object (GetSourceExplorer is only for default-control layout, not
			// for picking sources). Display uses the attribute's REAL name + Type.
			struct PickEntry { ibSourceId head; ibBackendFormAttributeValue* holder; wxString name; };
			std::vector<PickEntry> entries;
			std::vector<ibBackendFormAttributeValue*> holders;
			typeFactory->GetSourceList(holders);
			for (ibBackendFormAttributeValue* holder : holders) {
				if (holder != nullptr)
					entries.push_back({ holder->GetAttributeId(), holder, holder->GetAttributeName() });
			}

			const wxTreeItemId hiddenRoot = tc->AddRoot(wxEmptyString);   // one hidden root; sources are its children
			wxTreeItemId rootItem;   // hoisted: re-open positioning (below) walks the saved path from it
			for (const auto& entry : entries) {
				const ibSourceId headAttrId = entry.head;
				const std::vector<ibSourceId> basePrefix{ headAttrId };   // every hop under this attribute leads with its id
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

				if (is_tableBox) {
					// TABLEBOX: the type's tabular sections, each a selectable table
					// (path [attr, table]). Columns are picked separately (tableColumn).
					// Tabular sections live on a RECORD metaobject (object types), not on
					// the plain composite — a metadata-kind downcast, like GetReferencedTypes.
					if (const ibValueMetaObjectRecordData* record = dynamic_cast<const ibValueMetaObjectRecordData*>(metaObject)) {
						for (ibValueMetaObjectTableData* table : record->GetGenericTableArrayObject()) {
							if (table->IsDeleted()) continue;
							const wxString tableLabel = table->GetSynonym();
							ibTreeItemDataSource* tableData = new ibTreeItemDataSource(tableLabel, table->GetMetaID(), true, basePrefix);
							wxTreeItemId tableItem = tc->AppendItem(rootItem, tableLabel, icon_table, icon_table, tableData);
							tc->SetItemBold(tableItem);
							if (table->GetMetaID() == dataSource) tc->SelectItem(tableItem);
						}
					}
				}
				else {
					// SCALAR control: the type's attributes; a reference expands lazily
					// into the referenced type's fields (read-only by the dot rule). Each field is
					// SEPARATED into table vs attribute by the class factory (IsTableValue on its
					// Type's ctor) — a table-typed field (list / dynamic list) gets the table icon
					// and the tableSection flag, a scalar field the attribute icon.
					for (ibValueMetaObjectAttributeBase* field : metaObject->GetGenericAttributeArrayObject()) {
						if (field->IsDeleted()) continue;
						const wxString label = field->GetSynonym() + wxT(" (") + MakeTypeString(metaData, field->GetTypeDesc()) + wxT(")");
						std::vector<const ibValueMetaObjectCompositeData*> refTypes = GetReferencedTypes(field, metaData);
						const bool fieldIsTable = IsTableType(metaData, field->GetTypeDesc());
						const int fieldIcon = fieldIsTable ? icon_table : icon_attribute;
						ibTreeItemDataSource* fieldData = new ibTreeItemDataSource(label, field->GetMetaID(), fieldIsTable, basePrefix, refTypes);
						wxTreeItemId fieldItem = tc->AppendItem(rootItem, label, fieldIcon, fieldIcon, fieldData);
						if (fieldIsTable) tc->SetItemBold(fieldItem);
						if (!refTypes.empty()) tc->AppendItem(fieldItem, wxEmptyString);   // dummy child → [+]; built lazily on expand
						if (field->GetMetaID() == dataSource) tc->SelectItem(fieldItem);
					}
				}
				tc->Expand(rootItem);
			}

			// Lazy dot-expansion. A reference node carries a single dummy child (the [+]);
			// on its first expand we drop the dummy and build the referenced type's fields,
			// each carrying the accumulated guid chain. NEVER expand references eagerly — a
			// self / cyclic reference (A.B.A...) would otherwise blow the stack (hence no
			// ExpandAll: only the root level is shown up front, the rest opens on demand).
			tc->Bind(wxEVT_TREE_ITEM_EXPANDING, [tc, metaData](wxTreeEvent& evt) {
				wxTreeItemId node = evt.GetItem();
				ibTreeItemDataSource* data = dynamic_cast<ibTreeItemDataSource*>(tc->GetItemData(node));
				if (data == nullptr || !data->HasRef() || data->IsLoaded())
					return;
				data->SetLoaded();
				tc->DeleteChildren(node);                          // drop the dummy

				const std::vector<ibMetaID> childPrefix = data->ChildPrefix();
				const bool multi = data->GetRefTypes().size() > 1;            // composite: disambiguate fields by type
					for (const ibValueMetaObjectCompositeData* refType : data->GetRefTypes()) {
						if (refType == nullptr)
							continue;
						for (ibValueMetaObjectAttributeBase* attr : refType->GetGenericAttributeArrayObject()) {
					if (attr->IsDeleted())
						continue;
					const wxString label = multi ? attr->GetSynonym() + wxT(" - ") + refType->GetSynonym() : attr->GetSynonym();
						std::vector<const ibValueMetaObjectCompositeData*> childRefTypes = GetReferencedTypes(attr, metaData);
					ibTreeItemDataSource* childData = new ibTreeItemDataSource(
						label, attr->GetMetaID(), false, childPrefix, childRefTypes);
					wxTreeItemId childItem = tc->AppendItem(node, label, icon_attribute, icon_attribute, childData);
					if (!childRefTypes.empty())
						tc->AppendItem(childItem, wxEmptyString);   // dummy -> [+] for the next level
						} // close inner attr loop (outer refType loop closed by the next brace)
				}
			});

			// Re-open positioning: walk the saved metaId path (first hop .. leaf), expanding
			// each reference node (programmatic Expand fires the lazy build above) and
			// selecting the leaf — the user lands on it. A length-1 path lands on a top field.
			if (srcData != nullptr && hiddenRoot.IsOk()) {
				const std::vector<ibMetaID>& chain = srcData->GetSourceDesc().GetPath();
				// Walk from the hidden root: chain[0] (head attribute id) selects the
				// source node, the rest descends into its fields.
				wxTreeItemId node = hiddenRoot;
				for (size_t i = 0; i < chain.size(); ++i) {
					wxTreeItemId child = FindChildByMetaID(tc, node, chain[i]);
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

			const ibMetaID& dataSource = srcData != nullptr ? srcData->GetSource() : wxNOT_FOUND;

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
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxSUNKEN_BORDER);

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
			if (typeCtor != nullptr && typeCtor->IsTableValue()) {
				// Columns come from the bound table's SOURCE, reached through the holder
				// — attribute + value together. The source vends its own columns
				// (GetSourceExplorer), uniform across catalog / register / data-processor table /
				// dynamic list: no metaobject-vs-queryable branching here. The parent tablebox
				// binds ONE of the form's table sources — its source-desc head selects it.
				std::vector<ibBackendFormAttributeValue*> holders;
				typeFactory->GetSourceList(holders);

				const std::vector<ibMetaID> parentPath = parentFactory != nullptr ? parentFactory->GetSourceDesc().GetPath() : std::vector<ibMetaID>{};
				const ibMetaID boundHead = !parentPath.empty() ? parentPath.front() : wxNOT_FOUND;

				for (ibBackendFormAttributeValue* holder : holders) {
					if (holder == nullptr)
						continue;
					if (boundHead != wxNOT_FOUND && holder->GetAttributeId() != boundHead)
						continue;   // only the table this column's tablebox is bound to
					ibSourceDataObject* source = holder->GetSourceValue();
					if (source == nullptr)
						continue;
					const ibSourceExplorer explorer = source->GetSourceExplorer();
					ibTreeItemDataSource* srcItemData = new ibTreeItemDataSource(holder->GetAttributeName(), wxNOT_FOUND, true);
					const wxTreeItemId& rootItem = tc->AddRoot(holder->GetAttributeName(), icon_table, icon_table, srcItemData);
					for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
						const ibSourceExplorer col = explorer.GetHelper(i);
						ibTreeItemDataSource* itemData = new ibTreeItemDataSource(col.GetSourceName(), col.GetSourceId(), false);
						const wxTreeItemId& newItem = tc->AppendItem(rootItem, col.GetSourceName(), icon_attribute, icon_attribute, itemData);
						if (dataSource == col.GetSourceId()) tc->SelectItem(newItem);
					}
					break;   // one table (usually one source)
				}
			}

			tc->ExpandAll();
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
					// Column path = the PARENT tablebox's own bound path + this column id,
					// so the runtime walks [head, table, column] (the parent owns [head, table]).
					ibSourceDescription desc = parentFactory != nullptr ? parentFactory->GetSourceDesc() : ibSourceDescription();
					desc.AppendSource(item->GetID());
					ibVariantDataSource* variant = new ibVariantDataSource(typeFactory, item->GetID());
					variant->SetSourceDesc(desc);
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