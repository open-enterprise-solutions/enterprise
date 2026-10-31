#ifndef __MAIN_FRAME_CORE_H__
#define __MAIN_FRAME_CORE_H__

#include "backend/uniqueKey.h"
#include "backend/createRequest.h"   // ibFormRequest — what a form is opened with
#include "core/spreadsheet/spreadsheetObject.h"
#include "backend/system/systemEnum.h"
#include "backend/session/sessionHolder.h"   // the frame OWNS its session
#include "core/description/jobScheduleDescription.h"   // ibJobScheduleDescription — edited through ShowScheduleEditor

class ibSession;

// The frame is not a process-level singleton — it belongs to ibSession.
// Every caller reaches its frame through a session pointer available
// in its own scope:
//
//   - Runtime callers (script exec, error reporter): walk up the
//     current ProcUnit to ibProcUnitRoot → session → frame.
//   - Object-method callers (ibRuntimeModuleDataObject subclasses):
//     `GetSession()->GetFrame()` (GetSession walks parent chain).
//   - Process-lifecycle callers (metadata config hooks, property
//     dtor): `appData->GetMainSession()->GetFrame()`.
//   - Web per-tab: `ibWebSession::Session()->GetFrame()`.

class BACKEND_API ibBackendDocFrame {
protected:
	// The ONLY way to build a frame: with a session in hand. There is no
	// default constructor on purpose — a frame without a holder cannot be
	// written, so "someone forgot to attach the session" is not a bug
	// that can exist. That single rule is this base class's whole job;
	// what opening and closing MEAN is each implementation's business
	// (desktop asks BeforeStart / BeforeExit, the designer asks nothing,
	// the web will grow states desktop never has).
	explicit ibBackendDocFrame(ibSessionHolder&& holder) noexcept
		: m_sessionHolder(std::move(holder)) {
	}

public:

	virtual ~ibBackendDocFrame() = default;

	// The session this frame OWNS. Desktop (ibFrontendMainFrame), web
	// (ibWebFrame) and the server-side projection driving a thin client
	// are all the same case: the frame is what the session exists for,
	// so the frame is what keeps it alive.
	virtual ibSession* GetSession() const { return m_sessionHolder.Get(); }



	virtual class ibMetaData* FindMetadataByPath(const ibString& strFileName) const { return nullptr; }
	virtual void BackendError(const ibString& strFileName, const ibString& strDocPath, const long line, const ibString& strErrorMessage) {}

#pragma region _frontend_call_h__

	// Form support
	virtual class ibBackendValueForm* ActiveWindow() const { return nullptr; }
	virtual class ibBackendValueForm* CreateNewForm(const ibFormRequest& request, const class ibValueMetaObjectFormBase* creator, class ibBackendControlFrame* ownerControl = nullptr,
		class ibSourceDataObject* srcObject = nullptr) {
		return nullptr;
	}

	virtual ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid) {
		return wxNullUniqueKey;
	}

	virtual class ibBackendValueForm* FindFormByUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid) {
		return nullptr;
	}

	virtual class ibBackendValueForm* FindFormByUniqueKey(const ibUniqueKey& guid) { return nullptr; }
	virtual class ibBackendValueForm* FindFormByControlUniqueKey(const ibUniqueKey& guid) { return nullptr; }
	virtual class ibBackendValueForm* FindFormBySourceUniqueKey(const ibUniqueKey& guid) { return nullptr; }

	virtual bool UpdateFormUniqueKey(const ibUniqueKeyPair& guid) { return false; }

	// Grid support
	virtual bool ShowSpreadsheetDocument(const ibString& strTitle, std::shared_ptr<ibSpreadsheetObject>& doc) { return false; }
	virtual bool PrintSpreadsheetDocument(const std::shared_ptr<ibSpreadsheetObject>& doc, bool showPrintDlg = true) { return false; }

	// Open the SCHEDULE editor on `schedule`, editing it IN PLACE; true when the user accepted and
	// the value actually changed. Same shape as the spreadsheet door above and for the same reason:
	// the backend owns the value and knows nothing about wx, the frontend owns the window and knows
	// nothing about jobs. A host without a UI (appserver, codeRunner) inherits the default and simply
	// declines, which is the honest answer there.
	virtual bool ShowScheduleEditor(ibJobScheduleDescription& schedule) { return false; }

	// A PICTURE OF THIS WINDOW, as PNG bytes — the same shape as the two doors above and for the
	// same reason: the backend is asked for it (a debugger command arrives here), the frontend is
	// the only side that can draw. Whether it CAN is the implementation's business — a desktop GUI
	// can, a web client is a different question — so the default declines and a host without a
	// window inherits it.
	//
	// 🛑 THE IMPLEMENTATION ASKS ITS USER FIRST, showing them `reason`. Consent belongs here, not
	// with the caller: a screen holds counterparties, sums, somebody's pay, and none of it is the
	// platform's to hand over because a caller found it useful. Declining is an ordinary answer.
	// `area` picks WHAT to photograph, and the caller chooses because only the caller knows what it
	// is looking for: "active" — the window being worked in (a report standing on its own, a
	// dialog), "main" — the main frame, "screen" — everything, for when the person is moving
	// between windows to show a sequence.
	//
	// ⭐⭐ `focus` IS THE OTHER HALF OF THE ANSWER, and often the more useful one. Somebody trying to
	// show you something CLICKS ON IT FIRST — so the control holding the keyboard focus is them
	// pointing, in a sentence they could not finish (Max, 2026-09-04: *"he clicked and the focus
	// landed — then you know exactly which area he wants to show you"*). It comes back as text: the
	// control, the form the platform has open (ActiveWindow), and where the pointer is.
	virtual bool CaptureWindow(const ibString& reason, const ibString& area, const ibString& format,
		ibMemoryBuffer& bytes, ibString& focus) { return false; }

