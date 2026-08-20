#include "advpropType.h"

#include "backend/propertyManager/property/propertyType.h"
#include "backend/propertyManager/property/variant/variantType.h"

#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_* — the grid flags
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGTypeProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGTypeProperty, wxStringProperty, ComboBoxAndButton)

// register frontend property 
class ibPropertyTypeLoader
{
public:
	ibPropertyTypeLoader()
	{
		// The five-parameter slot dissolves: owner and the type filter were always the
		// property's own, the slot just carried them across.
		ibPropertyRegistry::Register([](ibPropertyType* prop) -> wxPGProperty* {
			return new ibPGTypeProperty(prop->GetPropertyObject(), prop->GetFilterDataType(),
				prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
}g_typeLoader;

wxPGChoices ibPGTypeProperty::GetDateTime()
{
	wxPGChoices choices;
	choices.Add(_("Date"), ibDateFractions::ibDateFractions_Date);
	choices.Add(_("Date and time"), ibDateFractions::ibDateFractions_DateTime);
	choices.Add(_("Time"), ibDateFractions::ibDateFractions_Time);
	return choices;
}

#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibPGTypeProperty::FillByClsid(const ibSelectorDataType& selectorDataType, const ibClassID& clsid)
{
	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
	wxASSERT(so);
	if (so->GetObjectTypeCtor() == ibCtorObjectType::ibCtorObjectType_object_metadata) {
		const ibMetaData* metaData = dynamic_cast<const ibBackendTypeConfigFactory*>(m_ownerProperty)->GetMetaData();
		wxASSERT(metaData);
		if (metaData != nullptr) {
			// Every branch adds the metaobject ctors of a kind identically (name + icon → choice,
			// value→clsid map); only the SET of kinds differs by selector.
			auto addKind = [&](ibCtorObjectMetaType kind) {
				for (auto ctor : metaData->GetListCtorsByType(clsid, kind)) {
					auto choice = m_choices.Add(ctor->GetClassName(), ctor->GetMetaObject()->GetIcon());
					m_valChoices.insert_or_assign(choice.GetValue(), ctor->GetClassType());
				}
			};
			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_reference) {
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic);
			}
			else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
			}
			else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
				// Attributes (filter = any) accept EVERY kind, including list / collection types.
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_List);
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager);
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic);
			}
		}
	}
	else {
		auto choice = m_choices.Add(so->GetClassName(), so->GetClassIcon());
		m_valChoices.insert_or_assign(
			choice.GetValue(), so->GetClassType()
		);
	}
}

#include "backend/system/value/valueTable.h"
#include "backend/system/value/valueDynamicList.h"   // g_valueDynamicListCLSID
#include "backend/system/value/valueDataComposition.h"  // g_valueDataCompositionCLSID
#include "backend/system/value/valueSpreadsheet.h"       // g_valueSpreadsheetCLSID — what a gridbox shows

