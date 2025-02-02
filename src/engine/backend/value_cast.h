#ifndef _VALUE_CAST_H__
#define _VALUE_CAST_H__

#include "backend_core.h"

//*************************************************************************************************************************************
//*                                                           value casting                                                           *
//*************************************************************************************************************************************

template <typename retType,
	typename retRef = retType* >
class value_cast {
	CValue* m_castValue;
public:

	operator retRef() const {
		return cast_value();
	}

	explicit value_cast(const CValue& cValue) :
		m_castValue(cValue.GetRef()) {
	}

	explicit value_cast(const CValue* refValue) :
		m_castValue(refValue ? refValue->GetRef() : nullptr) {
	}

protected:

	inline retRef cast_value() const {
		retRef retValue = nullptr;
		if (m_castValue != nullptr) {
			retValue = dynamic_cast<retRef>(m_castValue);
			if (retValue != nullptr) {
				return retValue;
			}
		}
#if defined(_USE_CONTROL_VALUECAST)
		ThrowErrorTypeOperation(
			m_castValue ? m_castValue->GetClassName() : wxEmptyString,
			CLASSINFO(retType)
		);
#endif
		return nullptr;
	}
};

#if defined(_USE_CONTROL_VALUECAST)
extern BACKEND_API void ThrowErrorTypeOperation(const wxString& fromType, wxClassInfo* clsInfo);
#endif

#endif