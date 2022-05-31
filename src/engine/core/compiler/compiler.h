#ifndef _COMPILER_H__
#define _COMPILER_H__

#include <wx/wx.h>

#include <functional>
#include <iostream>
#include <array>
#include <sstream>
#include <string_view>
#include <utility>
#include <iomanip>

#include <map>
#include <vector>

#include "ttmath/ttmath.h"

class CValue;
class CConfigMetadata;

class IMetaObject;
class IMetaObjectWrapperData;
class IMetaObjectRecordData;
class IMetaObjectRecordDataRef;

class IRecordDataObject;
class IRecordDataObjectRef;

class IMetadata;
class IConfigMetadata;

struct CByteCode;

class CMethods;
class attributeArg_t;
class methodArg_t;

class CProcUnit;

class PreparedStatement;
class DatabaseResultSet;

#include "resource.h"

extern wxBitmap wxGetImageBMPFromResource(long id);
extern inline void ThrowErrorTypeOperation(const wxString& fromType, wxClassInfo* clsInfo);

//*******************************************************************************************
//*                                 Special structures                                      *
//*******************************************************************************************

#define emptyDate -62135604000000ll

typedef int meta_identifier_t;
typedef int form_identifier_t;
typedef int action_identifier_t;

typedef unsigned int version_identifier_t;

struct guid_t  // UUID = GUID = CLSID = LIBID = IID
{
	unsigned long   m_data1;
	unsigned short  m_data2;
	unsigned short  m_data3;
	unsigned char   m_data4[8];
};

//reference data
struct reference_t
{
	meta_identifier_t m_id; // id of metadata 
	guid_t m_guid;

	reference_t(const meta_identifier_t &id, const guid_t &guid) : m_id(id), m_guid(guid) {}
};

typedef unsigned wxLongLong_t CLASS_ID;

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

enum eValueTypes
{
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
//*                                 Register new objects                                    *
//*******************************************************************************************

extern void CLSID2TEXT(CLASS_ID id, const wxString& clsidText);
extern CLASS_ID TEXT2CLSID(const wxString& clsidText);

#define S_VALUE_REGISTER(class_info, class_name, class_type, class_id, clsid)\
class CAuto_##class_info##class_id \
{\
public:\
CAuto_##class_info##class_id()\
{\
	CValue::RegisterObject(class_name, new CSimpleObjectValueSingle<class_info>(class_name, class_type, clsid));\
}\
~CAuto_##class_info##class_id()\
{\
	CValue::UnRegisterObject(class_name);\
}\
}m_auto_##class_info##class_id; \

#define SO_VALUE_REGISTER(class_info, class_name, class_type, clsid)\
class CAuto_##class_type \
{\
public:\
CAuto_##class_type()\
{\
	CValue::RegisterObject(class_name, new CSystemObjectValueSingle<class_info>(class_name, clsid));\
}\
~CAuto_##class_type()\
{\
	CValue::UnRegisterObject(class_name);\
}\
}m_auto_##class_type; \

#define VALUE_REGISTER(class_info, class_name, clsid)\
class CAuto_##class_info \
{\
public:\
CAuto_##class_info()\
{\
	CValue::RegisterObject(wxT(class_name), new CObjectValueSingle<class_info>(class_name, clsid));\
}\
~CAuto_##class_info()\
{\
	CValue::UnRegisterObject(wxT(class_name));\
}\
}m_auto_##class_info;\

#define CONTROL_VALUE_REGISTER(class_info, class_name, class_type, class_image, clsid)\
class CAuto_##class_info \
{\
public:\
CAuto_##class_info()\
{\
	CValue::RegisterObject(wxT(class_name), new CControlObjectValueSingle<class_info>(class_name, class_type, class_image, clsid));\
}\
~CAuto_##class_info()\
{\
	CValue::UnRegisterObject(wxT(class_name));\
}\
}m_auto_##class_info;\

#define S_CONTROL_VALUE_REGISTER(class_info, class_name, class_type, class_image, clsid)\
class CAuto_##class_info \
{\
public:\
CAuto_##class_info()\
{\
	CValue::RegisterObject(wxT(class_name), new CControlObjectValueSingle<class_info>(class_name, class_type, class_image, true, clsid));\
}\
~CAuto_##class_info()\
{\
	CValue::UnRegisterObject(wxT(class_name));\
}\
}m_auto_##class_info;\

#define ENUM_REGISTER(class_info, class_name, clsid)\
class CAuto_##class_info \
{\
public:\
CAuto_##class_info()\
{\
	CValue::RegisterObject(wxT(class_name), new CEnumObjectValueSingle<class_info>(class_name, clsid));\
}\
~CAuto_##class_info()\
{\
	CValue::UnRegisterObject(wxT(class_name));\
}\
}m_auto_##class_info; \

#define METADATA_REGISTER(class_info, class_name, clsid)\
class CAuto_##class_info \
{\
public:\
CAuto_##class_info()\
{\
	CValue::RegisterObject(wxT(class_name), new CMetaObjectValueSingle<class_info>(class_name, clsid));\
}\
~CAuto_##class_info()\
{\
	CValue::UnRegisterObject(wxT(class_name));\
}\
}m_auto_##class_info; \

//*******************************************************************************************
//*                                 Declare number type                                     *
//*******************************************************************************************

typedef ttmath::Big<TTMATH_BITS(128), TTMATH_BITS(128)> number_t;

//*******************************************************************************************
//*                                 Declare special var                                     *
//*******************************************************************************************

#define NOT_DEFINED wxT("undefined")

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

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
#define _USE_64_BIT_POINT_IN_DEBUGGER
#else 
#define _USE_64_BIT_POINT_IN_DEBUGGER 
#endif 

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