ibPGTypeProperty::ibPGTypeProperty(const ibPropertyObject* property, const ibSelectorDataType& selectorDataType, const wxString& label, const wxString& strName, const wxVariant& value) :
	wxPGProperty(label, strName), m_ownerProperty(property)
{
	m_precision = new wxUIntProperty(_("Precision"), wxT("precision"), 0);
	AddPrivateChild(m_precision);
	m_scale = new wxUIntProperty(_("Scale"), wxT("scale"), 0);
	AddPrivateChild(m_scale);
	{ wxPGChoices dtChoices = GetDateTime();
	m_date_time = new wxEnumProperty(_("Date time"), wxT("date_time"), dtChoices, ibDateFractions::ibDateFractions_Date); }
	AddPrivateChild(m_date_time);
	m_length = new wxUIntProperty(_("Length"), wxT("length"), 0);
	AddPrivateChild(m_length);

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
		FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_EMPTY));
	}

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any || selectorDataType == ibSelectorDataType::ibSelectorDataType_boolean || selectorDataType == ibSelectorDataType::ibSelectorDataType_reference) {
		if (selectorDataType == ibSelectorDataType::ibSelectorDataType_boolean) {
			FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN));
			FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		}
		else {
			FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN));
			FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
			FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
			FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
		}
	}
	else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_resource) {
		FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
	}

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
		FillByClsid(selectorDataType, ibValue::GetIDByVT(ibValueTypes::TYPE_NULL));
	}

	/////////////////////////////////////////////////

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table ||
		selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
		FillByClsid(selectorDataType, g_valueTableCLSID);
		// Unified dynamic list — selectable as an attribute type alongside Table.
		FillByClsid(selectorDataType, g_valueDynamicListCLSID);
		// Data composer — a list's sibling: a source plus a query plus the fold a user edits.
		FillByClsid(selectorDataType, g_valueDataCompositionCLSID);
		// A SPREADSHEET DOCUMENT is an attribute type as well — it is what a GRIDBOX shows. Creating
		// the control creates the variable; naming it here is what lets a form declare one on its own
		// and point several controls (or a composition's Compose) at the same document.
		FillByClsid(selectorDataType, g_valueSpreadsheetCLSID);
	}

	/////////////////////////////////////////////////

	FillByClsid(selectorDataType, g_metaCatalogCLSID);
	FillByClsid(selectorDataType, g_metaDocumentCLSID);
	FillByClsid(selectorDataType, g_metaEnumerationCLSID);
	FillByClsid(selectorDataType, g_metaChartOfCharacteristicTypesCLSID);
	FillByClsid(selectorDataType, g_metaChartOfAccountsCLSID);

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
		FillByClsid(selectorDataType, g_metaDataProcessorCLSID);
		FillByClsid(selectorDataType, g_metaReportCLSID);
	}

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {
		FillByClsid(selectorDataType, g_metaInformationRegisterCLSID);
		FillByClsid(selectorDataType, g_metaAccumulationRegisterCLSID);
		FillByClsid(selectorDataType, g_metaAccountingRegisterCLSID);
	}

	SetValue(value);

	//m_flags |= wxPGFlags::ReadOnly;
	m_flags |= wxPGPropertyFlags_ActiveButton;
}

bool ibPGTypeProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	ibVariantDataAttribute* dataType = property_cast(value, ibVariantDataAttribute);
	if (dataType != nullptr) {
		ibVariantDataAttribute* newType = dataType->Clone();
		wxASSERT(newType);
		ibTypeDescription& td = newType->GetTypeDesc();
		td.SetDefaultMetaType(m_valChoices.at(number));
		value = newType;
		return true;
	}
	return false;
}

wxVariant ibPGTypeProperty::ChildChanged(wxVariant& thisValue, int childIndex, wxVariant& childValue) const
{
	ibVariantDataAttribute* dataType = property_cast(thisValue, ibVariantDataAttribute);
	if (dataType != nullptr) {
		ibVariantDataAttribute* newType = dataType->Clone();
		wxASSERT(newType);
		ibTypeDescription& td = newType->GetTypeDesc();
		if (childIndex == 0 || childIndex == 1) {
			long precision = (childIndex == 0)
				? childValue : m_precision->GetValue(),
				scale = (childIndex == 1)
				? childValue : m_scale->GetValue();
			if (precision > MAX_PRECISION_NUMBER) {
				precision = m_precision->GetValue();
				scale = m_scale->GetValue();
			}
			else if (precision == 0 || precision < scale) {
				precision = m_precision->GetValue();
				scale = m_scale->GetValue();
			}
			td.SetNumber(precision, scale);
		}
		else if (childIndex == 2) {
			long dateTime = childValue;
			td.SetDate((ibDateFractions)dateTime);
		}
		else if (childIndex == 3) {
			long length = childValue;
			if (length > MAX_LENGTH_STRING) {
				length = m_length->GetValue();
			}
			td.SetString(length);
		}
		return newType;
	}

	return wxNullVariant;
}

