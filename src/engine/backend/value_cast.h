#ifndef _VALUE_CAST_H__
#define _VALUE_CAST_H__

#include "backend_core.h"

//*************************************************************************************************************************************
//*                                                           value casting                                                           *
//*************************************************************************************************************************************

#if defined(_USE_CONTROL_VALUECAST)
extern BACKEND_API void ThrowErrorTypeOperation(const wxString& fromType, wxClassInfo* clsInfo);
#endif

template <typename T, typename U>
static inline T* CastValue(U* ptr) {
	if (ptr != nullptr) {
		if (ptr->m_typeClass == ibValueTypes::TYPE_REFFER) {
			T* cast_value = dynamic_cast<T*>(ptr->GetRef());
			if (cast_value != nullptr) return cast_value;
		}
		else if (ptr->m_typeClass != ibValueTypes::TYPE_EMPTY) {
			T* cast_value = dynamic_cast<T*>(ptr);
			if (cast_value != nullptr) return cast_value;
		}
	}
#if defined(_USE_CONTROL_VALUECAST)
	ThrowErrorTypeOperation(
		ptr ? ptr->GetClassName() : wxString(wxEmptyString),
		CLASSINFO(T)
	);
#endif
	return nullptr;
};

template <typename T, typename U>
static inline T* CastValue(const U* ptr) {

	if (ptr != nullptr) {
		if (ptr->m_typeClass == ibValueTypes::TYPE_REFFER) {
			T* cast_value = dynamic_cast<T*>(ptr->GetRef());
			if (cast_value != nullptr) return cast_value;
		}
		else if (ptr->m_typeClass != ibValueTypes::TYPE_EMPTY) {
			T* cast_value = dynamic_cast<T*>(const_cast<U*>(ptr));
			if (cast_value != nullptr) return cast_value;
		}

#if defined(_USE_CONTROL_VALUECAST)
		ThrowErrorTypeOperation(
			ptr ? ptr->GetClassName() : wxString(wxEmptyString),
			CLASSINFO(T)
		);
#endif
	}

	return nullptr;
};

template <typename T, typename U>
static inline T* CastValue(U& ptr) {
	return CastValue<T>(ptr.GetRef());
};

template <typename T, typename U>
static inline T* CastValue(const U& ptr) {
	return CastValue<T>(ptr.GetRef());
};

#endif