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
// demoData[] catalog/document rows are written after metadata is saved.
// Unsupported data kinds are left in skippedDataRows with diagnostics.
//
// Returns a structured result with per-op success/failure so the wizard
// can display the apply diagnostics inline before closing.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TEMPLATE_WIZARD_APPLIER_H_
#define _IB_TEMPLATE_WIZARD_APPLIER_H_

#include <wx/string.h>
#include <functional>
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
	int    expectedObjectCount = 0;
	int    missingObjectCount = 0;
	int    preflightFailureCount = 0;
	int    completenessWarningCount = 0;
	int    completenessScore = 0;
	int    expectedDataRows = 0;
	int    insertedDataRows = 0;
	int    skippedDataRows = 0;
};

// Walks responseJson's mutations[] (under "result"/"structuredContent" if
// wrapped) and dispatches each through metaBridge::HostMetaCreate/Edit/
// Delete with pluginId="designer.templateWizard". If includeData is true,
// catalog/document demoData rows are inserted into the freshly saved DB.
//
// Must be called from the UI (main) thread — metaBridge asserts.
using ProgressCallback = std::function<void(int current,
                                            int total,
                                            const OpResult& op)>;

ApplyResult Apply(const wxString& responseJson, bool includeData,
                  ProgressCallback progress = ProgressCallback());

}  // namespace ibTemplateWizardApplier

#endif // _IB_TEMPLATE_WIZARD_APPLIER_H_
