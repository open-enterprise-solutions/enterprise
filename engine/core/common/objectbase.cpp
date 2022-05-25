////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider-team
//	Description : base object for property objects
////////////////////////////////////////////////////////////////////////////

#include "objectbase.h"
#include "utils/stringUtils.h"
#include "utils/typeconv.h"

#include <wx/tokenzr.h>

wxIMPLEMENT_ABSTRACT_CLASS(IObjectBase, wxObject);

OptionList* Property::GetTypelist() const
{
	return m_object->GetTypelist();
}

bool Property::IsNull() const
{
	wxString m_strValue = m_value;

	switch (GetType())
	{
	case PT_BITMAP:
	{
		wxString path;
		unsigned int semicolonIndex = m_strValue.find_first_of(';');
		if (semicolonIndex != m_strValue.npos) {
			path = m_strValue.substr(0, semicolonIndex);
		}
		else {
			path = m_strValue;
		}

		return path.empty();
	}
	case PT_WXSIZE:
	{
		return (wxDefaultSize == TypeConv::StringToSize(m_strValue));
	}
	default:
	{
		return m_strValue.empty();
	}
	}
}

bool Property::IsEditable()
{
	return m_object->IsEditable();
}

void Property::SetValue(const wxArrayString& str)
{
	m_value = TypeConv::ArrayStringToString(str);
}

void Property::SetValue(const wxFontContainer& font)
{
	m_value = TypeConv::FontToString(font);
}

void Property::SetValue(const wxColour& colour)
{
	m_value = TypeConv::ColourToString(colour);
}

#include <wx/base64.h> 
#include <wx/mstream.h>

void Property::SetValue(const wxBitmap& bmp)
{
	if (!bmp.IsOk())
		return;

	////wxMemoryOutputStream mos;
	////wxImage image = bmp.ConvertToImage();

	////if (image.SaveFile(mos, wxBITMAP_TYPE_PNG))
	////{
	////	const wxStreamBuffer* buff = mos.GetOutputStreamBuffer();
	////	m_value = wxBase64Encode(buff->GetBufferStart(), buff->GetBufferSize());
	////}
}

void Property::SetValue(const wxString& str, bool format)
{
	m_value = (format ? TypeConv::TextToString(str) : str);
}

void Property::SetValue(const wxPoint& point)
{
	m_value = TypeConv::PointToString(point);
}

void Property::SetValue(const wxSize& size)
{
	m_value = TypeConv::SizeToString(size);
}

void Property::SetValue(const int integer)
{
	m_value = StringUtils::IntToStr(integer);
}

void Property::SetValue(const long long integer)
{
	wxString result;
	result.Printf(wxT("%d"), integer);
	m_value = result;
}

void Property::SetValue(const double val)
{
	m_value = TypeConv::FloatToString(val);
}

wxFontContainer Property::GetValueAsFont() const
{
	return TypeConv::StringToFont(m_value);
}

wxColour Property::GetValueAsColour() const
{
	return TypeConv::StringToColour(m_value);
}
wxPoint Property::GetValueAsPoint() const
{
	return TypeConv::StringToPoint(m_value);
}
wxSize Property::GetValueAsSize() const
{
	return TypeConv::StringToSize(m_value);
}

wxBitmap Property::GetValueAsBitmap() const
{
	return TypeConv::StringToBitmap(m_value);
}

int Property::GetValueAsInteger() const
{
	int result = 0;

	switch (GetType())
	{
		//case PT_EDIT_OPTION:
		//case PT_OPTION:
		//case PT_TYPE_SELECT:
	case PT_MACRO:
		result = TypeConv::GetMacroValue(m_value);
		break;
		//case PT_BITLIST:
		//	result = TypeConv::BitlistToInt(m_value);
		//	break;
	default:
		result = TypeConv::StringToInt(m_value);
		break;
	}
	return result;
}

wxString Property::GetValueAsString() const
{
	return m_value;
}

wxString Property::GetValueAsText() const
{
	return TypeConv::StringToText(m_value);
}

wxArrayString Property::GetValueAsArrayString() const
{
	return TypeConv::StringToArrayString(m_value);
}

double Property::GetValueAsFloat() const
{
	return TypeConv::StringToFloat(m_value);
}

///////////////////////////////////////////////////////////////////////////////

const int IObjectBase::INDENT = 2;

IObjectBase::~IObjectBase()
{
	if (m_category) {
		delete m_category;
	}

	// remove the reference in the parent
	if (m_parent) {
		IObjectBase* pobj(GetThis());
		m_parent->RemoveChild(pobj);
	}

	for (auto child : m_children) {
		child->SetParent(NULL);
	}

	//LogDebug(wxT("delete IObjectBase"));
}

