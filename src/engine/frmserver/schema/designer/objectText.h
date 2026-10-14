#ifndef __SCHEMA_OBJECT_TEXT_H__
#define __SCHEMA_OBJECT_TEXT_H__

// THE TWO TEXTS OF AN OBJECT — the desktop's help and technical information editors over an object of the designer's tree
// (designer/win/dlg/textEditor), as the server holds them: what the object carries (ibValueMetaObject's HelpContent and
// NoteContent), and the one edit, the configuration modified by it. The designer's own.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaObjectText : public ibClientSchema {
public:

	// The right to edit the configuration — the tree's (ibSchemaMetaData).
	virtual bool AccessRight(const ibSession& session) const override;

	// Of the object Id names (in Tab's tree): Help and Note. Nothing, when it is no such object.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Save {Id, Help} or {Id, Note} — that text of the object given. Refused, nothing done: no such object, neither text.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
