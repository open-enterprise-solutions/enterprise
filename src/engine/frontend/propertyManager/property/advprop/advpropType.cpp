#include "advpropType.h"

#include "backend/propertyManager/property/propertyType.h"
#include "backend/propertyManager/property/variant/variantType.h"

#include "frontend/propertyManager/property/private/prop.h"
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
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGTypeProperty, ibPropertyType::ms_propertyType);
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

#include "backend/metadata.h"
#include "backend/objCtor.h"

void ibPGTypeProperty::FillByClsid(const ibSelectorDataType& selectorDataType, const ibClassID& clsid)
{
	const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
	wxASSERT(so);
	if (so->GetObjectTypeCtor() == ibCtorObjectType::ibCtorObjectType_object_metadata) {
		const ibMetaData* metaData = dynamic_cast<const ibBackendTypeConfigFactory*>(m_ownerProperty)->GetMetaData();
		wxASSERT(metaData);
		if (metaData != nullptr) {
			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_reference) {
				for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
					auto metaObject = so->GetMetaObject();
					auto choice = m_choices.Add(so->GetClassName(), metaObject->GetIcon());
					m_valChoices.insert_or_assign(
						choice.GetValue(), so->GetClassType()
					);
				}
			}
			else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {
				for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_List)) {
					auto metaObject = so->GetMetaObject();
					auto choice = m_choices.Add(so->GetClassName(), metaObject->GetIcon());
					m_valChoices.insert_or_assign(
						choice.GetValue(), so->GetClassType()
					);
				}
			}
			else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
				for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Object)) {
					auto metaObject = so->GetMetaObject();
					auto choice = m_choices.Add(so->GetClassName(), metaObject->GetIcon());
					m_valChoices.insert_or_assign(
						choice.GetValue(), so->GetClassType()
					);
				}
				for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
					auto metaObject = so->GetMetaObject();
					auto choice = m_choices.Add(so->GetClassName(), metaObject->GetIcon());
					m_valChoices.insert_or_assign(
						choice.GetValue(), so->GetClassType()
					);
				}
				for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager)) {
					auto metaObject = so->GetMetaObject();
					auto choice = m_choices.Add(so->GetClassName(), metaObject->GetIcon());
					m_valChoices.insert_or_assign(
						choice.GetValue(), so->GetClassType()
					);
				}
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

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {
		FillByClsid(selectorDataType, g_valueTableCLSID);
	}

	/////////////////////////////////////////////////

	FillByClsid(selectorDataType, g_metaCatalogCLSID);
	FillByClsid(selectorDataType, g_metaDocumentCLSID);
	FillByClsid(selectorDataType, g_metaEnumerationCLSID);

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
		FillByClsid(selectorDataType, g_metaDataProcessorCLSID);
		FillByClsid(selectorDataType, g_metaReportCLSID);
	}

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {
		FillByClsid(selectorDataType, g_metaInformationRegisterCLSID);
		FillByClsid(selectorDataType, g_metaAccumulationRegisterCLSID);
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

		void FillByClsid(ibMetaData* metaData, const ibClassID& clsid,
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

		void FillByClsid(ibSelectorDataType selectorDataType, ibMetaData* metaData, const ibClassID& clsid,
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
						ibValueMetaObjectRecordDataRef* registerData = dynamic_cast<ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
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
				else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {

					const int groupIcon = imageList->Add(so->GetClassIcon());
					const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName() + wxT("List"),
						groupIcon, groupIcon);

					for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_List)) {
						ibValueMetaObjectGenericData* registerData = dynamic_cast<ibValueMetaObjectGenericData*>(so->GetMetaObject());
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
					if (so->GetClassType() != g_metaEnumerationCLSID) {

						int groupIcon = imageList->Add(so->GetClassIcon());
						const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName() + wxT("Object"),
							groupIcon, groupIcon);

						for (auto so : metaData->GetListCtorsByType(clsid, ibCtorObjectMetaType::ibCtorObjectMetaType_Object)) {
							ibValueMetaObjectRecordData* registerData = dynamic_cast<ibValueMetaObjectRecordData*>(so->GetMetaObject());
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
							ibValueMetaObjectRecordDataRef* registerData = dynamic_cast<ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
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

			const ibTypeDescription& typeDesc = data->GetTypeDesc();

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice type"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			const ibSelectorDataType& selectorDataType = typeFactory->GetFilterDataType();

			wxCheckBox* compositeDataType = new wxCheckBox(dlg, wxID_ANY,
				_("Composite data type"), wxDefaultPosition, wxDefaultSize);

			compositeDataType->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));

			int style = wxCR_SINGLE_CHECK;

			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any ||
				selectorDataType == ibSelectorDataType::ibSelectorDataType_reference) {
				style = wxCR_MULTIPLE_CHECK;
			}

			if (data != nullptr) {
				const ibTypeDescription& typeDesc = data->GetTypeDesc();
				style = typeDesc.GetClsidCount() > 1 ?
					wxCR_MULTIPLE_CHECK : wxCR_SINGLE_CHECK;
				compositeDataType->SetValue(
					typeDesc.GetClsidCount() > 1);
			}
			else {
				compositeDataType->SetValue(style != wxCR_SINGLE_CHECK);
			}

			compositeDataType->Show(selectorDataType != ibSelectorDataType::ibSelectorDataType_table);

			ibCheckTree* tc = new ibCheckTree(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | style | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

			topsizer->Add(compositeDataType, wxSizerFlags(0).Border(wxALL, spacing));
			rowsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			wxBoxSizer* stringSizer = new wxBoxSizer(wxHORIZONTAL);
			wxStaticText* stSLength = new wxStaticText(dlg, wxID_ANY, _("Length:"), wxDefaultPosition, wxDefaultSize);
			stSLength->Wrap(-1);
			stringSizer->Add(stSLength, 0, wxALL, 5);

			wxSpinCtrl* tcSLength = new wxSpinCtrl(dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, MAX_LENGTH_STRING);
			stringSizer->Add(tcSLength, 0, wxBOTTOM | wxRIGHT, 0);
			tcSLength->SetValue(typeDesc.GetLength());

			tcSLength->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED,
				[data](wxSpinEvent& event)
				{
					ibTypeDescription& typeDesc = data->GetTypeDesc();
					typeDesc.SetString(event.GetValue());
					event.Skip();
				}
			);

			tcSLength->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));
			topsizer->Add(stringSizer, 0, 0, 5);

			wxBoxSizer* dateSizer = new wxBoxSizer(wxHORIZONTAL);
			wxStaticText* stDDateFormat = new wxStaticText(dlg, wxID_ANY, _("Date format:"), wxDefaultPosition, wxDefaultSize);
			stDDateFormat->Wrap(-1);
			dateSizer->Add(stDDateFormat, 0, wxALL, 5);

			wxArrayString cDDateFormatChoices; auto ch = dlgProp->GetDateTime();
			for (unsigned int idx = 0; idx < ch.GetCount(); idx++) {
				cDDateFormatChoices.Add(ch.GetLabel(idx));
			}
			wxChoice* cDDateFormat = new wxChoice(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize, cDDateFormatChoices);
			cDDateFormat->SetSelection(typeDesc.GetDateFraction());
			dateSizer->Add(cDDateFormat, 0, wxALL, 0);

			cDDateFormat->Bind(wxEVT_COMMAND_CHOICE_SELECTED,
				[data](wxCommandEvent& event)
				{
					ibTypeDescription& typeDesc = data->GetTypeDesc();
					typeDesc.SetDate((ibDateFractions)event.GetSelection());
					event.Skip();
				}
			);

			cDDateFormat->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));
			topsizer->Add(dateSizer, 0, 0, 5);

			wxBoxSizer* numberSizer = new wxBoxSizer(wxHORIZONTAL);
			wxStaticText* stNLength = new wxStaticText(dlg, wxID_ANY, _("Length:"), wxDefaultPosition, wxDefaultSize);
			stNLength->Wrap(-1);
			numberSizer->Add(stNLength, 0, wxALL, 5);

			wxSpinCtrl* tcNLength = new wxSpinCtrl(dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, MAX_PRECISION_NUMBER);
			numberSizer->Add(tcNLength, 0, wxBOTTOM | wxRIGHT, 0);
			tcNLength->SetValue(typeDesc.GetPrecision());

			tcNLength->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED,
				[data](wxSpinEvent& event)
				{
					ibTypeDescription& typeDesc = data->GetTypeDesc();
					typeDesc.SetNumber(event.GetValue(), typeDesc.GetScale());
					event.Skip();
				}
			);

			tcNLength->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));

			wxStaticText* stNScale = new wxStaticText(dlg, wxID_ANY, _("Scale:"), wxDefaultPosition, wxDefaultSize);
			stNScale->Wrap(-1);
			numberSizer->Add(stNScale, 0, wxTOP | wxBOTTOM | wxLEFT, 5);

			wxSpinCtrl* tcNScale = new wxSpinCtrl(dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS);
			numberSizer->Add(tcNScale, 0, wxRIGHT | wxLEFT, 5);
			tcNScale->SetValue(typeDesc.GetScale());

			tcNScale->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED,
				[data](wxSpinEvent& event)
				{
					ibTypeDescription& typeDesc = data->GetTypeDesc();
					unsigned short scale = typeDesc.GetPrecision() > event.GetValue() ?
						event.GetValue() : typeDesc.GetPrecision();

					typeDesc.SetNumber(typeDesc.GetPrecision(), scale);
					event.Skip();
				}
			);

			tcNScale->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));
			topsizer->Add(numberSizer, 0, 0, 5);

			tc->SetDoubleBuffered(true);

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			compositeDataType->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED,
				[tc](wxCommandEvent& event)
				{
					tc->SetWindowStyle(
						event.IsChecked() ?
						wxCR_MULTIPLE_CHECK :
						wxCR_SINGLE_CHECK
					);
					event.Skip();
				}
			);

			for (unsigned int i = 0; i < numberSizer->GetItemCount(); i++) { numberSizer->Hide(i); }
			for (unsigned int i = 0; i < dateSizer->GetItemCount(); i++) { dateSizer->Hide(i); }
			for (unsigned int i = 0; i < stringSizer->GetItemCount(); i++) { stringSizer->Hide(i); }

			tc->Bind(wxEVT_COMMAND_TREE_SEL_CHANGED,
				[tc, numberSizer, dateSizer, stringSizer, topsizer](wxTreeEvent& event)
				{
					ibTreeItemPropertyData* item = dynamic_cast<ibTreeItemPropertyData*>(tc->GetItemData(event.GetItem()));
					if (item != nullptr) {
						const ibClassID& clsid = item->GetClassType();
						for (unsigned int i = 0; i < numberSizer->GetItemCount(); i++) {
							numberSizer->Show(i, ibValue::GetVTByID(clsid) == ibValueTypes::TYPE_NUMBER);
						}
						for (unsigned int i = 0; i < dateSizer->GetItemCount(); i++) {
							dateSizer->Show(i, ibValue::GetVTByID(clsid) == ibValueTypes::TYPE_DATE);
						}
						for (unsigned int i = 0; i < stringSizer->GetItemCount(); i++) {
							stringSizer->Show(i, ibValue::GetVTByID(clsid) == ibValueTypes::TYPE_STRING);
						}
						topsizer->Layout();
						tc->Layout();
					}
					event.Skip();
				}
			);

			const wxTreeItemId& rootItem = tc->AddRoot(wxEmptyString);

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(400, 300);
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			tc->SetFocus();

			// Make an state image list containing small icons
			tc->AssignImageList(
				new wxImageList(icon_size, icon_size)
			);

			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_EMPTY), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
			}

			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any
				|| selectorDataType == ibSelectorDataType::ibSelectorDataType_reference) {
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_DATE), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
			}
			else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_boolean) {
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
			}
			else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_resource) {
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
			}

			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
				FillByClsid(ibValue::GetIDByVT(ibValueTypes::TYPE_NULL), tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
			}

			/////////////////////////////////////////////////
			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {
				FillByClsid(g_valueTableCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
			}
			/////////////////////////////////////////////////

			ibMetaData* metaData = typeFactory->GetMetaData();
			wxASSERT(metaData);
			if (metaData != nullptr) {
				FillByClsid(selectorDataType, metaData, g_metaCatalogCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				FillByClsid(selectorDataType, metaData, g_metaDocumentCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				FillByClsid(selectorDataType, metaData, g_metaEnumerationCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
					FillByClsid(selectorDataType, metaData, g_metaDataProcessorCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
					FillByClsid(selectorDataType, metaData, g_metaReportCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				}
				if (selectorDataType == ibSelectorDataType::ibSelectorDataType_table) {
					FillByClsid(selectorDataType, metaData, g_metaInformationRegisterCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
					FillByClsid(selectorDataType, metaData, g_metaAccumulationRegisterCLSID, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly));
				}
			}

			wxArrayTreeItemIds ids;
			unsigned int itemCount = tc->GetItems(ids);

			for (auto clsid : typeDesc.GetClsidList()) {
				bool allowType = true;
				for (const wxTreeItemId& selItem : ids) {
					if (selItem.IsOk()) {
						const ibTreeItemPropertyData* item = dynamic_cast<ibTreeItemPropertyData*>(tc->GetItemData(selItem));
						if (item != nullptr && clsid == item->GetClassType()) { allowType = false; break; }
					}
				}
				if (allowType) { FillByClsid(metaData, clsid, tc, data, !dlgProp->HasFlag(wxPGFlags::ReadOnly)); }
			}
			tc->ExpandAll(); int res = dlg->ShowModal();
			if (res == wxID_OK) {
				ibVariantDataAttribute* clone = data->Clone();
				ibTypeDescription& createdDesc = clone->GetTypeDesc();
				createdDesc.ClearMetaType();
				unsigned int selCount = tc->GetSelections(ids);
				for (const wxTreeItemId& selItem : ids) {
					if (selItem.IsOk()) {
						const ibTreeItemPropertyData* item = dynamic_cast<ibTreeItemPropertyData*>(tc->GetItemData(selItem));
						if (item != nullptr) createdDesc.AppendMetaType(item->GetClassType());
					}
				}
				createdDesc.SetTypeData(typeDesc.GetTypeData());
				SetValue(clone);
			}

			dlg->Destroy();
			return res == wxID_OK;
		}
	};

	return new ibPGEditorTypeDialogAdapter();
}