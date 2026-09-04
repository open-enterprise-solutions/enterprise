#include "debugClientImpl.h"
#include "docManager/docManager.h"

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
static void ClearEveryRunLine()
{
	if (docManager == nullptr)
		return;

	auto clear = [](ibDocument* doc) {
		if (ibValueModuleDocument* moduleDoc = dynamic_cast<ibValueModuleDocument*>(doc)) {
			moduleDoc->SetCurrentLine(wxNOT_FOUND, false);
			moduleDoc->SetToolTip(wxEmptyString);
		}
	};

	for (auto& doc : docManager->GetDocumentsVector()) {

		ibMetaDocument* metaDoc = dynamic_cast<ibMetaDocument*>(doc);
		if (metaDoc == nullptr)
			continue;

		clear(metaDoc);

		// The editors a navigator opened are OWNED by their configuration's document and are not in
		// the manager's own list — the run line is almost always in one of these.
		for (auto& child : metaDoc->GetChild())
			clear(child);
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

void ibDebuggerClientBridgeDesigner::OnSessionStart(wxSocketClient* sock)
{
	m_runLine = std::make_unique<ibDebugRunLine>();

	if (mainFrame != nullptr) mainFrame->Debugger_OnSessionStart();
}

void ibDebuggerClientBridgeDesigner::OnSessionEnd(wxSocketClient* sock)
{
	// The arrow goes with it — the holder's destructor is the clear.
	m_runLine.reset();

	if (localWindow != nullptr) localWindow->ClearAndCreate();
	if (stackWindow != nullptr) stackWindow->ClearAndCreate();

	if (mainFrame != nullptr) mainFrame->Debugger_OnSessionEnd();
}

void ibDebuggerClientBridgeDesigner::OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data)
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

	// ⭐⭐ WHICH TREE, AND THEN ONE CALL. Empty file name: the open configuration, whose tree is the
	// main form's navigator. A file name: an external data processor or report opened from disk —
	// and its DOCUMENT is what knows its tree.
	//
	// 🛑 IT USED TO GO `metaData->GetMetaTree()` — the engine handing a viewer back to the UI that
	// asked for it — and the same twenty lines were written out in both branches.
	//
	// ⚠ Stopping in a file nobody had open has to OPEN it: a debugger that stops on something the
	// person cannot see has not stopped anywhere.
	const ibGuid moduleName(data.m_moduleName);

	if (data.m_fileName.IsEmpty()) {
		if (mainFrame != nullptr && mainFrame->GetMetaWindow() != nullptr)
			mainFrame->GetMetaWindow()->EditModule(moduleName, data.m_line, true);
	}
	else if (docManager != nullptr) {
		ibDocument* fileDoc = docManager->FindDocumentByPath(data.m_fileName);
		if (fileDoc == nullptr)
			fileDoc = docManager->CreateDocument(data.m_fileName, ibDOC_SILENT);

		if (const ibMetaDataDocument* metaDoc = dynamic_cast<ibMetaDataDocument*>(fileDoc))
			if (ibMetaTreeAbstract* metaTree = metaDoc->GetMetaTree())
				metaTree->EditModule(moduleName, data.m_line, true);
	}

	if (mainFrame != nullptr) mainFrame->Debugger_OnEnterLoop();
}

void ibDebuggerClientBridgeDesigner::OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	// ⭐⭐ THE RUNTIME IS RUNNING AGAIN, SO THERE IS NO RUN LINE — ANYWHERE. That is the whole
	// statement, and it needs neither the module's name nor a tree to find it in.
	//
	// 🛑 IT USED TO LOOK THE MODULE UP AND CLEAR JUST THAT ONE, and got the lookup wrong in a way
	// that could not be seen: `m_moduleName` is a wxString carrying the ADDRESS the runtime answers
	// to — a guid — and FindAnyObjectByFilter is a template that compares NAMES when handed a
	// wxString and guids when handed an ibGuid. So it asked for an object called
	// "74d982b2-554e-…", found nothing, cleared nothing, and the arrow sat on screen after the
	// debugger had long run on (Max, 2026-09-01: *"I think the problem is you pass a string there
	// instead of a guid"* — he was right; EditModule one function up has always taken an ibGuid,
	// which is why the same lookup worked on the way IN and not on the way OUT).
	//
	// ⚠ Fixing the type would have made this handler correct. Removing the question makes it
	// UNABLE to be wrong, and it was never a question worth asking: one run line exists at a time.
	if (m_runLine != nullptr)
		m_runLine->Retake();

	// Do not clear the locals/stack windows here. The next OnEnterLoop
	// will deliver fresh data via SetStack/SetLocalVariable, which update
	// the tables incrementally. Clearing here caused a visible blank-out
	// of the tables between every debugger step.

	if (mainFrame != nullptr) mainFrame->Debugger_OnLeaveLoop();
}

