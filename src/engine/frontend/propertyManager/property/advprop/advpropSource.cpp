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
		public:
			ibTreeItemDataSource(const wxString& nameProp, const ibMetaID& id, bool tableSection) : wxTreeItemData(), m_nameProp(nameProp), m_id(id), m_tableSection(tableSection) {};

			const wxString& GetPropName() const { return m_nameProp; }
			const ibMetaID& GetID() const { return m_id; }
			const bool IsTableSection() const { return m_tableSection; }
		};

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
				wxTreeItemId rootItem = nullptr;
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
						ibTreeItemDataSource* itemData = nullptr;
						if (typeFactory->FilterSource(srcNextExplorer, srcExplorer.GetSourceId())) {
							if (srcNextExplorer.IsSelect()) {
								itemData = new ibTreeItemDataSource(
									srcNextExplorer.GetSourceName() + wxT(" (") + MakeTypeString(typeFactory->GetMetaData(), srcNextExplorer.GetClsidList()) + wxT(")"),
									srcNextExplorer.GetSourceId(),
									srcNextExplorer.IsTableSection()
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

			tc->ExpandAll(); int res = dlg->ShowModal();

			wxTreeItemId selItem = tc->GetSelection();
			if (selItem.IsOk()) {
				wxTreeItemData* dataItem = tc->GetItemData(selItem);
				if (dataItem != nullptr && res == wxID_OK) {
					ibTreeItemDataSource* item = dynamic_cast<ibTreeItemDataSource*>(dataItem);
					wxASSERT(item);
					if (is_tableBox == item->IsTableSection()) {
						SetValue(new ibVariantDataSource(typeFactory, item->GetID()));
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