#ifndef __SCHEMA_TYPES_H__
#define __SCHEMA_TYPES_H__

// THE LANGUAGE'S TYPES, FOR THE DESIGNER'S SYNTAX HELPER. Its articles are the client's; the types are the server's, so
// the pane asks here each time it searches and keeps nothing. The same answers type_list and type_members give.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaTypes : public ibClientSchema {
public:

	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;
};

#endif