void ibDebuggerClientBridgeDesigner::OnAutoComplete(const ibDebugAutoCompleteData& data)
{
	// ⚠ dynamic_cast, not the static_cast one of the two branches had: ibModuleDocument's wx base
	// chain does not name ibValueModuleDocument, so a static_cast down a sibling gives a wrong
	// pointer rather than a null one.
	//
	// THE ADDRESS THE RUNTIME ANSWERS TO, taken once and as the type it IS — a wxString here asks
	// the same lookup to compare NAMES, see the note in OnLeaveLoop.
	const ibGuid moduleName(data.m_moduleName);

	ibMetaTreeAbstract* metaTree = nullptr;

	if (data.m_fileName.IsEmpty()) {
		metaTree = mainFrame != nullptr ? mainFrame->GetMetaWindow() : nullptr;
	}
	else if (docManager != nullptr) {
		if (const ibMetaDataDocument* fileDoc = dynamic_cast<ibMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)))
			metaTree = fileDoc->GetMetaTree();
	}

	if (metaTree != nullptr && metaTree->GetMetaData() != nullptr) {
		if (ibValueMetaObject* module =
				metaTree->GetMetaData()->FindAnyObjectByFilter(moduleName, true)) {
			if (ibValueModuleDocument* moduleDoc =
					dynamic_cast<ibValueModuleDocument*>(metaTree->GetDocument(module)))
				moduleDoc->ShowAutoComplete(data);
		}
	}
}

void ibDebuggerClientBridgeDesigner::OnMessageFromServer(const ibDebugLineData& data, const wxString& message)
{
	// ⭐ WHAT ARRIVES HERE IS A FAILURE, and that is why it takes the screen: a person chose to send
	// it over ("Go to designer"), and the module and line below are theirs to fix. What a run merely
	// SAYS never comes down this road — it goes to whoever asked for the run, by the eval channel.
	if (mainFrame != nullptr) mainFrame->RaiseFrame();

	// A REPORTED ERROR HAS A PLACE, and the place has to be reachable — so this opens a file nobody
	// had open, exactly as OnEnterLoop does, and shows the line without marking it as the RUN line.
	//
	// 🛑 THE TWO BRANCHES WERE BOTH TAKEN when a file was named: the first `if` jumped inside the
	// open configuration, then the second jumped again inside the file's own.
	const ibGuid moduleName(data.m_moduleName);

	if (data.m_fileName.IsEmpty()) {
		if (mainFrame != nullptr && mainFrame->GetMetaWindow() != nullptr)
			mainFrame->GetMetaWindow()->EditModule(moduleName, data.m_line, false);
	}
	else if (docManager != nullptr) {
		ibDocument* fileDoc = docManager->FindDocumentByPath(data.m_fileName);
		if (fileDoc == nullptr)
			fileDoc = docManager->CreateDocument(data.m_fileName, ibDOC_SILENT);

		if (const ibMetaDataDocument* metaDoc = dynamic_cast<ibMetaDataDocument*>(fileDoc))
			if (ibMetaTreeAbstract* metaTree = metaDoc->GetMetaTree())
				metaTree->EditModule(moduleName, data.m_line, false);
	}

	outputWindow->OutputError(message,
		data.m_fileName, data.m_moduleName, data.m_line);
}

void ibDebuggerClientBridgeDesigner::OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr)
{
	// Same shape again — and the same two defects the twins had: both `if`s ran when a file was
	// named, so it jumped inside the open configuration first and the file's container after; and
	// the cast down to the module document was a static_cast.
	// THE ADDRESS THE RUNTIME ANSWERS TO, taken once and as the type it IS — see the note in
	// OnLeaveLoop for what a wxString does to the same lookup.
	const ibGuid moduleName(data.m_moduleName);

	ibMetaTreeAbstract* metaTree = nullptr;

	if (data.m_fileName.IsEmpty()) {
		metaTree = mainFrame != nullptr ? mainFrame->GetMetaWindow() : nullptr;
	}
	else if (docManager != nullptr) {
		if (const ibMetaDataDocument* fileDoc = dynamic_cast<ibMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)))
			metaTree = fileDoc->GetMetaTree();
	}

	if (metaTree != nullptr && metaTree->GetMetaData() != nullptr) {
		if (ibValueMetaObject* module =
				metaTree->GetMetaData()->FindAnyObjectByFilter(moduleName, true)) {
			if (ibValueModuleDocument* moduleDoc =
					dynamic_cast<ibValueModuleDocument*>(metaTree->GetDocument(module)))
				moduleDoc->SetToolTip(resultStr);
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
