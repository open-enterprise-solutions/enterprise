#ifndef _SINGLE_CLASS_DEFS_H__
#define _SINGLE_CLASS_DEFS_H__

// A kind byte is 0x0F + this (metatype_to_kind, clsid.h), so the numbers stay put: 2 was a per-metaobject list.
enum ibCtorObjectMetaType {
	ibCtorObjectMetaType_Reference = 1,
	ibCtorObjectMetaType_Object = 3,
	ibCtorObjectMetaType_Manager,
	ibCtorObjectMetaType_Selection,
	ibCtorObjectMetaType_TabularSection,
	ibCtorObjectMetaType_TabularSection_String,
	ibCtorObjectMetaType_Characteristic,
	ibCtorObjectMetaType_RecordSet,
	ibCtorObjectMetaType_RecordSet_String,
	ibCtorObjectMetaType_RecordKey,
	ibCtorObjectMetaType_RecordManager, 
};

#define generate_class_name(prefix) GetClassName() + prefix + GetName()
#define generate_class_table_name(prefix) metaObject->GetClassName() + prefix + metaObject->GetName() + ibT(".") + GetName()
#define generate_class_characteristic_name(prefix) prefix + GetName()

//record
#define prefixReference			ibT("Ref.")
#define prefixObject			ibT("Object.")
#define prefixManager			ibT("Manager.")
#define prefixSelection			ibT("Selection.")

#define prefixTabSection		ibT("TabularSection.")
#define prefixTabSectionStr		ibT("TabularSectionString.")
#define prefixCharacteristic	ibT("Characteristic.")

//accum
#define prefixRecordKey			ibT("RecordKey.")
#define prefixRecordManager		ibT("RecordManager.")
#define prefixRecordSet			ibT("RecordSet.")

#define prefixRecordSetStr		ibT("RecordSetString.")

#endif
