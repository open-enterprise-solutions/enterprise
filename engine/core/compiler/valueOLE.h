#ifndef  _VALUEOLE_H__
#define _VALUEOLE_H__

#include "value.h"

class CORE_API CValueOLE : public CValue
{
	wxString m_sObjectName;

#ifdef __WXMSW__
	IDispatch *m_dispatch;
#endif 

	CMethods *m_methods;

#ifdef __WXMSW__
	CLSID m_clsId;
#endif

private:

#ifdef __WXMSW__
	IDispatch *DoCreateInstance();
#endif

#ifdef __WXMSW__
	void AddFromArray(CValue &Ret, long *aPos, SAFEARRAY *pArray, SAFEARRAYBOUND *aDims, int nLastDim);
	CValue FromVariant(VARIANT &data);
	CValue FromVariantArray(SAFEARRAY *pArray);
	VARIANT FromValue(CValue &Val);
	void FreeValue(VARIANT &Val);
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
	IDispatch *GetDispatch() { return m_dispatch; }
#endif

	virtual CMethods* GetPMethods() const;
	virtual void PrepareNames() const;//этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА
	virtual CValue Method(methodArg_t &aParams);

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cValue);//установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);//значение атрибута

	virtual int FindMethod(const wxString &sName) const override;
	virtual int FindAttribute(const wxString &sName) const override;

#ifdef __WXMSW__
	CValueOLE();
	CValueOLE(CLSID clsId, IDispatch *dispatch, const wxString &objectName);
#else
	CValueOLE();
#endif

	bool Create(const wxString &sName);

	virtual bool Init() { return false; }
	virtual bool Init(CValue **aParams);

	virtual wxString GetString() const { return m_sObjectName; };
	virtual wxString GetTypeString() const { return wxT("comObject"); };

	//operator '=='
	virtual inline bool CompareValueEQ(const CValue &cParam) const
	{
		CValueOLE *m_pValueOLE = dynamic_cast<CValueOLE *>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch == m_pValueOLE->m_dispatch;
		else return false;
	}
	//operator '!='
	virtual inline bool CompareValueNE(const CValue &cParam) const
	{
		CValueOLE *m_pValueOLE = dynamic_cast<CValueOLE *>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch != m_pValueOLE->m_dispatch;
		else return false;
	}

	//check is empty
	virtual inline bool IsEmpty() const override { return false; }

	virtual ~CValueOLE();

protected:

	wxDECLARE_DYNAMIC_CLASS_NO_COPY(CValueOLE);
};

#endif 