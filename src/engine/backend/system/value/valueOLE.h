#ifndef __VALUE_OLE_H__
#define __VALUE_OLE_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValueOLE :
	public ibValue {
	wxString m_objectName;
#ifdef __WXMSW__
	IDispatch* m_dispatch = nullptr;
	IStream* m_streamDispatch = nullptr;
	IDispatch* m_currentDispatch = nullptr;
#endif 
	ibValueMethodHelper* m_methodHelper;
#ifdef __WXMSW__
	CLSID m_clsId;
#endif
private:
#ifdef __WXMSW__
	IDispatch* DoCreateInstance();
#endif
#ifdef __WXMSW__
	void AddFromArray(ibValue& pvarRetValue, long* aPos, SAFEARRAY* psa, SAFEARRAYBOUND* aDims, int nLastDim) const;
	bool FromVariant(const VARIANT& oleVariant, ibValue& pvarRetValue) const;
	ibValue FromVariantArray(SAFEARRAY* psa) const;
	VARIANT FromValue(const ibValue& varRetValue) const;
	void FreeValue(VARIANT& oleVariant);
#endif
	friend class ibDebuggerServer;
public:

	//STA
	static void CreateStreamForDispatch();
	static void ReleaseStreamForDispatch();

	static void ReleaseComObjects();

	//MTA
	static void GetInterfaceAndReleaseStream();

public:

#ifdef __WXMSW__
	IDispatch* GetDispatch() const {
		return m_dispatch;
	}
#endif

	virtual ibValueMethodHelper* GetPMethods() const {
		return m_methodHelper;
	}

	virtual void PrepareNames() const;

	virtual long FindMethod(const wxString& strMethodName) const;
	virtual long FindProp(const wxString& strPropName) const;

	virtual bool IsPropReadable(const long lPropNum) const {
		return true;
	}
	virtual bool IsPropWritable(const long lPropNum) const {
		return true;
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);//setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

	//WORK AS AN AGGREGATE OBJECT
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

#ifdef __WXMSW__
	ibValueOLE();
private:
	ibValueOLE(const CLSID& clsId, IDispatch* dispatch, const wxString& objectName);
public:
#else
	ibValueOLE();
#endif

	virtual ~ibValueOLE();

	bool Create(const wxString& strOleName);

	virtual bool Init() {
		return false;
	}

	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual wxString GetString() const {
		return m_objectName;
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue& cParam) const {
#ifdef __WXMSW__
		ibValueOLE* m_pValueOLE = dynamic_cast<ibValueOLE*>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch == m_pValueOLE->m_dispatch;
#endif
		return false;
	}

	//operator '!='
	virtual bool CompareValueNE(const ibValue& cParam) const {
#ifdef __WXMSW__
		ibValueOLE* m_pValueOLE = dynamic_cast<ibValueOLE*>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch != m_pValueOLE->m_dispatch;
#endif
		return false;
	}

	//check is empty
	virtual bool IsEmpty() const {
		return false;
	}

protected:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibValueOLE);
};

#endif 