#include "debugClientImpl.h"

#include "designer-thin/docManager/templates/docViewModuleEditor.h"
#include "designer-thin/mainFrame/mainFrameDesigner.h"
#include "designer-thin/mainFrame/local/localWindow.h"
#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"
#include "designer-thin/mainFrame/stack/stackWindow.h"
#include "designer-thin/mainFrame/watch/watchWindow.h"
#include "frmclient/mainFrame/output/outputWindow.h"

#include <memory>   // the run line is HELD — see ibDebugRunLine below

// ============================================================================
//  What the events below need — see the note in debugClientImpl.h
// ============================================================================

// ⭐⭐ THERE IS EXACTLY ONE RUN LINE, so taking it off is a statement about EVERY open editor and not
// about the one the runtime happened to name. Written once here; it was three verbatim copies of the
// same nested walk, and the fourth caller — the one that steps from module A into module B — was
// never written at all, which is why the arrow stayed behind in A (2026-09-01).
//
// ⚠ A SWEEP AND NOT A REMEMBERED DOCUMENT, deliberately (see ibDebugRunLine below). Holding the
// document the arrow was put in would be one pointer instead of a walk — and that pointer outlives
// the document the moment somebody closes the tab, which is the family of defect this tree has paid
// for twice already. The walk is over the open editors, once per step; it cannot be stale.
//
// (Every module is a tab of its own here — the server's — so the walk is the manager's list; the desktop's went
// into a configuration's document for the editors its navigator opened.)
static void ClearEveryRunLine()
{
	if (docManager == nullptr)
		return;

	for (ibFrontendDocument* const doc : docManager->GetDocumentsVector()) {
		if (ibModuleEditDocument* const moduleDoc = dynamic_cast<ibModuleEditDocument*>(doc)) {
			moduleDoc->SetCurrentLine(wxNOT_FOUND, false);
			moduleDoc->SetToolTip(wxEmptyString);
		}
	}
}

// ⭐⭐ THE RUN LINE IS HELD, NOT REMEMBERED (Max, 2026-09-01: *"have you not thought of just doing
// the reset through RAII"*). A stop TAKES it and anything that ends the stop RELEASES it — so
// "exactly one arrow, and only while stopped" is a property of the object's lifetime rather than of
// four handlers each remembering to call the same clear.
//
// ⚠ WHAT IT HOLDS IS THE FACT, NOT THE PLACE. It stores nothing about WHICH document is marked; the
// release sweeps the open editors. A member pointing at the marked document would be shorter and
// would dangle the first time that tab is closed while the debugger is stopped in it.
class ibDebugRunLine {
public:

	ibDebugRunLine() { ClearEveryRunLine(); }
	~ibDebugRunLine() { ClearEveryRunLine(); }

	// Re-taken on every stop: the previous arrow comes off before the new one goes on, which is
	// what a step ACROSS modules needs and what nothing used to do.
	void Retake() { ClearEveryRunLine(); }

	wxDECLARE_NO_COPY_CLASS(ibDebugRunLine);
};

// Defined here, where ibDebugRunLine is complete — that is the whole reason they are not defaulted
// in the header.
ibDebuggerClientBridgeDesigner::ibDebuggerClientBridgeDesigner() = default;
ibDebuggerClientBridgeDesigner::~ibDebuggerClientBridgeDesigner() = default;

void ibDebuggerClientBridgeDesigner::OnSessionStart()
{
	m_runLine = std::make_unique<ibDebugRunLine>();

	if (mainFrame != nullptr) mainFrame->Debugger_OnStateChanged();
}

void ibDebuggerClientBridgeDesigner::OnSessionEnd()
{
	// The arrow goes with it — the holder's destructor is the clear.
	m_runLine.reset();

	if (localWindow != nullptr) localWindow->ClearAndCreate();
	if (stackWindow != nullptr) stackWindow->ClearAndCreate();

	if (mainFrame != nullptr) mainFrame->Debugger_OnStateChanged();
}

void ibDebuggerClientBridgeDesigner::OnEnterLoop(const ibDebugLineData& data)
{
	if (mainFrame != nullptr) mainFrame->RaiseFrame();

	// ⭐ THE PREVIOUS ARROW COMES OFF BEFORE THE NEW ONE GOES ON. A step that crosses from one module
	// into another gets no OnLeaveLoop for the module it left, so the old arrow stayed there — two
	// run lines on screen and only one of them true.
	//
	// ⚠ THE HOLDER'S EXISTENCE IS THE CONNECTION (Max, 2026-09-01: *"m_runLine lives for the length
	// of the connection"*) — taken in OnSessionStart, released in OnSessionEnd. Nothing creates one
	// here: a stop outside a connection is not a state that means anything.
	if (m_runLine != nullptr)
		m_runLine->Retake();

	// Empty file name: the open configuration, whose tree is the main form's navigator.
	// ⚠ A file name — an external data processor or report opened from disk — is not looked for yet: the desktop opened
	// that file's document and asked its tree (thin-designer-editors.md §8).
	if (data.m_fileName.IsEmpty() && mainFrame != nullptr && mainFrame->GetMetaWindow() != nullptr)
		mainFrame->GetMetaWindow()->EditModule(ibGuid(data.m_moduleName), data.m_line, true);

	if (mainFrame != nullptr) mainFrame->Debugger_OnStateChanged();
}

