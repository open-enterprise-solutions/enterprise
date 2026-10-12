#include "backend/compiler/enumUnit.h"
#include "backend/compiler/typeCtor.h"

// Unscoped, and only in this file: a scoped enum does not register
// (enumUnit.h), and these names must not leak into other translation units.
enum ibComparisonType
{
	ibComparisonType_Equal = 1,
	ibComparisonType_NotEqual,
	ibComparisonType_Less,
	ibComparisonType_LessOrEqual,
	ibComparisonType_Greater,
	ibComparisonType_GreaterOrEqual,
	ibComparisonType_Contains,
	ibComparisonType_NotContains,
	ibComparisonType_InList,
	ibComparisonType_NotInList
};

class ibValueEnumComparisonType : public ibValueEnumeration<ibComparisonType> {
public:
	ibValueEnumComparisonType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibComparisonType_Equal,        wxT("Equal"),        wxT("Equal"));
		AddEnumeration(ibComparisonType_NotEqual,     wxT("NotEqual"),     wxT("NotEqual"));
		AddEnumeration(ibComparisonType_Less,         wxT("Less"),         wxT("Less"));
		AddEnumeration(ibComparisonType_LessOrEqual,  wxT("LessOrEqual"),  wxT("LessOrEqual"));
		AddEnumeration(ibComparisonType_Greater,      wxT("Greater"),      wxT("Greater"));
		AddEnumeration(ibComparisonType_GreaterOrEqual, wxT("GreaterOrEqual"), wxT("GreaterOrEqual"));
		AddEnumeration(ibComparisonType_Contains,     wxT("Contains"),     wxT("Contains"));
		AddEnumeration(ibComparisonType_NotContains,  wxT("NotContains"),  wxT("NotContains"));
		AddEnumeration(ibComparisonType_InList,       wxT("InList"),       wxT("InList"));
		AddEnumeration(ibComparisonType_NotInList,    wxT("NotInList"),    wxT("NotInList"));
	}
};

ENUM_TYPE_REGISTER(ibValueEnumComparisonType, "ComparisonType", enum_to_clsid("EN_CTYP"));