void ibPGTypeProperty::RefreshChildren()
{
	ibVariantDataAttribute* varData = property_cast(m_value, ibVariantDataAttribute);

	if (varData != nullptr) {
		const ibTypeDescription& td = varData->GetTypeDesc();
		if (td.GetClsidCount() < 2) {
			ibValueTypes id = ibValue::GetVTByID(td.GetFirstClsid());
			if (id == ibValueTypes::TYPE_NUMBER) {
				m_precision->Hide(false);
				m_precision->SetExpanded(true);
				m_scale->Hide(false);
				m_scale->SetExpanded(true);
				m_date_time->Hide(true);
				m_date_time->SetExpanded(false);
				m_length->Hide(true);
				m_length->SetExpanded(false);
			}
			else if (id == ibValueTypes::TYPE_DATE) {
				m_precision->Hide(true);
				m_precision->SetExpanded(false);
				m_scale->Hide(true);
				m_scale->SetExpanded(false);
				m_date_time->Hide(false);
				m_precision->SetExpanded(true);
				m_length->Hide(true);
				m_length->SetExpanded(false);
			}
			else if (id == ibValueTypes::TYPE_STRING) {
				m_precision->Hide(true);
				m_precision->SetExpanded(false);
				m_scale->Hide(true);
				m_scale->SetExpanded(false);
				m_date_time->Hide(true);
				m_date_time->SetExpanded(false);
				m_length->Hide(false);
				m_length->SetExpanded(true);
			}
			else {
				m_precision->Hide(true);
				m_precision->SetExpanded(false);
				m_scale->Hide(true);
				m_scale->SetExpanded(false);
				m_date_time->Hide(true);
				m_date_time->SetExpanded(false);
				m_length->Hide(true);
				m_length->SetExpanded(false);
			}
		}
		else {
			m_precision->Hide(true);
			m_precision->SetExpanded(false);
			m_scale->Hide(true);
			m_scale->SetExpanded(false);
			m_date_time->Hide(true);
			m_date_time->SetExpanded(false);
			m_length->Hide(true);
			m_length->SetExpanded(false);
		}

		for (unsigned int idx = 0; idx < td.GetClsidCount(); idx++) {
			ibValueTypes id = ibValue::GetVTByID(td.GetByIdx(idx));
			if (id == ibValueTypes::TYPE_NUMBER) {
				m_precision->SetValue(td.GetPrecision());
				m_scale->SetValue(td.GetScale());
			}
			else if (id == ibValueTypes::TYPE_DATE) {
				m_date_time->SetValue(td.GetDateFraction());
			}
			else if (id == ibValueTypes::TYPE_STRING) {
				m_length->SetValue(td.GetLength());
			}
		}
	}
	else {
		m_precision->Hide(true);
		m_precision->SetExpanded(false);
		m_scale->Hide(true);
		m_scale->SetExpanded(false);
		m_date_time->Hide(true);
		m_date_time->SetExpanded(false);
		m_length->Hide(true);
		m_length->SetExpanded(false);
	}

	ibPGTypeProperty::SetExpanded(true);
}

#include <wx/spinctrl.h>

#include "frontend/win/ctrls/checktree.h"
#include "frontend/win/dlgs/typeSelector.h"   // the shared picker — this editor is one of its two callers