wxString IObjectBase::GetIndentString(int indent)
{
	int i;
	wxString s;

	for (i = 0; i < indent; i++)
		s += wxT(" ");

	return s;
}

Property* IObjectBase::GetProperty(const wxString& nameParam) const
{
	std::map<wxString, Property*>::const_iterator it = m_properties.find(nameParam.Lower());

	if (it != m_properties.end())
		return it->second;

	//LogDebug(wxT("[IObjectBase::GetProperty] Property %s not found!"),name.c_str());
	  // este aserto falla siempre que se crea un sizerItem
	  // assert(false);

	return NULL;
}

Property* IObjectBase::GetProperty(unsigned int idx) const
{
	assert(idx < m_properties.size());

	std::map<wxString, Property*>::const_iterator it = m_properties.begin();
	unsigned int i = 0;
	while (i < idx && it != m_properties.end())
	{
		i++;
		it++;
	}

	if (it != m_properties.end())
		return it->second;

	return NULL;
}

Event* IObjectBase::GetEvent(const wxString& nameParam) const
{
	std::map<wxString, Event*>::const_iterator it = m_events.find(nameParam);
	if (it != m_events.end())
		return it->second;

	//LogDebug("[IObjectBase::GetEvent] Event " + name + " not found!");
	return NULL;
}

Event* IObjectBase::GetEvent(unsigned int idx) const
{
	assert(idx < m_events.size());

	std::map<wxString, Event*>::const_iterator it = m_events.begin();
	unsigned int i = 0;
	while (i < idx && it != m_events.end())
	{
		i++;
		it++;
	}

	if (it != m_events.end())
		return it->second;

	return NULL;
}

void IObjectBase::AddProperty(Property* prop)
{
	m_properties.insert(std::map<wxString, Property*>::value_type(prop->GetName(), prop));
}

void IObjectBase::AddEvent(Event* event)
{
	m_events.insert(std::map<wxString, Event*>::value_type(event->GetName(), event));
}

IObjectBase* IObjectBase::FindNearAncestor(const wxString& type)
{
	IObjectBase* result = NULL;
	IObjectBase* parent = GetParent();
	if (parent)
	{
		if (parent->GetObjectTypeName() == type)
			result = parent;
		else
			result = parent->FindNearAncestor(type);
	}

	return result;
}

IObjectBase* IObjectBase::FindNearAncestorByBaseClass(const wxString& type)
{
	IObjectBase* result = NULL;
	IObjectBase* parent = GetParent();
	if (parent)
	{
		if (parent->GetObjectTypeName() == type)
			result = parent;
		else
			result = parent->FindNearAncestorByBaseClass(type);
	}

	return result;
}

bool IObjectBase::AddChild(IObjectBase* obj)
{
	m_children.push_back(obj);
	return true;
}

bool IObjectBase::AddChild(unsigned int idx, IObjectBase* obj)
{
	m_children.insert(m_children.begin() + idx, obj);
	return true;
}

void IObjectBase::RemoveChild(IObjectBase* obj)
{
	std::vector< IObjectBase* >::iterator it = m_children.begin();
	while (it != m_children.end() && *it != obj)
		it++;

	if (it != m_children.end())
		m_children.erase(it);
}

void IObjectBase::RemoveChild(unsigned int idx)
{
	assert(idx < m_children.size());

	std::vector< IObjectBase* >::iterator it = m_children.begin() + idx;
	m_children.erase(it);
}

IObjectBase* IObjectBase::GetChild(unsigned int idx)
{
	assert(idx < m_children.size());

	return m_children[idx];
}

IObjectBase* IObjectBase::GetChild(unsigned int idx, const wxString& type)
{
	unsigned int cnt = 0;

	for (std::vector< IObjectBase* >::iterator it = m_children.begin(); it != m_children.end(); ++it)
	{
		if ((*it)->GetObjectTypeName() == type && ++cnt == idx) return *it;
	}

	return NULL;
}

unsigned int IObjectBase::GetChildPosition(IObjectBase* obj)
{
	unsigned int pos = 0;
	while (pos < GetChildCount() && m_children[pos] != obj)
		pos++;

	return pos;
}

bool IObjectBase::ChangeChildPosition(IObjectBase* obj, unsigned int pos)
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

///////////////////////////////////////////////////////////////////////////////

bool IObjectBase::IsNull(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->IsNull();
	else
		return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxArrayString& str)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(str);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxFontContainer& font)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(font);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxFont& font)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(font);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxColour& colour)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(colour);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxBitmap& bmp)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(bmp);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxString& str, bool format)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(str, format);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxPoint& point)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(point);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const wxSize& size)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(size);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const int integer)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(integer);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const bool integer)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(integer);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const long integer)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(integer);
	return true;
}

