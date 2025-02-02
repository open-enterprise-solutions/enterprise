////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider-team
//	Description : base object for property objects
////////////////////////////////////////////////////////////////////////////

#include "propertyInfo.h"
#include <wx/tokenzr.h>

wxIMPLEMENT_ABSTRACT_CLASS(IPropertyObject, wxObject);

bool Property::IsNull() const
{
	wxString strValue = m_value;

	switch (GetType())
	{
	case PT_BITMAP:
	{
		wxString path;
		unsigned int semicolonIndex = strValue.find_first_of(';');
		if (semicolonIndex != strValue.npos) {
			path = strValue.substr(0, semicolonIndex);
		}
		else {
			path = strValue;
		}

		return path.empty();
	}
	case PT_WXSIZE:
	{
		return (wxDefaultSize == typeConv::StringToSize(strValue));
	}
	default:
	{
		return strValue.empty();
	}
	}
}

#include "backend/compiler/value/value.h"

#include "backend/wrapper/typeInfo.h"
#include "backend/metaCollection/partial/catalogVariant.h"
#include "backend/metaCollection/partial/documentVariant.h"

bool Property::LoadData(CMemoryReader& reader)
{
	wxString name = reader.r_stringZ();
	if (name != m_name)
		return false;
	PropertyType type = (PropertyType)reader.r_s16();
	if (type != m_type)
		return false;

	switch (m_type)
	{
	case PropertyType::PT_BOOL:
		SetValue((bool)reader.r_u8());
		break;
	case PropertyType::PT_INT:
		SetValue((int)reader.r_s32());
		break;
	case PropertyType::PT_UINT:
		SetValue((unsigned int)reader.r_s32());
		break;
	case PropertyType::PT_TYPE: {
		ITypeAttribute* attributeInfo =
			dynamic_cast<ITypeAttribute*>(m_object);
		bool hasData = reader.r_u8();
		if (hasData && attributeInfo != nullptr)
			return attributeInfo->LoadTypeData(reader);
		break;
	}
	case PropertyType::PT_OWNER: {
		wxVariantOwnerData* attributeInfo =
			dynamic_cast<wxVariantOwnerData*>(m_value.GetData());
		bool hasData = reader.r_u8();
		if (hasData && attributeInfo != nullptr)
			//return attributeInfo->LoadData(reader);
			break;
	}
	case PropertyType::PT_NUMBER: {
		number_t value;
		reader.r(&value, sizeof(number_t));
		SetValue(value);
		break;
	}
	case PropertyType::PT_WXNAME:
	case PropertyType::PT_WXSTRING:
	case PropertyType::PT_TEXT:
		SetValue(reader.r_stringZ());
		break;
	case PropertyType::PT_OPTION:
		SetValue(reader.r_s32());
		break;
	default:
		SetValue(reader.r_stringZ());
	}

	return true;
}

bool Property::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(GetName());
	writer.w_s16(m_type);

	switch (m_type)
	{
	case PropertyType::PT_BOOL:
		writer.w_u8(GetValueAsBoolean());
		break;
	case PropertyType::PT_INT:
		writer.w_s32(GetValueAsInteger());
		break;
	case PropertyType::PT_UINT:
		writer.w_s32(GetValueAsInteger());
		break;
	case PropertyType::PT_TYPE: {
		ITypeAttribute* attributeInfo =
			dynamic_cast<ITypeAttribute*>(m_object);
		writer.w_u8(attributeInfo != nullptr);
		if (attributeInfo != nullptr)
			return attributeInfo->SaveTypeData(writer);
		break;
	}
	case PropertyType::PT_OWNER: {
		wxVariantOwnerData* attributeInfo =
			dynamic_cast<wxVariantOwnerData*>(m_value.GetData());
		writer.w_u8(attributeInfo != nullptr);
		if (attributeInfo != nullptr)
			//return attributeInfo->SaveData(writer);
			break;
	}
	case PropertyType::PT_NUMBER:
		writer.w(&GetValueAsNumber(), sizeof(number_t));
		break;
	case PropertyType::PT_WXNAME:
	case PropertyType::PT_WXSTRING:
	case PropertyType::PT_TEXT:
		writer.w_stringZ(GetValueAsString());
		break;
	case PropertyType::PT_OPTION:
		writer.w_s32(GetValueAsInteger());
		break;
	default:
		writer.w_stringZ(GetValueAsString());
	}

	return true;
}

