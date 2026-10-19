#ifndef __SELECTOR_DATA_TYPE_H__
#define __SELECTOR_DATA_TYPE_H__

////////////////////////////////////////////////////////////////////////////
//	Description : WHAT SHAPE A TYPE IS PICKED IN - one vocabulary for every side
////////////////////////////////////////////////////////////////////////////
//
// A field that holds a type says which kinds of type it may take — any, a flag, a reference, a table, a
// figure, a source of events — and the type picker renders that shape. The factory that answers it is each
// side's (backend_type.h: ibBackendTypeConfigFactory::GetFilterDataType); the vocabulary is one, so it
// lives alone here, as queryBalanceRole.h does.

enum ibSelectorDataType {
	ibSelectorDataType_any,
	ibSelectorDataType_boolean,
	ibSelectorDataType_reference,
	ibSelectorDataType_table,
	ibSelectorDataType_resource,
	// WHAT RAISES AN EVENT — an object, a manager, a record set, of one metaobject or of every one of a
	// metatype: what an event handler names as its source.
	ibSelectorDataType_eventSource,
};

#endif // __SELECTOR_DATA_TYPE_H__
