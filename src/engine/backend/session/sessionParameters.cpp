////////////////////////////////////////////////////////////////////////////
//	Description : ibSession — the session parameters half
////////////////////////////////////////////////////////////////////////////
//
// Kept apart from session.cpp on purpose: this is one small mechanism with its
// own rule — a value that lives for the length of a session, written in exactly
// one window and read everywhere — and it has nothing to do with the connection,
// the pool or the registry that fill the rest of that file.
//
// The rule, in three lines of code and one of prose: closed by default, opened
// around the session module's single call, and a write outside it raises rather
// than being ignored.
//
// The declaration side (the metatype, the manager and the unit) lives with the
// metadata: metaCollection/metaSessionParameterObject.h. See
// docs/session-parameters.md for why the pieces sit where they do.
//
////////////////////////////////////////////////////////////////////////////

#include "session.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/metadataConfiguration.h"                 // activeMetaData
#include "backend/metaCollection/metaObjectMetadata.h"     // GetSessionModule
#include "backend/metaCollection/metaModuleObject.h"       // ibValueMetaObjectManagerModule
#include "backend/moduleManager/moduleManager.h"

ibValue ibSession::GetSessionParameter(const wxString& name) const
{
	const auto found = m_sessionParameters.find(name);
	// UNSET READS AS UNDEFINED here, and the caller decides what that means. The
	// unit above this (ibValueSessionParameter) turns it into the declared type's
	// own empty, because a comparison wants an empty reference of the right kind —
	// but that is a decision about types, not about storage, so it is not made here.
	return found != m_sessionParameters.end() ? found->second : ibValue();
}

void ibSession::SetSessionParameter(const wxString& name, const ibValue& value)
{
	if (!m_sessionParametersOpen) {
		ibBackendCoreException::Error(
			_("Session parameter '%s' can only be set from the session module"), name);
	}
	m_sessionParameters[name] = value;
}

// The session module runs once and fills the parameters.
//
// FAILURE IS NOT FATAL, and that is a decision rather than an omission. A broken
// SetSessionParameters must not lock everybody out of the base: the parameters
// stay empty, a policy written against an empty parameter narrows to nothing —
// the safe direction — and the reason is logged. A session that cannot start at
// all is a far worse outcome than one that shows no rows.
void ibSession::SetSessionParameters()
{
	if (appData->DesignerMode())
		return;   // the designer runs no application code

	ibValueMetaObjectConfiguration* const config = activeMetaData != nullptr
		? activeMetaData->GetCommonMetaObject() : nullptr;
	if (config == nullptr)
		return;

	const ibValueMetaObjectManagerModule* const sessionModule = config->GetSessionModule();
	if (sessionModule == nullptr || !m_root)
		return;

	ibValueModuleManager::ibValueModuleUnit* const unit = m_root->FindCommonModule(sessionModule);
	if (unit == nullptr)
		return;   // nothing declared — a configuration need not have session parameters

	// TRUSTED: the module reads data to decide what to set, and what it sets is what
	// those reads would otherwise be filtered by. The same door the role modules use.
	ibAccessTrustScope trusted(this);

	// THE WRITE WINDOW, opened exactly around the one call that may write and closed
	// on every path out of it — including the exceptional ones, which is why it is a
	// guard object and not two assignments.
	struct WriteWindow {
		explicit WriteWindow(bool& flag) : m_flag(flag) { m_flag = true; }
		~WriteWindow() { m_flag = false; }
		bool& m_flag;
	} window(m_sessionParametersOpen);

	try {
		unit->ExecAsProc(wxT("SetSessionParameters"));
	}
	catch (const ibBackendException& err) {
		ibJournalWarning(wxT("session.params"),_("SetSessionParameters: %s"), err.GetErrorDescription());
	}
	catch (const std::exception& err) {
		ibJournalWarning(wxT("session.params"),wxT("SetSessionParameters: %s"), err.what());
	}
	catch (...) {
		ibJournalWarning(wxT("session.params"),wxT("SetSessionParameters: unknown exception"));
	}
}
