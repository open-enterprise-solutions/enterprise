#include "advpropType.h"
#include "advpropValuePicture.h"                                         // the value's picture, read when drawn

#include "frmclient/backend/backend_core.h"                              // MAX_PRECISION_NUMBER, MAX_LENGTH_STRING
#include "frmclient/backend/compiler/value.h"                            // a type's name and picture, as the server named it
#include "frmclient/backend/propertyManager/property/variant/variantType.h"

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_* — the grid flags
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

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
		// ibPropertyType — the type description as the server stores it (ibTypeDescriptionMemory); what it may hold is the
		// row's offer (GetValueList: each type's class id, in the server's order; several at once unless a table's shape).
		ibPropertyRegistry::Register(wxT("ibPropertyType"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibPropertyChoiceList offered;
			const bool multiple = property->GetValueList(offered) == ibPropertyChoiceMode::Mult;
			std::vector<ibClassID> listType;
			for (unsigned int idx = 0; idx < offered.GetCount(); idx++)
				listType.push_back(offered.GetValue(idx).GetULongLong().GetValue());
			ibTypeDescription typeDesc;
			ibTypeDescriptionMemory::ReadNode(property->GetNodeValue(), typeDesc);
			return new ibPGTypeProperty(listType, multiple, property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataAttribute(typeDesc)));
		},
		[](const wxVariant& value) {
			ibDataValue node;
			if (ibVariantDataAttribute* const typeData = property_cast(value, ibVariantDataAttribute))
				ibTypeDescriptionMemory::WriteNode(node, typeData->GetTypeDesc());
			return node;
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

ibPGTypeProperty::ibPGTypeProperty(const std::vector<ibClassID>& listType, bool multiple, const wxString& label,
	const wxString& strName, const wxVariant& value) :
	wxPGProperty(label, strName), m_listType(listType), m_multiple(multiple)
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

	// Each type it may hold, named and drawn as the server offered it — in the server's order (GetTypesByFilter there:
	// the primitives, the containers, each family before its members, AnyRef last).
	for (const ibClassID& clsid : m_listType) {
		const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
		if (so == nullptr)
			continue;
		auto choice = m_choices.Add(so->GetClassName(), so->GetClassIcon());
		m_valChoices.insert_or_assign(choice.GetValue(), clsid);
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

#include "frmclient/win/dlgs/typeSelector.h"   // the shared picker — this editor is one of its two callers

wxPGEditorDialogAdapter* ibPGTypeProperty::GetEditorDialog() const
{
	class ibPGEditorTypeDialogAdapter : public wxPGEditorDialogAdapter {
	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGTypeProperty* dlgProp = wxDynamicCast(prop, ibPGTypeProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			ibVariantDataAttribute* data = property_cast(dlgProp->GetValue(), ibVariantDataAttribute);
			if (data == nullptr) return false;

			// WHAT IS ON OFFER is the property's, as the server offered it — the desktop asked the configuration for it by
			// the shape (GetTypesByFilter); so is SINGLE OR COMPOSITE: a table slot takes one type, everything else may be
			// composite.
			const bool singleChoice = !dlgProp->IsMultiple();

			ibVariantDataAttribute* clone = data->Clone();

			if (!ibShowTypeSelector(pg, ibSelectorDataType::ibSelectorDataType_any, dlgProp->GetTypeList(), clone->GetTypeDesc(),
				nullptr, !dlgProp->HasFlag(wxPGFlags::ReadOnly), singleChoice)) {
				clone->DecRef();
				return false;
			}

			SetValue(clone);
			return true;
		}
	};

	return new ibPGEditorTypeDialogAdapter();
}
