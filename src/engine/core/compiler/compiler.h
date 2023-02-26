#ifndef _COMPILER_H__
#define _COMPILER_H__

#include <wx/wx.h>

#include <3rdparty/ttmath/ttmath.h>
#include <3rdparty/guid/guid.h>

#include <functional>
#include <vector>
#include <map>

#include <utility>

class CValue;

class IEnumerationWrapper;

class IMetaObject;
class IMetaObjectWrapperData;
class IMetaObjectRecordData;
class IMetaObjectRecordDataRef;

class IRecordDataObject;
class IRecordDataObjectRef;

class IMetadata;

class CProcUnit;

//*******************************************************************************************
//*                                 Special structures                                      *
//*******************************************************************************************

#define emptyDate -62135604000000ll

typedef int meta_identifier_t;
typedef int form_identifier_t;
typedef int action_identifier_t;

typedef unsigned int version_identifier_t;

typedef std::map<
	meta_identifier_t, CValue
> valueArray_t;

typedef unsigned wxLongLong_t CLASS_ID;

//*******************************************************************************************
//*                                 Clsid support											*
//*******************************************************************************************

#define MK_CLSID(a,b,c,d,e,f,g,h) \
    	CLASS_ID((CLASS_ID(a)<<CLASS_ID(56))|(CLASS_ID(b)<<CLASS_ID(48))|(CLASS_ID(c)<<CLASS_ID(40))|(CLASS_ID(d)<<CLASS_ID(32))|(CLASS_ID(e)<<CLASS_ID(24))|(CLASS_ID(f)<<CLASS_ID(16))|(CLASS_ID(g)<<CLASS_ID(8))|(CLASS_ID(h)))

#define MK_CLSID_INV(a,b,c,d,e,f,g,h) MK_CLSID(h,g,f,e,d,c,b,a)

inline void CLSID2TEXT(CLASS_ID& clsid, const wxString& clsidStr) {
	clsidStr[8] = '\0';
	for (int i = 7; i >= 0; i--)
		clsidStr[i] = char(clsid & 0xff); clsid >>= 8;
}

#define CLSID_OFFSET 4800000000000000000ul

inline CLASS_ID TEXT2CLSID(const wxString& clsidStr) {

	wxASSERT(clsidStr.length() <= 8);
	char buf[9] = { 0 };
	strncpy_s(buf, sizeof(buf),
		clsidStr.GetData(), clsidStr.length()
	);
	size_t need = 8 - strlen(buf);
	while (need) {
		buf[8 - need] = ' ';
		need--;
	}
	const CLASS_ID& clsid = MK_CLSID(
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7]
	);
	wxASSERT(clsid > CLSID_OFFSET);
	return clsid;
}

//*******************************************************************************************
//*                                 Versions support									    *
//*******************************************************************************************

#define version_generate(major, minor, release) \
		( (major * 1000) + (minor * 100) + release )

#define version_oes_1_0_0 version_generate(1, 0, 0)	
#define version_oes_1_0_1 version_generate(1, 0, 1)	

#define version_oes_last  version_oes_1_0_1

//*******************************************************************************************
//*                                 Special enumeration                                     *
//*******************************************************************************************

enum eValueTypes {
	TYPE_EMPTY = 0,
	TYPE_BOOLEAN = 1,
	TYPE_NUMBER = 2,
	TYPE_DATE = 3,
	TYPE_STRING = 4,
	TYPE_NULL = 5,
	TYPE_REFFER = 100,  // cсылка на объект 
	TYPE_ENUM = 101,    // перечисление
	TYPE_OLE = 102,     // оле объект 
	TYPE_MODULE = 103,  // модуль
	TYPE_VALUE = 200,   // значение

	TYPE_LAST,
};

//*******************************************************************************************
//*                                 Declare number type                                     *
//*******************************************************************************************

typedef ttmath::Big<TTMATH_BITS(128), TTMATH_BITS(128)> number_t;

//*******************************************************************************************
//*                                 Declare special var                                     *
//*******************************************************************************************

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
#define MAX_STATIC_VAR 25ll
#else 
#define MAX_STATIC_VAR 10ll
#endif 

#define _USE_CONTROL_VALUECAST
//firebird doesn't support multiple transaction 
//#define _USE_SAVE_METADATA_IN_TRANSACTION 
// full parser is very slowly ...
//#define _USE_OLD_TEXT_PARSER_IN_CODE_EDITOR

#define _USE_64_BIT_POINT_IN_DEBUGGER 

//have bugs....
//#define _USE_NET_COMPRESSOR 

//max precision 
#define MAX_PRECISION_NUMBER 32
//max precision 
#define MAX_LENGTH_STRING 1024
//stack guard
#define MAX_OBJECTS_LEVEL 100
//max record level 
#define MAX_REC_COUNT 200

#endif