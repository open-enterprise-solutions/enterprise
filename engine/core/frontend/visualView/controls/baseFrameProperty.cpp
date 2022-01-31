////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control property
////////////////////////////////////////////////////////////////////////////

#include "baseControl.h"
#include "utils/typeconv.h"

void IValueFrame::SetPropertyData(Property *property, const CValue &srcValue)
{
	switch (property->GetType())
	{
	case PropertyType::PT_BOOL:
		property->SetValue(srcValue.ToBool());
		break;
	case PropertyType::PT_INT:
	case PropertyType::PT_UINT:
	case PropertyType::PT_MACRO:
	case PropertyType::PT_TYPE_SELECT:
	case PropertyType::PT_FLOAT:
		property->SetValue(srcValue.ToDouble());
		break;
	case PropertyType::PT_WXSTRING:
	case PropertyType::PT_WXSTRING_I18N:
	case PropertyType::PT_TEXT:
		property->SetValue(srcValue.ToString());
		break;
	case PropertyType::PT_OPTION:
	{
		const int m_intVal = srcValue.ToInt();
		OptionList *m_opt_list = property->GetOptionList();
		for (auto option : m_opt_list->GetOptions())
		{
			if (option.m_intVal == m_intVal) {
				property->SetValue(m_intVal);
			}
		}
		break;
	}
	default: property->SetValue(srcValue.ToString()); //point, font etc... 
	}
}

#include "frontend/visualView/special/valueFont.h"
#include "frontend/visualView/special/valueSize.h"
#include "frontend/visualView/special/valuePoint.h"
#include "frontend/visualView/special/valueColour.h"

CValue IValueFrame::GetPropertyData(Property *property)
{
	wxString m_value = property->GetValue();

	switch (property->GetType())
	{
	case PropertyType::PT_BOOL: return CValue(m_value == wxT("0") ? false : true);
	case PropertyType::PT_INT: return CValue(TypeConv::StringToInt(m_value));
	case PropertyType::PT_UINT: return CValue(TypeConv::StringToInt(m_value));
	case PropertyType::PT_FLOAT: return CValue(TypeConv::StringToFloat(m_value));
	case PropertyType::PT_MACRO: return CValue(TypeConv::GetMacroValue(m_value));
	case PropertyType::PT_WXPOINT: return new CValuePoint(TypeConv::StringToPoint(m_value));
	case PropertyType::PT_WXSIZE: return new CValueSize(TypeConv::StringToSize(m_value));
	case PropertyType::PT_WXCOLOUR: return new CValueColour(TypeConv::StringToColour(m_value));
	case PropertyType::PT_WXFONT: return new CValueFont(TypeConv::StringToFont(m_value));
	}

	return m_value;
}
