#ifndef __SCHEMA_PROPERTIES_H__
#define __SCHEMA_PROPERTIES_H__

// THE DESIGNER'S INSPECTOR — an edit of the object it shows, whatever that is: a form's control, a metaobject. The rows
// ride the frame (ibClientFrameDesigner::OnDraw, ibPropertyInspector), so the schema shows nothing; the edit goes to
// the object shown, through the inspector's door, and the rows follow from what it became. The designer's own.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaProperties : public ibClientSchema {
public:

	// The right to edit the configuration — what the designer's frame lets a person in by.
	virtual bool AccessRight(const ibSession& session) const override;

	// Nothing: the rows are the frame's.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Save {Id, Name, Value} — the object shown, if it is still Id, its property or event of that name given that value,
	// as its node holds it; not shown — the metaobject of that id in the tab's tree (Tab: an external file's, the header
	// above it; none: the configuration's). Refused, nothing done: no such object, no such property, no value.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