void Property::SetDataValue(const CValue& srcValue)
{
	switch (GetType())
	{
	case PropertyType::PT_BOOL:
		SetValue(srcValue.GetBoolean());
		break;
	case PropertyType::PT_INT:
	case PropertyType::PT_UINT:
	case PropertyType::PT_TYPE:
	case PropertyType::PT_NUMBER:
		SetValue(srcValue.GetNumber());
		break;
	case PropertyType::PT_WXNAME:
	case PropertyType::PT_WXSTRING:
	case PropertyType::PT_TEXT:
		SetValue(srcValue.GetString());
		break;
	case PropertyType::PT_OPTION:
		SetValue(srcValue.GetNumber());
		break;
	default: SetValue(srcValue.GetString()); //point, font etc... 
	}
}

#include "backend/compiler/value/valueFont.h"
#include "backend/compiler/value/valueSize.h"
#include "backend/compiler/value/valuePoint.h"
#include "backend/compiler/value/valueColour.h"

CValue Property::GetDataValue() const
{
	CValue retValue;
	switch (GetType())
	{
	case PropertyType::PT_BOOL:
		retValue = CValue(Property::GetValueAsBoolean());
		break; 
	case PropertyType::PT_INT:

	case PropertyType::PT_UINT:
		retValue = CValue(Property::GetValueAsInteger());
		break;
	case PropertyType::PT_NUMBER:
		retValue = CValue(Property::GetValueAsNumber());
		break;
	case PropertyType::PT_WXPOINT:
		retValue = CValue::CreateObjectValueRef<CValuePoint>(Property::GetValueAsPoint());
		break;
	case PropertyType::PT_WXSIZE:
		retValue = CValue::CreateObjectValueRef<CValueSize>(Property::GetValueAsSize());
		break;
	case PropertyType::PT_WXCOLOUR:
		retValue = CValue::CreateObjectValueRef<CValueColour>(Property::GetValueAsColour());
		break;
	case PropertyType::PT_WXFONT:
		retValue = CValue::CreateObjectValueRef<CValueFont>(Property::GetValueAsFont());
		break;
	default:
		retValue = Property::GetValueAsString();
	}
	return retValue;
}

void Property::InitProperty(PropertyCategory* cat, const wxVariant& value)
{
	m_object->AddProperty(this);
	if (cat != nullptr) cat->AddProperty(this);
	m_value = value;
}

bool Property::IsEditable() const
{
	return m_object->IsEditable();
}

void Property::SetValue(const wxArrayString& str)
{
	m_value = typeConv::ArrayStringToString(str);
}

void Property::SetValue(const wxFontContainer& font)
{
	m_value = typeConv::FontToString(font);
}

void Property::SetValue(const wxColour& colour)
{
	m_value = typeConv::ColourToString(colour);
}

#include <wx/base64.h> 
#include <wx/mstream.h>

void Property::SetValue(const wxBitmap& bmp)
{
	if (!bmp.IsOk())
		return;
	m_value << bmp;
}

void Property::SetValue(const wxString& str, bool format)
{
	m_value = (format ? typeConv::TextToString(str) : str);
}

void Property::SetValue(const wxPoint& point)
{
	m_value = typeConv::PointToString(point);
}

void Property::SetValue(const wxSize& size)
{
	m_value = typeConv::SizeToString(size);
}