void ibDebuggerClientBridgeDesigner::OnLeaveLoop(const ibDebugLineData& data)
{
	// ⭐⭐ THE RUNTIME IS RUNNING AGAIN, SO THERE IS NO RUN LINE — ANYWHERE. That is the whole
	// statement, and it needs neither the module's name nor a tree to find it in.
	if (m_runLine != nullptr)
		m_runLine->Retake();

	// Do not clear the locals/stack windows here. The next OnEnterLoop
	// will deliver fresh data via SetStack/SetLocalVariable, which update
	// the tables incrementally. Clearing here caused a visible blank-out
	// of the tables between every debugger step.

	if (mainFrame != nullptr) mainFrame->Debugger_OnStateChanged();
}

void ibDebuggerClientBridgeDesigner::OnAutoComplete(const ibDebugAutoCompleteData& WXUNUSED(data))
{
	// ⚠ Not asked from here yet: the editor completes from the server's module (ibModuleTextView::Fetch), and has no
	// door for the runtime's answer (the desktop's ShowAutoComplete) — thin-designer-editors.md §8.
}

void ibDebuggerClientBridgeDesigner::OnMessageFromServer(const ibDebugLineData& data, const wxString& message)
{
	// ⭐ WHAT ARRIVES HERE IS A FAILURE, and that is why it takes the screen: a person chose to send
	// it over ("Go to designer"), and the module and line below are theirs to fix. What a run merely
	// SAYS never comes down this road — it goes to whoever asked for the run, by the eval channel.
	if (mainFrame != nullptr) mainFrame->RaiseFrame();

	// A REPORTED ERROR HAS A PLACE — shown without marking it as the RUN line (an external file's: see OnEnterLoop).
	if (data.m_fileName.IsEmpty() && mainFrame != nullptr && mainFrame->GetMetaWindow() != nullptr)
		mainFrame->GetMetaWindow()->EditModule(ibGuid(data.m_moduleName), data.m_line, false);

	if (mainFrame != nullptr && mainFrame->GetOutputWindow() != nullptr)
		mainFrame->GetOutputWindow()->Output(message, ibProtocol::ibFrame::ibMessage::ibLevel::Error);
}

void ibDebuggerClientBridgeDesigner::OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr)
{
	// THE ADDRESS THE RUNTIME ANSWERS TO, taken once and as the type it IS — its module's tab, by the address its State
	// says. A module not open has nobody to show it to.
	const ibGuid moduleName(data.m_moduleName);

	if (docManager == nullptr)
		return;

	for (ibFrontendDocument* const doc : docManager->GetDocumentsVector()) {
		ibModuleEditDocument* const moduleDoc = dynamic_cast<ibModuleEditDocument*>(doc);
		if (moduleDoc != nullptr && ibGuid(moduleDoc->GetDocPath()) == moduleName) {
			moduleDoc->SetToolTip(resultStr);
			return;
		}
	}
}

void ibDebuggerClientBridgeDesigner::OnSetStack(const ibStackData& stackData)
{
	stackWindow->SetStack(stackData);
}

void ibDebuggerClientBridgeDesigner::OnSetLocalVariable(const ibLocalWindowData& data)
{
	localWindow->SetLocalVariable(data);
}

// ⭐⭐ AN ANSWER TO SOMEBODY ELSE'S QUESTION IS NOT OURS.
//
// The watch row id in this payload is a `wxTreeItemId` — a pointer into THIS window's tree — so an
// answer produced for another listener carries a number that means nothing here, and handing it on
// dereferences it. The window used to defend itself by walking its own rows looking for a match,
// which is a guess wearing the clothes of a check: it happens to be right because a foreign pointer
// happens not to coincide with one of ours, and the day one did there would be nothing to tell them
// apart.
//
// Now the question carried who asked (ibDebuggerClientBridge::GetBridgeId) and the answer brings it
// back, so the test is identity rather than resemblance — and the walk is gone.
void ibDebuggerClientBridgeDesigner::OnSetVariable(const ibWatchWindowData& watchData)
{
	if (!(watchData.GetBridgeId() == GetBridgeId()))
		return;

	watchWindow->SetVariable(watchData);
}

void ibDebuggerClientBridgeDesigner::OnSetExpanded(const ibWatchWindowData& watchData)
{
	if (!(watchData.GetBridgeId() == GetBridgeId()))
		return;

	watchWindow->SetExpanded(watchData);
}
