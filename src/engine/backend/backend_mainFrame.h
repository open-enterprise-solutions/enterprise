#ifndef __MAIN_FRAME_CORE_H__
#define __MAIN_FRAME_CORE_H__

#include "backend/uniqueKey.h"
#include "backend/backend_spreadsheet.h"
#include "backend/system/systemEnum.h"

#define backend_mainFrame \
	ibBackendDocMDIFrame::GetDocMDIFrame() \

class BACKEND_API ibBackendDocMDIFrame {
protected:
	ibBackendDocMDIFrame();
public:

	static ibBackendDocMDIFrame* GetDocMDIFrame();

	virtual ~ibBackendDocMDIFrame();
	virtual wxFrame* GetFrameHandler() const = 0;

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

#pragma endregion 

#pragma region property
	virtual class BACKEND_API ibPropertyObject* GetProperty() const { return nullptr; }
	virtual bool SetProperty(class BACKEND_API ibPropertyObject* prop) { return false; }
#pragma endregion 

	virtual void SetTitle(const wxString& strTitle) = 0;
	virtual void SetStatusText(const wxString& strTitle, int number = 0) = 0;

	virtual void Message(const wxString& strMessage, ibStatusMessage status) {}
	virtual void ClearMessage() {}

	virtual void RefreshFrame() = 0;
	virtual void RaiseFrame() = 0;

	virtual bool AuthenticationUser(const wxString& userName, const wxString& userPassword) const { return false; }

public:
	virtual void OnInitializeConfiguration(int cfg) {}
	virtual void OnDestroyConfiguration(int cfg) {}
private:
	static ibBackendDocMDIFrame* ms_mainFrame;
};

#endif