void Property::SetValue(const bool boolean)
{
	m_value = typeConv::BoolToString(boolean);
}

void Property::SetValue(const signed int integer)
{
	m_value = stringUtils::IntToStr(integer);
}

void Property::SetValue(const unsigned int integer)
{
	m_value = stringUtils::UIntToStr(integer);
}

void Property::SetValue(const long long integer)
{
	wxString result;
	result.Printf(wxT("%d"), (const long)integer);
	m_value = result;
}

void Property::SetValue(const number_t& val)
{
	m_value = typeConv::NumberToString(val);
}

wxFontContainer Property::GetValueAsFont() const
{
	return typeConv::StringToFont(m_value);
}

wxColour Property::GetValueAsColour() const
{
	return typeConv::StringToColour(m_value);
}
wxPoint Property::GetValueAsPoint() const
{
	return typeConv::StringToPoint(m_value);
}
wxSize Property::GetValueAsSize() const
{
	return typeConv::StringToSize(m_value);
}

bool Property::GetValueAsBoolean() const
{
	return typeConv::StringToBool(m_value);
}

wxBitmap Property::GetValueAsBitmap() const
{
	return typeConv::StringToBitmap(m_value);
}

int Property::GetValueAsInteger() const
{
	return typeConv::StringToInt(m_value);
}

wxString Property::GetValueAsString() const
{
	if (m_value.IsNull())
		return wxEmptyString;

	return m_value;
}

wxString Property::GetValueAsText() const
{
	return typeConv::StringToText(m_value);
}

wxArrayString Property::GetValueAsArrayString() const
{
	return typeConv::StringToArrayString(m_value);
}

number_t Property::GetValueAsNumber() const
{
	return typeConv::StringToNumber(m_value);
}

///////////////////////////////////////////////////////////////////////////////

#include "backend/compiler/enum.h"

void PropertyOption::SetDataValue(const CValue& srcValue) {
	Property::SetDataValue(srcValue);
}

CValue PropertyOption::GetDataValue() const {
	IEnumerationWrapper* enumVal = CreateEnum();
	if (enumVal != nullptr) {
		CValue enumVariant = Property::GetValueAsInteger();
		CValue* ppParams[] = { &enumVariant };
		if (enumVal->Init(ppParams, 1)) {
			CValue retValue = enumVal->GetEnumVariantValue();
			wxDELETE(enumVal);
			return retValue;
		}
		return enumVal;
	}
	return Property::GetDataValue();
}

///////////////////////////////////////////////////////////////////////////////

void Event::InitEvent(PropertyCategory* cat)
{
	m_object->AddEvent(this);
	if (cat != nullptr) cat->AddEvent(this);
}

bool Event::LoadData(CMemoryReader& reader)
{
	return true;
}

bool Event::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_name);
	writer.w_s16(m_type);
	writer.w_stringZ(m_value);
	return true;
}

///////////////////////////////////////////////////////////////////////////////

const int IPropertyObject::INDENT = 2;

#include "backend/backend_mainFrame.h"

IPropertyObject::~IPropertyObject()
{
	//wxDELETE(m_category);

	//for (auto& property : m_properties)
	//	wxDELETE(property.second);

	//for (auto& event : m_events)
	//	wxDELETE(event.second);

	IPropertyObject* pobj(GetThis());

	if (backend_mainFrame != nullptr && pobj == backend_mainFrame->GetProperty()) {
		backend_mainFrame->SetProperty(nullptr);
	}

	// remove the reference in the parent
	if (m_parent != nullptr)
		m_parent->RemoveChild(pobj);

	for (auto& child : m_children)
		child->SetParent(nullptr);

	//LogDebug(wxT("delete IPropertyObject"));
}

wxString IPropertyObject::GetIndentString(int indent) const
{
	int i;
	wxString s;

	for (i = 0; i < indent; i++)
		s += wxT(" ");

	return s;
}