#pragma endregion

#pragma region property
	virtual class BACKEND_API ibPropertyObject* GetProperty() const { return nullptr; }
	virtual bool SetProperty(class BACKEND_API ibPropertyObject* prop) { return false; }
#pragma endregion 

	virtual void SetTitle(const ibString& strTitle) = 0;
	virtual void SetStatusText(const ibString& strTitle, int number = 0) = 0;

	virtual void Message(const ibString& strMessage, ibStatusMessage status) {}
	virtual void ClearMessage() {}

	virtual void RefreshFrame() = 0;
	virtual void RaiseFrame() = 0;

	// THE TWO DOORS A QUESTION TO THE PERSON PASSES — a message box and a request. Not virtual:
	// each asks the frame's session first and only then hands the question to the host
	// (DoShowModalMessage / DoRequest below). A role handler (OnAccessRead / OnAccessWrite) is
	// refused here — it runs under the policy's lock before every query, and a question there
	// would park with the lock held. A script Try catches the refusal. Bodies are out-of-line in
	// backend_mainFrame.cpp so backend.dll exports a concrete symbol for the other DLLs.

	// Frontend-facing modal-message primitive. Backend code routes every
	// wxMessageBox call through here so the GUI-only dependency stays on
	// the frontend side — the client's frame (frmserver/client/clientFrame.h)
	// asks its client to show it.
	// `style` is the wx bitmask (wxOK / wxYES_NO / wxICON_WARNING …); the
	// returned int is the wx pressed-button code.
	//
	// Not named `MessageBox` — the Win32 header defines that as a macro
	// mapping to MessageBoxW, which renames the function and breaks the
	// backend/frmserver link across the DLL boundary.
	int ShowModalMessage(const ibString& message, const ibString& caption, int style);

	// ⭐ A REQUEST TO THE CLIENT, and its response — the server's half of the request/response pair, as a
	// protocol carries it: `request` names its kind (Kind — the protocol's, not this frame's) and holds what the
	// person is shown — a type to pick among several, a quick-choice list, a template, a composer's settings — and
	// `response` comes back in the shape that kind defines. One door for every dialog a form on the server opens:
	// each kind is a small protocol of its own, the client draws it, and the caller waits the way ShowModalMessage
	// waits. False — no response: declined, cancelled, or a host with nobody to ask.
	bool Request(const class ibDataNode& request, class ibDataNode& response);

protected:
	// What each host does once a door let the question through. The defaults are a host with
	// nobody to ask: 0 (Cancel-equivalent) and no response.
	virtual int DoShowModalMessage(const ibString& message, const ibString& caption, int style) { return 0; }
	virtual bool DoRequest(const class ibDataNode& request, class ibDataNode& response) { return false; }

private:
	// The thread of life. Filled at construction — there is no other way
	// to build this class — and released by ~ibBackendDocFrame, which is
	// what ends the session.
	ibSessionHolder m_sessionHolder;
};

#endif