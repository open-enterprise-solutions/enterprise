/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardApplier — applies a template's mutations[] to the live
// activeMetaData via metaBridge.
//
// Walks mutations from an oes_template_get or oes_template_customize
// response and dispatches each through metaBridge::HostMetaCreate/Edit/
// Delete with a dedicated "designer.templateWizard" pluginId. The caller
// is expected to have already granted AllowAlways policy for that
// pluginId (see ibTemplateWizard::GrantWizardPolicy).
//
// demoData[] is counted but not inserted yet: record creation needs a
// dedicated data API, while metaBridge is intentionally metadata-only.
//
// Returns a structured result with per-op success/failure so the wizard
// can display the apply diagnostics inline before closing.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TEMPLATE_WIZARD_APPLIER_H_
#define _IB_TEMPLATE_WIZARD_APPLIER_H_

#include <wx/string.h>
#include <vector>

namespace ibTemplateWizardApplier {

struct OpResult {
	wxString op;          // "create" / "edit" / "delete" / "data-insert"
	wxString kind;
	wxString fullName;
	bool     success = false;
	wxString error;       // populated on failure
};

struct ApplyResult {
	std::vector<OpResult> ops;
	int    successCount = 0;
	int    failureCount = 0;
	int    skippedDataRows = 0;
};

// Walks responseJson's mutations[] (under "result"/"structuredContent" if
// wrapped) and dispatches each through metaBridge::HostMetaCreate/Edit/
// Delete with pluginId="designer.templateWizard". If includeData is true,
// demoData rows are counted in skippedDataRows until the data API lands.
//
// Must be called from the UI (main) thread — metaBridge asserts.
ApplyResult Apply(const wxString& responseJson, bool includeData);

}  // namespace ibTemplateWizardApplier

#endif // _IB_TEMPLATE_WIZARD_APPLIER_H_