unsigned int IPropertyObject::GetParentPosition() const
{
	if (m_parent == nullptr)
		return 0;
	unsigned int pos = 0;
	while (pos < m_parent->GetChildCount() && m_parent->m_children[pos] != this)
		pos++;
	return pos;
}

Property* IPropertyObject::GetProperty(const wxString& nameParam) const
{
	std::map<wxString, Property*>::const_iterator it = std::find_if(m_properties.begin(), m_properties.end(),
		[nameParam](const std::pair<wxString, Property*>& pair) {
			return stringUtils::CompareString(nameParam, pair.first);
		}
	);

	if (it != m_properties.end())
		return it->second;

	//LogDebug(wxT("[IPropertyObject::GetProperty] Property %s not found!"),name.c_str());
	  // este aserto falla siempre que se crea un sizerItem
	  // assert(false);

	return nullptr;
}

Property* IPropertyObject::GetProperty(unsigned int idx) const
{
	assert(idx < m_properties.size());

	std::map<wxString, Property*>::const_iterator it = m_properties.begin();
	unsigned int i = 0;
	while (i < idx && it != m_properties.end()) {
		i++;
		it++;
	}

	if (it != m_properties.end())
		return it->second;

	return nullptr;
}

Event* IPropertyObject::GetEvent(const wxString& nameParam) const
{
	std::map<wxString, Event*>::const_iterator it = std::find_if(m_events.begin(), m_events.end(),
		[nameParam](const std::pair<wxString, Event*>& pair) {
			return stringUtils::CompareString(nameParam, pair.first);
		}
	);

	if (it != m_events.end())
		return it->second;

	//LogDebug("[IPropertyObject::GetEvent] Event " + name + " not found!");
	return nullptr;
}

Event* IPropertyObject::GetEvent(unsigned int idx) const
{
	assert(idx < m_events.size());

	std::map<wxString, Event*>::const_iterator it = m_events.begin();
	unsigned int i = 0;
	while (i < idx && it != m_events.end()) {
		i++; it++;
	}

	if (it != m_events.end())
		return it->second;

	return nullptr;
}

void IPropertyObject::AddProperty(Property* prop)
{
	m_properties.insert(std::map<wxString, Property*>::value_type(prop->GetName(), prop));
}

void IPropertyObject::AddEvent(Event* event)
{
	m_events.insert(std::map<wxString, Event*>::value_type(event->GetName(), event));
}

IPropertyObject* IPropertyObject::FindNearAncestor(const wxString& type) const
{
	IPropertyObject* result = nullptr;
	IPropertyObject* parent = GetParent();
	if (parent != nullptr) {
		if (stringUtils::CompareString(parent->GetObjectTypeName(), type))
			result = parent;
		else
			result = parent->FindNearAncestor(type);
	}

	return result;
}

IPropertyObject* IPropertyObject::FindNearAncestorByBaseClass(const wxString& type) const
{
	IPropertyObject* result = nullptr;
	IPropertyObject* parent = GetParent();
	if (parent != nullptr) {
		if (stringUtils::CompareString(parent->GetObjectTypeName(), type))
			result = parent;
		else
			result = parent->FindNearAncestorByBaseClass(type);
	}

	return result;
}

bool IPropertyObject::AddChild(IPropertyObject* obj)
{
	m_children.push_back(obj);
	return true;
}

bool IPropertyObject::AddChild(unsigned int idx, IPropertyObject* obj)
{
	m_children.insert(m_children.begin() + idx, obj);
	return true;
}

void IPropertyObject::RemoveChild(IPropertyObject* obj)
{
	std::vector< IPropertyObject* >::iterator it = m_children.begin();
	while (it != m_children.end() && *it != obj)
		it++;

	if (it != m_children.end())
		m_children.erase(it);
}

void IPropertyObject::RemoveChild(unsigned int idx)
{
	assert(idx < m_children.size());

	std::vector< IPropertyObject* >::iterator it = m_children.begin() + idx;
	m_children.erase(it);
}

