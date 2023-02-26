#ifndef  _VALUEOLE_H__
#define _VALUEOLE_H__

#include "value.h"

class CORE_API CValueOLE : 
	public CValue {
	wxString m_objectName;
#ifdef __WXMSW__
	IDispatch* m_dispatch;
#endif 
	CMethodHelper* m_methodHelper;
#ifdef __WXMSW__
	CLSID m_clsId;
#endif
private:
#ifdef __WXMSW__
	IDispatch* DoCreateInstance();
#endif
#ifdef __WXMSW__
	void AddFromArray(CValue& pvarRetValue, long* aPos, SAFEARRAY* psa, SAFEARRAYBOUND* aDims, int nLastDim) const;
	bool FromVariant(const VARIANT& oleVariant, CValue& pvarRetValue) const;
	CValue FromVariantArray(SAFEARRAY* psa) const;
	VARIANT FromValue(const CValue& varRetValue) const;
	void FreeValue(VARIANT& oleVariant);
#endif
	friend class CDebuggerServer;
public:

	//STA
	static void CreateStreamForDispatch();
	static void ReleaseCoObjects();

	//MTA
	static void GetInterfaceAndReleaseStream();

public:

#ifdef __WXMSW__
	IDispatch* GetDispatch() const {
		return m_dispatch;
	}
#endif

	virtual CMethodHelper* GetPMethods() const {
		return m_methodHelper;
	}

	virtual void PrepareNames() const;

	virtual long FindMethod(const wxString& methodName) const override {
		return FindProp(methodName);
	}
	
	virtual long FindProp(const wxString& propName) const override;

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);//установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);//значение атрибута
	
	//РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

#ifdef __WXMSW__
	CValueOLE();
	CValueOLE(const CLSID& clsId, IDispatch* dispatch, const wxString& objectName);
#else
	CValueOLE();
#endif

	virtual ~CValueOLE();

	bool Create(const wxString& oleName);

	virtual bool Init() { 
		return false;
	}
	
	virtual bool Init(CValue** paParams, const long lSizeArray);

	virtual wxString GetString() const { 
		return m_objectName;
	}
	
	virtual wxString GetTypeString() const { 
		return wxT("comObject"); 
	}

	//operator '=='
	virtual inline bool CompareValueEQ(const CValue& cParam) const {
		CValueOLE* m_pValueOLE = dynamic_cast<CValueOLE*>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch == m_pValueOLE->m_dispatch;
		else return false;
	}
	
	//operator '!='
	virtual inline bool CompareValueNE(const CValue& cParam) const {
		CValueOLE* m_pValueOLE = dynamic_cast<CValueOLE*>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch != m_pValueOLE->m_dispatch;
		else return false;
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return false; 
	}

protected:

	wxDECLARE_DYNAMIC_CLASS_NO_COPY(CValueOLE);
};

#endif 