wxPGEditorDialogAdapter* ibPGTypeProperty::GetEditorDialog() const
{
	class ibPGEditorTypeDialogAdapter : public wxPGEditorDialogAdapter {

		class ibTreeItemPropertyData : public wxTreeItemData {
			const ibCtorAbstractType* m_typeCtor;
		public:
			ibTreeItemPropertyData(const ibCtorAbstractType* typeCtor) : wxTreeItemData(), m_typeCtor(typeCtor) {}
			ibClassID GetClassType() const { return m_typeCtor->GetClassType(); }
			const ibCtorAbstractType* GetTypeCtor() const { return m_typeCtor; }
		};

		void FillByClsid(const ibClassID& clsid,
			ibCheckTree* tc, ibVariantDataAttribute* data, bool allowEdit) {

			wxImageList* imageList = tc->GetImageList();
			wxASSERT(imageList);
			const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
			const int groupIcon = imageList->Add(so->GetClassIcon());

			ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
			wxTreeItemId newItem = tc->AppendItem(tc->GetRootItem(), so->GetClassName(),
				groupIcon, groupIcon,
				itemData);

			if (data != nullptr) {
				const ibTypeDescription& td = data->GetTypeDesc();
				tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
				tc->Check(newItem, td.ContainType(so->GetClassType()));
			}
			else {
				tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
				tc->Check(newItem, false);
			}
		}

		void FillByClsid(const ibMetaData* metaData, const ibClassID& clsid,
			ibCheckTree* tc, ibVariantDataAttribute* data, bool allowEdit) {

			wxImageList* imageList = tc->GetImageList();
			wxASSERT(imageList);
			if (metaData != nullptr && metaData->IsRegisterCtor(clsid)) {
				const ibCtorAbstractType* so = metaData ? metaData->GetAvailableCtor(clsid) : ibValue::GetAvailableCtor(clsid);
				const int groupIcon = imageList->Add(so->GetClassIcon());

				ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
				wxTreeItemId newItem = tc->AppendItem(tc->GetRootItem(), so->GetClassName(),
					groupIcon, groupIcon,
					itemData);

				if (data != nullptr) {
					const ibTypeDescription& td = data->GetTypeDesc();
					tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
					tc->Check(newItem, td.ContainType(so->GetClassType()));
				}
				else {
					tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
					tc->Check(newItem, false);
				}
			}
		}

		void FillByClsid(ibSelectorDataType selectorDataType, const ibMetaData* metaData, const ibClassID& clsid,
			ibCheckTree* tc, ibVariantDataAttribute* data, bool allowEdit) {

			wxImageList* imageList = tc->GetImageList();
			wxASSERT(imageList);
			if (metaData != nullptr && metaData->IsRegisterCtor(clsid)) {
				const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
				if (selectorDataType == ibSelectorDataType::ibSelectorDataType_reference) {

					const int groupIcon = imageList->Add(so->GetClassIcon());
					const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName() + wxT("Ref"),
						groupIcon, groupIcon);

					for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
						const ibValueMetaObjectRecordDataRef* registerData = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
						{
							int icon = imageList->Add(registerData->GetIcon());
							ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
							wxTreeItemId newItem = tc->AppendItem(parentID, registerData->GetName(),
								icon, icon,
								itemData);

							if (data != nullptr) {
								const ibTypeDescription& td = data->GetTypeDesc();
								tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
								tc->Check(newItem, td.ContainType(so->GetClassType()));
							}
							else {
								tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
								tc->Check(newItem, false);
							}
						}
					}

					if (so->GetClassType() == g_metaChartOfCharacteristicTypesCLSID) {

						const int groupIcon = imageList->Add(so->GetClassIcon());
						const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), wxT("Characteristic"),
							groupIcon, groupIcon);

						for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic)) {
							const ibValueMetaObjectRecordDataRef* registerData = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
							{
								int icon = imageList->Add(registerData->GetIcon());
								ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
								wxTreeItemId newItem = tc->AppendItem(parentID, registerData->GetName(),
									icon, icon,
									itemData);

								if (data != nullptr) {
									const ibTypeDescription& td = data->GetTypeDesc();
									tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, td.ContainType(so->GetClassType()));
								}
								else {
									tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, false);
								}
							}
						}
					}
				}
				else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {

					const int groupIcon = imageList->Add(so->GetClassIcon());
					const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName() + wxT("List"),
						groupIcon, groupIcon);

					for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_List)) {
						const ibValueMetaObjectGenericData* registerData = dynamic_cast<const ibValueMetaObjectGenericData*>(so->GetMetaObject());
						{
							int icon = imageList->Add(registerData->GetIcon());
							ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
							wxTreeItemId newItem = tc->AppendItem(parentID, registerData->GetName(),
								icon, icon,
								itemData);

							if (data != nullptr) {
								const ibTypeDescription& td = data->GetTypeDesc();
								tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
								tc->Check(newItem, td.ContainType(so->GetClassType()));
							}
							else {
								tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
								tc->Check(newItem, false);
							}
						}
					}
				}
				else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {

					// List / collection group (e.g. CatalogList) — attributes (filter = any) accept
					// list types too, not only the table filter. Mirrors the _table branch above.
					{
						const auto listCtors = metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_List);
						if (!listCtors.empty()) {
							int groupIcon = imageList->Add(so->GetClassIcon());
							const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName() + wxT("List"),
								groupIcon, groupIcon);

							for (auto so : listCtors) {
								const ibValueMetaObjectGenericData* registerData = dynamic_cast<const ibValueMetaObjectGenericData*>(so->GetMetaObject());
								int icon = imageList->Add(registerData->GetIcon());
								ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
								wxTreeItemId newItem = tc->AppendItem(parentID, registerData->GetName(), icon, icon, itemData);
								if (data != nullptr) {
									const ibTypeDescription& td = data->GetTypeDesc();
									tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, td.ContainType(so->GetClassType()));
								}
								else {
									tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, false);
								}
							}
						}
					}

					if (so->GetClassType() != g_metaEnumerationCLSID) {

						int groupIcon = imageList->Add(so->GetClassIcon());
						const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName() + wxT("Object"),
							groupIcon, groupIcon);

						for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Object)) {
							const ibValueMetaObjectRecordData* registerData = dynamic_cast<const ibValueMetaObjectRecordData*>(so->GetMetaObject());
							{
								int icon = imageList->Add(registerData->GetIcon());
								ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
								wxTreeItemId newItem = tc->AppendItem(parentID, registerData->GetName(),
									icon, icon,
									itemData);

								if (data != nullptr) {
									const ibTypeDescription& td = data->GetTypeDesc();
									tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, td.ContainType(so->GetClassType()));
								}
								else {
									tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, false);
								}
							}
						}
					}
					
					if (so->GetClassType() != g_metaDataProcessorCLSID && so->GetClassType() != g_metaReportCLSID)
					{
						int groupIcon = imageList->Add(so->GetClassIcon());
						const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName() + wxT("Ref"),
							groupIcon, groupIcon);

						for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
							const ibValueMetaObjectRecordDataRef* registerData = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
							{
								int icon = imageList->Add(registerData->GetIcon());
								ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(so);
								wxTreeItemId newItem = tc->AppendItem(parentID, registerData->GetName(),
									icon, icon,
									itemData);

								if (data != nullptr) {
									const ibTypeDescription& td = data->GetTypeDesc();
									tc->SetItemState(newItem, td.ContainType(so->GetClassType()) ? allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED : allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, td.ContainType(so->GetClassType()));
								}
								else {
									tc->SetItemState(newItem, allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
									tc->Check(newItem, false);
								}
							}
						}
					}
				}
			}
		}

	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGTypeProperty* dlgProp = wxDynamicCast(prop, ibPGTypeProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const ibBackendTypeConfigFactory* typeFactory = dynamic_cast<const ibBackendTypeConfigFactory*>(dlgProp->GetPropertyObject());
			if (typeFactory == nullptr) return false;

			ibVariantDataAttribute* data = property_cast(dlgProp->GetValue(), ibVariantDataAttribute);
			if (data == nullptr) return false;

			// WHICH SHAPE — the only question this editor still answers. Everything that belongs to
			// that shape, and how it is grouped, is the picker's business.
			const ibSelectorDataType& selectorDataType = typeFactory->GetFilterDataType();

			// SINGLE OR COMPOSITE follows the same declaration the old in-place tree read: a table
			// slot takes one type, everything else may be composite.
			const bool singleChoice = selectorDataType == ibSelectorDataType::ibSelectorDataType_table;

			// ROUTED TO THE SHARED PICKER — win/dlgs/typeSelector, the same dialog the data side
			// reaches through its Select button. This editor says only WHICH SHAPE to render; what
			// belongs to that shape the picker works out from the registry, and no filter narrows it
			// here because a metadata declaration may name any type the configuration has.

			ibVariantDataAttribute* clone = data->Clone();

			if (!ibShowTypeSelector(pg, selectorDataType, std::vector<ibClassID>(), clone->GetTypeDesc(),
				typeFactory->GetMetaData(), !dlgProp->HasFlag(wxPGFlags::ReadOnly), singleChoice))
				return false;

			SetValue(clone);
			return true;
		}
	};

	return new ibPGEditorTypeDialogAdapter();
}