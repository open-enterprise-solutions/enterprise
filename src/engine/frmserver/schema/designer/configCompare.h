#ifndef __SCHEMA_CONFIG_COMPARE_H__
#define __SCHEMA_CONFIG_COMPARE_H__

// THE DESIGNER'S CONFIGURATION COMPARE — the desktop's ibConfigCompareDocument walk and merge, as the thin client asks
// them: Build walks two configurations (active vs an uploaded file, vs the database baseline, or two uploaded files) and
// answers with Records; Merge applies selected Add/Delete/Replace ops through CopyMetaObject / RemoveMetaObject /
// PasteMetaObject on the server. The client's tab (designer-thin/docManager/templates/docViewConfigCompare) draws the
// records and sends the ops.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaConfigCompare : public ibClientSchema {
public:

	// The right to edit the configuration — the tree's (ibSchemaMetaData).
	virtual bool AccessRight(const ibSession& session) const override;

	// {Mode, File?, File2?} → Records — each Status, Parent, Depth, Group, Name, Picture, Property, Left, Right,
	// LeftId, RightId. Refused: bad Mode, missing File, unreadable configuration.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Merge {Mode, File?, File2?, Pull, Ops} — each Op Add/Delete/Replace with LeftId/RightId and parent ids. Push
	// (Pull false) writes the right side back into File or File2. How it went — what was applied, what was not — is
	// said to the person by the work, as it runs.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const override;
};

#endif
