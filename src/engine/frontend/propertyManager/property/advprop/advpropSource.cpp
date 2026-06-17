#include "advpropSource.h"

#include "backend/propertyManager/property/variant/variantSource.h"
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

			wxTreeCtrl* tc = new wxTreeCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

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

			const ibSourceDataObject* srcObject = dynamic_cast<ibSourceDataObject*>(typeFactory->GetSourceObject());
			wxTreeItemId rootItem;   // hoisted: re-open positioning (below) walks the saved path from it
			if (srcObject != nullptr) {

				bool allow_create = true;

				const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(srcObject->GetSourceClassType());
				if (typeCtor != nullptr) {
					if (typeFactory->GetFilterSourceDataType() == ibSourceDataType::ibSourceDataType_attribute) {
						bool is_list_source = typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_List;
						allow_create = !is_list_source;
					}
				}

				const ibSourceExplorer& srcExplorer = srcObject->GetSourceExplorer();
				ibTreeItemDataSource* srcItemData = nullptr;
				if (typeFactory->FilterSource(srcExplorer, srcExplorer.GetSourceId())) {
					if (srcExplorer.IsSelect()) {
						srcItemData = new ibTreeItemDataSource(
							srcExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcExplorer.GetClsidList()) + wxT(")"), srcExplorer.GetSourceId(), srcExplorer.IsTableSection()
						);
					}
				}
				if (srcExplorer.IsTableSection()) {
					rootItem = tc->AddRoot(srcExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcExplorer.GetClsidList()) + wxT(")"), icon_table, icon_table, srcItemData);
				}
				else {
					rootItem = tc->AddRoot(srcExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcExplorer.GetClsidList()) + wxT(")"), icon_attribute, icon_attribute, srcItemData);
				}

				if (allow_create) {

					if (srcExplorer.GetSourceId() == dataSource) {
						tc->SelectItem(rootItem);
					}
					for (unsigned int idx = 0; idx < srcExplorer.GetHelperCount(); idx++) {
						const ibSourceExplorer& srcNextExplorer = srcExplorer.GetHelper(idx);
						// Reference attribute? Then this node is lazily expandable into the
						// referenced types' fields; remember ALL referenced types (composite).
						std::vector<const ibValueMetaObjectCompositeData*> refTypes = GetReferencedTypes(srcNextExplorer.GetMetaObject(), metaData);
						ibTreeItemDataSource* itemData = nullptr;
						if (typeFactory->FilterSource(srcNextExplorer, srcExplorer.GetSourceId())) {
							if (srcNextExplorer.IsSelect()) {
								itemData = new ibTreeItemDataSource(
									srcNextExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcNextExplorer.GetClsidList()) + wxT(")"),
									srcNextExplorer.GetSourceId(),
									srcNextExplorer.IsTableSection(),
									{}, refTypes
								);
							}
						}
						wxTreeItemId newItem = nullptr;
						if (srcNextExplorer.IsTableSection()) {
							newItem = tc->AppendItem(rootItem, srcNextExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcNextExplorer.GetClsidList()) + wxT(")"), icon_table, icon_table, itemData);
						}
						else {
							newItem = tc->AppendItem(rootItem, srcNextExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcNextExplorer.GetClsidList()) + wxT(")"), icon_attribute, icon_attribute, itemData);
						}
						if (srcNextExplorer.GetSourceId() == dataSource) {
							tc->SelectItem(newItem);
						}
						bool needDelete = itemData == nullptr;
						if (srcNextExplorer.IsTableSection()) {
							tc->SetItemBold(newItem);
							for (unsigned int i = 0; i < srcNextExplorer.GetHelperCount(); i++) {
								ibSourceExplorer srcColExplorer = srcNextExplorer.GetHelper(i);
								ibTreeItemDataSource* itemTableData = nullptr;
								if (typeFactory->FilterSource(srcColExplorer, srcNextExplorer.GetSourceId())) {
									itemTableData = new ibTreeItemDataSource(
										srcColExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcColExplorer.GetClsidList()) + wxT(")"),
										srcColExplorer.GetSourceId(),
										srcColExplorer.IsTableSection()
									);
									wxTreeItemId nextItem = tc->AppendItem(newItem, srcColExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcColExplorer.GetClsidList()) + wxT(")"), icon_attribute, icon_attribute, itemTableData);
									if (srcColExplorer.GetSourceId() == dataSource) {
										tc->SelectItem(nextItem);
									}
									needDelete = !typeFactory->FilterSource(srcNextExplorer, srcNextExplorer.GetSourceId());
								}
							}
						}

						if (!refTypes.empty() && itemData != nullptr) {
							tc->AppendItem(newItem, wxEmptyString);   // dummy child -> shows [+]; built lazily on expand
						}

						if (needDelete) {
							tc->Delete(newItem);
						}
					}

					if (srcExplorer.IsTableSection()) {
						tc->SetItemBold(rootItem);
					}

					tc->Expand(rootItem);
				}

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
			if (srcData != nullptr && rootItem.IsOk()) {
				const std::vector<ibMetaID>& chain = srcData->GetSourceDesc().GetPath();
				wxTreeItemId node = rootItem;
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

			const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);

			if (typeCtor != nullptr) {
				const ibValueMetaObjectCompositeData* metaObject = nullptr;
				if (typeCtor->ConvertToMetaValue(metaObject)) {
					ibTreeItemDataSource* srcItemData = new ibTreeItemDataSource(metaObject->GetName() + wxT(" (") + MakeTypeString(metaData, clsid) + wxT(")"), wxNOT_FOUND, true);
					const wxTreeItemId& rootItem = tc->AddRoot(metaObject->GetName() + wxT(" (") + MakeTypeString(metaData, clsid) + wxT(")"), icon_table, icon_table, srcItemData);
					for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
						if (!object->IsAllowed())
							continue;
						ibTreeItemDataSource* itemData = itemData = new ibTreeItemDataSource(
							object->GetName() + wxT(" (") + MakeTypeString(metaData, object->GetTypeDesc()) + wxT(")"),
							object->GetMetaID(),
							false
						);
						const wxTreeItemId& newItem = tc->AppendItem(rootItem, object->GetName() + wxT(" (") + MakeTypeString(metaData, object->GetTypeDesc()) + wxT(")"), icon_attribute, icon_attribute, itemData);
						if (dataSource == object->GetMetaID()) tc->SelectItem(newItem);
					}
				}
			}

			tc->ExpandAll(); int res = dlg->ShowModal();

			wxTreeItemId selItem = tc->GetSelection();
			if (selItem.IsOk()) {
				wxTreeItemData* dataItem = tc->GetItemData(selItem);
				if (dataItem
					&& res == wxID_OK) {
					ibTreeItemDataSource* item = dynamic_cast<ibTreeItemDataSource*>(dataItem);
					wxASSERT(item);
					SetValue(new ibVariantDataSource(typeFactory, item->GetID()));
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