bool IObjectBase::SetPropertyValue(const wxString& propertyName, const double val)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	property->SetValue(val);
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxArrayInt& ineger)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	wxArrayInt array;

	IntList il(property->GetValue(), property->GetType() == PT_UINTLIST);
	for (unsigned int i = 0; i < il.GetSize(); i++)
		array.Add(il.GetValue(i));

	ineger = array;
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxArrayString& str)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	str = property->GetValueAsArrayString();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxFontContainer& font)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	font = property->GetValueAsFont();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxFont& font)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	font = property->GetValueAsFont();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxColour& colour)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	colour = property->GetValueAsColour();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxBitmap& bmp)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	bmp = property->GetValueAsBitmap();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxString& str)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	str = property->GetValueAsString();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxPoint& point)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	point = property->GetValueAsPoint();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, wxSize& size)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	size = property->GetValueAsSize();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, bool& integer)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	integer = property->GetValueAsInteger() > 0;
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, int& integer)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	integer = property->GetValueAsInteger();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, long& integer)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	integer = property->GetValueAsInteger();
	return true;
}

bool IObjectBase::GetPropertyValue(const wxString& propertyName, double& val)
{
	Property* property = GetProperty(propertyName);
	if (!property)
		return false;

	val = property->GetValueAsFloat();
	return true;
}


int IObjectBase::GetPropertyAsInteger(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsInteger();
	else
		return 0;
}

wxFontContainer IObjectBase::GetPropertyAsFont(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsFont();
	else
		return wxFontContainer();
}

wxColour IObjectBase::GetPropertyAsColour(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsColour();
	else
		return wxColour();
}

wxString IObjectBase::GetPropertyAsString(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsString();
	else
		return wxString();
}

wxPoint  IObjectBase::GetPropertyAsPoint(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsPoint();
	else
		return wxPoint();
}

wxSize   IObjectBase::GetPropertyAsSize(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsSize();
	else
		return wxDefaultSize;
}

wxBitmap IObjectBase::GetPropertyAsBitmap(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsBitmap();
	else
		return wxBitmap();
}

double IObjectBase::GetPropertyAsFloat(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsFloat();
	else
		return 0;
}

wxVariant &IObjectBase::GetPropertyAsVariant(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValue();
	else
		return wxNullVariant;
}

wxArrayInt IObjectBase::GetPropertyAsArrayInt(const wxString& propertyName)
{
	wxArrayInt array;
	Property* property = GetProperty(propertyName);
	if (property)
	{
		IntList il(property->GetValue(), property->GetType() == PT_UINTLIST);
		for (unsigned int i = 0; i < il.GetSize(); i++)
			array.Add(il.GetValue(i));
	}

	return array;
}

wxArrayString IObjectBase::GetPropertyAsArrayString(const wxString& propertyName)
{
	Property* property = GetProperty(propertyName);
	if (property)
		return property->GetValueAsArrayString();
	else
		return wxArrayString();
}

bool IObjectBase::IsSubclassOf(wxString classname)
{
	bool found = false;

	if (classname == GetClassName())
	{
		found = true;
	}
	else
	{
		IObjectBase* m_parent = GetParent();

		while (m_parent)
		{
			found = m_parent->IsSubclassOf(classname);

			if (found)
				break;
			else
				m_parent = m_parent->GetParent();
		}
	}

	return found;
}

void IObjectBase::DeleteRecursive()
{
	for (auto objChild : m_children) {
		objChild->SetParent(NULL);
		objChild->DeleteRecursive();
		delete objChild;
	}
	m_children.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

wxString CodeInfo::GetTemplate(wxString name)
{
	wxString result;

	TemplateMap::iterator it = m_templates.find(name);
	if (it != m_templates.end())
		result = it->second;

	return result;
}

void CodeInfo::AddTemplate(wxString name, wxString _template)
{
	m_templates.insert(TemplateMap::value_type(name, _template));
}

void CodeInfo::Merge(CodeInfo* merger)
{
	TemplateMap::iterator mergerTemplate;
	for (mergerTemplate = merger->m_templates.begin(); mergerTemplate != merger->m_templates.end(); ++mergerTemplate)
	{
		std::pair< TemplateMap::iterator, bool > mine = m_templates.insert(TemplateMap::value_type(mergerTemplate->first, mergerTemplate->second));
		if (!mine.second)
		{
			mine.first->second += mergerTemplate->second;
		}
	}
}
