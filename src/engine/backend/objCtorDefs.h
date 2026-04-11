#ifndef _SINGLE_CLASS_DEFS_H__
#define _SINGLE_CLASS_DEFS_H__

enum ibCtorObjectMetaType {
	ibCtorObjectMetaType_Reference = 1,
	ibCtorObjectMetaType_List,
	ibCtorObjectMetaType_Object,
	ibCtorObjectMetaType_Manager,
	ibCtorObjectMetaType_Selection,
	ibCtorObjectMetaType_TabularSection,
	ibCtorObjectMetaType_TabularSection_String,
	ibCtorObjectMetaType_RecordSet,
	ibCtorObjectMetaType_RecordSet_String,
	ibCtorObjectMetaType_RecordKey,
	ibCtorObjectMetaType_RecordManager
};

#define generate_class_name(prefix) GetClassName() + prefix + GetName()
#define generate_class_table_name(prefix) metaObject->GetClassName() + prefix + metaObject->GetName() + wxT(".") + GetName()

//record
#define prefixReference		wxT("Ref.")
#define prefixList			wxT("List.")
#define prefixObject		wxT("Object.")
#define prefixManager		wxT("Manager.")
#define prefixSelection		wxT("Selection.")

#define prefixTabSection	wxT("TabularSection.")
#define prefixTabSectionStr	wxT("TabularSectionString.")

//accum
#define prefixRecordKey		wxT("RecordKey.")
#define prefixRecordManager wxT("RecordManager.")
#define prefixRecordSet		wxT("RecordSet.")

#define prefixRecordSetStr	wxT("RecordSetString.")

#endif