IPropertyObject* IPropertyObject::GetChild(unsigned int idx) const
{
	assert(idx < m_children.size());
	return m_children[idx];
}

IPropertyObject* IPropertyObject::GetChild(unsigned int idx, const wxString& type) const
{
	unsigned int cnt = 0;
	for (std::vector< IPropertyObject* >::const_iterator it = m_children.begin(); it != m_children.end(); ++it) {
		if (stringUtils::CompareString((*it)->GetObjectTypeName(), type) && ++cnt == idx)
			return *it;
	}
	return nullptr;
}

unsigned int IPropertyObject::GetPropertyIndex(const wxString& nameParam) const {
	return std::distance(m_properties.begin(),
		std::find_if(m_properties.begin(), m_properties.end(),
			[nameParam](const  std::pair<wxString, Property*>& pair) {
				return stringUtils::CompareString(nameParam, pair.first);
			}
		)
	);
}

unsigned int IPropertyObject::GetChildPosition(IPropertyObject* obj) const
{
	unsigned int pos = 0;
	while (pos < GetChildCount() && m_children[pos] != obj)
		pos++;
	return pos;
}

bool IPropertyObject::ChangeChildPosition(IPropertyObject* obj, unsigned int pos)
{
	unsigned int obj_pos = GetChildPosition(obj);

	if (obj_pos == GetChildCount() || pos >= GetChildCount())
		return false;

	if (pos == obj_pos)
		return true;

	// Procesamos el cambio de posición
	RemoveChild(obj);
	AddChild(pos, obj);
	return true;
}

bool IPropertyObject::IsSubclassOf(const wxString& clsName) const
{
	bool found = false;

	if (stringUtils::CompareString(clsName, GetClassName())) {
		found = true;
	}
	else {
		IPropertyObject* parent = GetParent();
		while (parent) {
			found = parent->IsSubclassOf(clsName);
			if (found)
				break;
			else
				parent = parent->GetParent();
		}
	}

	return found;
}

void IPropertyObject::DeleteRecursive()
{
	for (auto objChild : m_children) {
		objChild->SetParent(nullptr);
		objChild->DeleteRecursive();
		wxDELETE(objChild);
	}
	m_children.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PropertyCategory::AddProperty(Property* property)
{
	m_properties.push_back(property->GetName());
}

void PropertyCategory::AddEvent(Event* event)
{
	m_events.push_back(event->GetName());
}

void PropertyCategory::AddCategory(PropertyCategory* cat)
{
	m_categories.push_back(cat);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxArrayString& str)
{
	return CreateProperty(cat, name, type, typeConv::ArrayStringToString(str));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxFontContainer& font)
{
	return CreateProperty(cat, name, type, typeConv::FontToString(font));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxColour& colour)
{
	return CreateProperty(cat, name, type, typeConv::ColourToString(colour));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxBitmap& bmp)
{
	return CreateProperty(cat, name, type);
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxString& str, bool format)
{
	return CreateProperty(cat, name, type, wxVariant(format ? typeConv::TextToString(str) : str));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxPoint& point)
{
	return CreateProperty(cat, name, type, typeConv::PointToString(point));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const wxSize& size)
{
	return CreateProperty(cat, name, type, typeConv::SizeToString(size));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const bool boolean)
{
	return CreateProperty(cat, name, type, typeConv::BoolToString(boolean));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const int integer)
{
	return CreateProperty(cat, name, type, stringUtils::IntToStr(integer));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const long long integer)
{
	return CreateProperty(cat, name, type, stringUtils::IntToStr(integer));
}

Property* IPropertyObject::CreateProperty(PropertyCategory* cat, const propertyName_t& name, PropertyType type, const number_t& val)
{
	return CreateProperty(cat, name, type, typeConv::NumberToString(val));
}