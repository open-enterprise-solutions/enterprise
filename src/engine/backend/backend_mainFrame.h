#ifndef __MAIN_FRAME_CORE_H__
#define __MAIN_FRAME_CORE_H__

#include "backend/uniqueKey.h"
#include "backend/backend_spreadsheet.h"
#include "backend/system/systemEnum.h"
#include "backend/session/sessionHolder.h"   // the frame OWNS its session
#include "backend/job/jobSchedule.h"          // ibJobScheduleDescription — edited through ShowScheduleEditor

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



	virtual class ibMetaData* FindMetadataByPath(const wxString& strFileName) const { return nullptr; }
	virtual void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const {}

#pragma region _frontend_call_h__

	// Form support
	virtual class ibBackendValueForm* ActiveWindow() const { return nullptr; }
	virtual class ibBackendValueForm* CreateNewForm(const class ibValueMetaObjectFormBase* creator, class ibBackendControlFrame* ownerControl = nullptr,
		class ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey) {
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
	virtual bool ShowSpreadsheetDocument(const wxString& strTitle, wxObjectDataPtr<ibBackendSpreadsheetObject>& doc) { return false; }
	virtual bool PrintSpreadsheetDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, bool showPrintDlg = true) { return false; }

	// Open the SCHEDULE editor on `schedule`, editing it IN PLACE; true when the user accepted and
	// the value actually changed. Same shape as the spreadsheet door above and for the same reason:
	// the backend owns the value and knows nothing about wx, the frontend owns the window and knows
	// nothing about jobs. A host without a UI (daemon, codeRunner) inherits the default and simply
	// declines, which is the honest answer there.
	virtual bool ShowScheduleEditor(ibJobScheduleDescription& schedule) { return false; }

#pragma endregion 

#pragma region property
	virtual class BACKEND_API ibPropertyObject* GetProperty() const { return nullptr; }
	virtual bool SetProperty(class BACKEND_API ibPropertyObject* prop) { return false; }
#pragma endregion 

	virtual void SetTitle(const wxString& strTitle) = 0;
	virtual void SetStatusText(const wxString& strTitle, int number = 0) = 0;

	virtual void Message(const wxString& strMessage, ibStatusMessage status) {}
	virtual void ClearMessage() {}

	// Frontend-facing modal-message primitive. Backend code routes every
	// wxMessageBox call through here so the GUI-only dependency stays on
	// the frontend side — desktop frame calls wxMessageBox with itself
	// as parent; web frame (future) emits an HTTP notification / toast.
	// Default base: do nothing, returns 0 (Cancel-equivalent). `style` is
	// the wx bitmask (wxOK / wxYES_NO / wxICON_WARNING …); the returned
	// int is the wx pressed-button code.
	//
	// Not named `MessageBox` — the Win32 header defines that as a macro
	// mapping to MessageBoxW, which renames the virtual and breaks the
	// backend/frontend link across the wfrontend DLL boundary.
	// Body is out-of-line in backend_mainFrame.cpp so backend.dll exports
	// a concrete symbol that wfrontend.dll can pick up via dllimport.
	virtual int ShowModalMessage(const wxString& message, const wxString& caption, int style);

	virtual void RefreshFrame() = 0;
	virtual void RaiseFrame() = 0;

private:
	// The thread of life. Filled at construction — there is no other way
	// to build this class — and released by ~ibBackendDocFrame, which is
	// what ends the session.
	ibSessionHolder m_sessionHolder;
};

#endif