#ifndef __LANGUAGES_H__
#define __LANGUAGES_H__

// LANGUAGES — the configuration's, as the desktop's translation window listed them (ibDialogTranslateConstructor, a
// box each) and its user's record offered them (ibDialogUserItem): the client holds no configuration, so it asks.
// Shared by every mode — a caption is translated in the designer's inspector and in the runtime's settings alike.

#include "clientSchema.h"

class FRMSERVER_API ibSchemaLanguages : public ibClientSchema {
public:

	// Languages {Guid, Title, Code} — of the configuration of the tab named (Tab: a tree's, a form's, a module's),
	// else the session's; in its order, none without one. Refused, a tab that shows none.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;
};

#endif
