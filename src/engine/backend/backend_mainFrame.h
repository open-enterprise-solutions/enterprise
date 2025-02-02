#ifndef __MAIN_FRAME_CORE_H__
#define __MAIN_FRAME_CORE_H__

#include "backend/uniqueKey.h"
#include "backend/systemManager/systemEnum.h"

#define backend_mainFrame \
	IBackendDocMDIFrame::GetDocMDIFrame() \

class BACKEND_API IBackendDocMDIFrame {
	static IBackendDocMDIFrame* sm_mainFrame;
protected:
	IBackendDocMDIFrame();
public:

	static IBackendDocMDIFrame* GetDocMDIFrame();

	virtual ~IBackendDocMDIFrame();
	virtual wxFrame* GetFrameHandler() const = 0;

	virtual class IMetaData* FindMetadataByPath(const wxString& strFileName) const { return nullptr; }
	virtual void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const {}

	virtual class IBackendValueForm* ActiveWindow() const { return nullptr; }
	virtual class IBackendValueForm* CreateNewForm(class IBackendControlFrame* ownerControl = nullptr, class IMetaObjectForm* metaForm = nullptr,
		class ISourceDataObject* ownerSrc = nullptr, const CUniqueKey& formGuid = wxNullUniqueKey, bool readOnly = false) {
		return nullptr;
	}

	virtual class IBackendValueForm* FindFormByUniqueKey(const CUniqueKey& guid) { return nullptr; }
	virtual bool UpdateFormUniqueKey(const CUniquePairKey& guid) { return nullptr; }

#pragma region property
	virtual class BACKEND_API IPropertyObject* GetProperty() const { return nullptr; }
	virtual bool SetProperty(class BACKEND_API IPropertyObject* prop) { return false; }
#pragma endregion 

	virtual void SetTitle(const wxString& strTitle) = 0;
	virtual void SetStatusText(const wxString& strTitle, int number = 0) = 0;

	virtual void Message(const wxString& strMessage, eStatusMessage status) {}
	virtual void ClearMessage() {}

	virtual void RefreshFrame() = 0;
	virtual void RaiseFrame();

	virtual bool AuthenticationUser(const wxString& userName, const wxString& userPassword) const { return false; }

public:

	virtual void OnInitializeConfiguration(enum eConfigType cfg) {}
	virtual void OnDestroyConfiguration(enum eConfigType cfg) {}
};

#endif