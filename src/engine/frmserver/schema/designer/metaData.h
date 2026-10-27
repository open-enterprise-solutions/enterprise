#ifndef __SCHEMA_META_DATA_H__
#define __SCHEMA_META_DATA_H__

// THE DESIGNER'S CONFIGURATION TREE — what a person does in it, as the server holds it: the doors the desktop's
// navigator (designer/mainFrame/metaTree) called on its metadata, by id. Its rows ride the frame
// (ibClientFrameDesigner::OnDraw), so the schema shows nothing; a door changes the configuration, and the rows follow
// from what it became, as on the desktop the row followed the stage. The designer's own.

#include "frmserver/schema/clientSchema.h"

class ibMetaData;
class ibValueMetaObject;

class FRMSERVER_API ibSchemaMetaData : public ibClientSchema {
public:

	// The right to edit the configuration — what the designer's frame lets a person in by.
	virtual bool AccessRight(const ibSession& session) const override;

	// Nothing: the rows are the frame's.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Open {Id} — the object's editor; Create {Type, Parent} — a new one of that class under that object (0: the
	// configuration's own), its rows shown with the answer (ibClientFrame::ShowProperties) — the tree selects it and
	// opens its editor, as a click on New did; Remove {Id, Select} — and the rows of the object the tree selects after it
	// shown; Move {Id, Position} — put at that place among its parent's children; Copy {Id, File} — written into that
	// file, uploaded empty, done in the asking; Paste {Type, Parent, File} — read from that one. The configuration's
	// own, by the right to its base (data administration): Rollback, Apply — its ledger put to the person
	// (ibDialogApplyChange), Load {File} — read from that uploaded file, Unload {File} — written into that one, uploaded
	// empty, done in the asking; how each went is said to the person by the server. Refused, nothing done: no such
	// object, a class its parent does not take, no such file, no such right. Each with Tab — in that tab's tree, an
	// external file's (ibMetaDataDocument), not the configuration's: the configuration's own refused there.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;

	// WHOSE TREE THE DOOR IS IN — the tab's, by its id (Tab): the file its document holds, an external data processor's or
	// report's; a metaobject's tab (a form, a module) — the configuration it is in; none — the session's. Refused, null,
	// when there is no such. The tree's dialogs ask the same (ibSchemaPredefined), and the languages (ibSchemaLanguages).
	static ibMetaData* FindMetaData(const ibSession& session, const ibDataNode& args, ibProtocol::ibRefusal& refusal,
		ibString& error);

	// The object `name` names by its id — refused, null, when nothing of the configuration has it.
	static ibValueMetaObject* FindObject(ibMetaData* metaData, const ibDataNode& args, const ibString& name,
		ibProtocol::ibRefusal& refusal, ibString& error);
};

#endif
