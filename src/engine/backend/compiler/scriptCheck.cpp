////////////////////////////////////////////////////////////////////////////
//	Description : ibCheckScript / ibCheckModule — compile and report
////////////////////////////////////////////////////////////////////////////

#include "backend/compiler/scriptCheck.h"

#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileModule.h"     // ibCompileModule — the contextual compile
#include "backend/metaData.h"                   // ibCompileValueCache::FindCompileModule
#include "backend/metaCollection/metaObject.h"  // ibValueMetaObject::GetMetaData
#include "backend/moduleInfo.h"                 // ibRuntimeModuleDataObject::GetCompileModule
#include "backend/moduleManager/moduleManager.h" // ibValueModuleManager — the context a snippet parents to
#include "backend/session/session.h"            // ibSession::EditModuleManagerFor

namespace {

// ONE INSTANCE PER CALL, never a shared one: these doors are called from
// several threads at once (a language service answering an editor while a
// build step asks its own question), and a single collector would mix their
// answers. The TYPE is shared; the instance is a local.
class ibDiagnosticCollector : public ibDiagnosticSink {
public:
	void OnDiagnostic(const ibDiagnostic& diagnostic) override { m_found.push_back(diagnostic); }
	std::vector<ibDiagnostic> m_found;
};

// The compiler reports by publishing a diagnostic and THEN throwing, so the
// record is already in the collector by the time we are here — there is
// nothing to add in the handlers, and nothing to rethrow: the refusal is the
// answer, not a failure of the caller.
template <typename Compile>
std::vector<ibDiagnostic> CollectFrom(Compile compile)
{
	// ⭐⭐ THE ANSWER GOES TO WHOEVER ASKED, AND NOWHERE ELSE. A compile failure normally travels to
	// the designer's message pane as well (ibBackendException::ProcessError → ibFrontendMainFrame::
	// BackendError), which is right for code somebody is EDITING and wrong for a text somebody
	// ASKED ABOUT: checking a snippet filled the person's own window with the assistant's failed
	// attempts — *"{module(3)}: Procedure or function not detected (NoSuchFunction)"* about a text
	// they never wrote (Max, 2026-09-01: *"I do not particularly need to know what did not work out
	// for you — that is information for you"*).
	//
	// ⚠ THE MECHANISM ALREADY EXISTED, for the same reason and one road over: an EVALUATION — a
	// watch row, a debugger tooltip — is compile-and-discard too, and ProcessError skips the pane
	// while `isEvalMode` holds. A check is the same kind of act, so it says so the same way instead
	// of growing a second switch beside it.
	const ibBackendException::ibEvalModeScope quiet;

	ibDiagnosticCollector collector;
	ibDiagnostics::Subscribe(&collector);

	try {
		compile();
	}
	catch (const ibBackendException&) {
		// Already in the collector — see above.
	}
	catch (...) {
		// The compiler failed in a way it does not describe. An empty answer
		// beats taking the caller down: this runs on a plugin's thread and on
		// the MCP server's alike.
	}

	ibDiagnostics::Unsubscribe(&collector);
	return std::move(collector.m_found);
}

} // namespace

std::vector<ibDiagnostic> ibCheckScript(const wxString& text, const wxString& moduleName,
	const ibMetaData* metaData)
{
	return CollectFrom([&] {
		// COMPILED AND DISCARDED. The compiler is constructed here and dies
		// here: nothing is registered, no module is replaced, the open
		// configuration never learns this happened.
		ibCompileCode compiler(moduleName, wxT("check"), false);

		// ⭐ …BUT NOT IN A VACUUM, when a configuration was named. The module manager this
		// configuration compiles against IS the context every module of it is parented to — the
		// globals among them — so a snippet judged without it is judged against a language that
		// has no Message, no Query and no ValueTable. See the note in the header.
		//
		// ⚠ NULL IS ORDINARY: a plugin checking a text with no configuration open gets the old
		// behaviour, which is the most that can honestly be said about a text with no home.
		if (metaData != nullptr) {
			if (ibValueModuleManager* manager = ibSession::EditModuleManagerFor(metaData)) {
				if (ibCompileModule* context = manager->GetCompileModule()) {

					// 🛑 …AND IT HAS TO HAVE BEEN COMPILED. A manager nobody compiled carries an
					// EMPTY bytecode, so parenting to it buys nothing: `CurrentDate()` comes back as
					// "procedure or function not detected" — a true statement about an empty world.
					// The designer's composer window learned this the hard way and does exactly this
					// before it checks anything (PrepareModuleContext, composerSettings.cpp); Max
					// pointed here: *"that is where we hunted a bug for a very long time"*.
					//
					// ⚠ SWALLOWED. A configuration whose own modules do not compile is not this
					// checker's complaint to make — it was asked about the SNIPPET. The reason is
					// written down instead, because "the check knows fewer names than it should" is
					// invisible otherwise.
					try { context->Compile(); }
					catch (const ibBackendException& error) {
						ibJournalInfo(wxT("compiler"),
							wxT("script check: the module manager did not compile - %s"),
							error.GetErrorDescription());
					}

					compiler.SetParent(context);
				}
			}
		}

		compiler.Compile(text);
	});
}

ibScriptCheckAnswer ibCheckModule(const ibValueMetaObject* metaObject)
{
	ibScriptCheckAnswer answer;

	if (metaObject == nullptr)
		return answer;

	const ibMetaData* metaData = metaObject->GetMetaData();
	if (metaData == nullptr)
		return answer;

	// The cache exists only where the configuration supports designer editing,
	// which is the same test the rest of the tree spells as
	// `if (auto* cc = metaData->GetCompileCache())` rather than by asking what
	// mode we are in.
	ibCompileValueCache* cache = metaData->GetCompileCache();
	if (cache == nullptr)
		return answer;

	ibRuntimeModuleDataObject* dataRef = nullptr;
	if (!cache->FindCompileModule(metaObject, dataRef) || dataRef == nullptr)
		return answer;

	ibCompileModule* compileModule = dataRef->GetCompileModule();
	if (compileModule == nullptr)
		return answer;

	// THE BUTTON'S OWN ROAD — ibCodeEditor::SyntaxControl compiles exactly this,
	// so the module is checked against the metaobject that owns it and every
	// name it can legitimately see resolves.
	answer.m_diagnostics = CollectFrom([&] { compileModule->Compile(); });
	answer.m_outcome = ibScriptCheckOutcome::Checked;
	return answer;
}
