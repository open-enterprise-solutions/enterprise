#include "advpropType.h"
#include "advpropValuePicture.h"                                         // the value's picture, read when drawn

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
				// The references come after their FAMILY — `CatalogRef`, a reference to any catalog — as in
				// the type picker (ibShowTypeSelector).
				if (kind == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
					if (const ibCtorMetaAnyKind* family = ib_find_meta_any_kind(so->GetClassName(), kind)) {
						auto choice = m_choices.Add(family->GetClassName(), family->GetClassIcon());
						m_valChoices.insert_or_assign(choice.GetValue(), family->GetClassType());
					}
				for (auto ctor : metaData->GetListCtorsByType(clsid, kind)) {
					auto choice = m_choices.Add(ctor->GetClassName(), ctor->GetMetaObject()->GetIcon());
					m_valChoices.insert_or_assign(choice.GetValue(), ctor->GetClassType());
				}
			};
			if (selectorDataType == ibSelectorDataType::ibSelectorDataType_reference) {
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
				addKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic);
			}
			else if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
				// Attributes (filter = any) accept EVERY kind.
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
	// Left out, a calculation type could not be picked as an attribute's type with the mouse — a
	// payroll document's line names one, and it could only be typed so through a script.
	FillByClsid(selectorDataType, g_metaChartOfCalculationTypesCLSID);

	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
		FillByClsid(selectorDataType, g_metaDataProcessorCLSID);
		FillByClsid(selectorDataType, g_metaReportCLSID);
	}

	// …and `AnyRef` — a reference to anything at all — after every reference, as the picker has it.
	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_reference ||
		selectorDataType == ibSelectorDataType::ibSelectorDataType_any) {
		if (const ibCtorAbstractType* anyRef = ibValue::GetAvailableCtor(wxT("AnyRef"))) {
			auto choice = m_choices.Add(anyRef->GetClassName(), anyRef->GetClassIcon());
			m_valChoices.insert_or_assign(choice.GetValue(), anyRef->GetClassType());
		}
	}

	// AN EVENT SOURCE offers what the backend says raises events — the very list the picker shows
	// (ibBackendTypeConfigFactory::GetTypesByFilter), families first.
	if (selectorDataType == ibSelectorDataType::ibSelectorDataType_eventSource) {
		const ibBackendTypeConfigFactory* const factory = dynamic_cast<const ibBackendTypeConfigFactory*>(m_ownerProperty);
		const ibMetaData* const metaData = factory != nullptr ? factory->GetMetaData() : nullptr;
		std::vector<ibClassID> offered;
		ibBackendTypeConfigFactory::GetTypesByFilter(selectorDataType, metaData, offered);
		for (const ibClassID& clsid : offered) {
			const ibCtorAbstractType* so = metaData != nullptr ? metaData->GetAvailableCtor(clsid) : ibValue::GetAvailableCtor(clsid);
			if (so == nullptr)
				continue;
			auto choice = m_choices.Add(so->GetClassName(), so->GetClassIcon());
			m_valChoices.insert_or_assign(choice.GetValue(), clsid);
		}
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

// THE VALUE'S PICTURE — the one the chosen type's own choice carries; none for several types at once. This
// property answers no choice selection (GetChoiceSelection), so the grid would draw none of its own accord.
wxBitmapBundle ibPGTypeProperty::GetValuePicture() const
{
	ibVariantDataAttribute* const varData = property_cast(m_value, ibVariantDataAttribute);
	if (varData == nullptr)
		return wxBitmapBundle();
	const ibTypeDescription& td = varData->GetTypeDesc();
	if (td.GetClsidCount() != 1)
		return wxBitmapBundle();
	const ibClassID chosen = td.GetFirstClsid();
	for (const std::pair<const int, ibClassID>& choice : m_valChoices) {
		if (choice.second != chosen)
			continue;
		const int idx = m_choices.Index(choice.first);
		return idx != wxNOT_FOUND ? m_choices.Item(idx).GetBitmap() : wxBitmapBundle();
	}
	return wxBitmapBundle();
}

wxSize ibPGTypeProperty::OnMeasureImage(int /*item*/) const
{
	return ibMeasureValuePicture(GetValuePicture(), GetGrid());
}

void ibPGTypeProperty::OnCustomPaint(wxDC& dc, const wxRect& rect, wxPGPaintData& paintdata)
{
	ibPaintValuePicture(GetValuePicture(), dc, rect, paintdata);
}

#include "frontend/win/dlgs/typeSelector.h"   // the shared picker — this editor is one of its two callers

wxPGEditorDialogAdapter* ibPGTypeProperty::GetEditorDialog() const
{
	class ibPGEditorTypeDialogAdapter : public wxPGEditorDialogAdapter {
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