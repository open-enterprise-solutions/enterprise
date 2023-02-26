////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, Vadim Zeitlin, 2C-Team
//	Description : OLE-supporter
////////////////////////////////////////////////////////////////////////////

#include "valueole.h"
#include "translateError.h"
#include "appData.h"

#include <wx/clipbrd.h>

#pragma warning(push)
#pragma warning(disable : 4018)

#ifdef __WXMSW__

static std::map<IDispatch*, CValueOLE*> s_valueOLE;

//*********************************************************************************************************************
//*                                             Wrapper around BSTR type (by Vadim Zeitlin)                           *
//*********************************************************************************************************************

class wxBasicString
{
public:
	// Constructs with the owned BSTR set to NULL
	wxBasicString() : m_bstrBuf(NULL) {}

	// Constructs with the owned BSTR created from a wxString
	wxBasicString(const wxString& str)
		: m_bstrBuf(SysAllocString(str.wc_str(*wxConvCurrent))) {}

	// Constructs with the owned BSTR as a copy of the BSTR owned by bstr
	wxBasicString(const wxBasicString& bstr) : m_bstrBuf(bstr.Copy()) {}

	// Frees the owned BSTR
	~wxBasicString() { SysFreeString(m_bstrBuf); }

	// Creates the owned BSTR from a wxString
	void AssignFromString(const wxString& str)
	{
		SysFreeString(m_bstrBuf);
		m_bstrBuf = SysAllocString(str.wc_str(*wxConvCurrent));
	}

	// Returns the owned BSTR and gives up its ownership,
	// the caller is responsible for freeing it
	BSTR Detach()
	{
		BSTR bstr = m_bstrBuf;
		m_bstrBuf = NULL;
		return bstr;
	}

	// Returns a copy of the owned BSTR,
	// the caller is responsible for freeing it
	BSTR Copy() const { return SysAllocString(m_bstrBuf); }

	// Returns the address of the owned BSTR, not to be called
	// when wxBasicString already contains a non-NULL BSTR
	BSTR* ByRef()
	{
		wxASSERT_MSG(!m_bstrBuf,
			wxS("Can't get direct access to initialized BSTR"));
		return &m_bstrBuf;
	}

	// Sets its BSTR to a copy of the BSTR owned by bstr
	wxBasicString& operator=(const wxBasicString& src)
	{
		if (this != &src)
		{
			wxCHECK_MSG(m_bstrBuf == NULL || m_bstrBuf != src.m_bstrBuf,
				*this, wxS("Attempting to assign already owned BSTR"));
			SysFreeString(m_bstrBuf);
			m_bstrBuf = src.Copy();
		}

		return *this;
	}

	/// Returns the owned BSTR while keeping its ownership
	operator BSTR() const { return m_bstrBuf; }

	// retrieve a copy of our string - caller must SysFreeString() it later!
	wxDEPRECATED_MSG("use Copy() instead")
		BSTR Get() const { return Copy(); }
private:
	// actual string
	BSTR m_bstrBuf;
};

//*********************************************************************************************************************
//*                                                      Converting BSTR and wxString                                 *
//*********************************************************************************************************************

BSTR wxConvertStringToOle(const wxString& str)
{
	return wxBasicString(str).Detach();
}

wxString wxConvertStringFromOle(const BSTR& bStr)
{
	// NULL BSTR is equivalent to an empty string (this is the convention used
	// by VB and hence we must follow it)
	if (!bStr) return wxString();

	const unsigned int len = SysStringLen(bStr);

#if wxUSE_UNICODE
	wxString str(bStr, len);
#else
	wxString str;
	if (len)
	{
		wxStringBufferLength buf(str, len); // asserts if len == 0
		buf.SetLength(WideCharToMultiByte(CP_ACP, 0 /* no flags */,
			bStr, len /* not necessarily NUL-terminated */,
			buf, len,
			NULL, NULL /* no default char */));
	}
#endif

	return str;
}

#endif 

wxIMPLEMENT_DYNAMIC_CLASS(CValueOLE, CValue);

//*********************************************************************************************************************
//*                                                      OLE Value                                                    *
//*********************************************************************************************************************

void CValueOLE::GetInterfaceAndReleaseStream()
{
#ifdef __WXMSW__
	//for (auto dispOle : s_valueOLE) {
	//	CValueOLE *oleValue = dispOle.second;
	//	if (!oleValue->m_streamThreading)
	//		continue;
	//	HRESULT hr = ::CoGetInterfaceAndReleaseStream(oleValue->m_streamThreading, IID_IDispatch, (void **)&oleValue->m_dispatchThreading);
	//	if (SUCCEEDED(hr)) {
	//		oleValue->m_streamThreading = NULL;
	//	}
	//}
#endif
}

void CValueOLE::CreateStreamForDispatch()
{
#ifdef __WXMSW__
	//for (auto dispOle : s_valueOLE)
	//{
	//	CValueOLE *oleValue = dispOle.second;
	//	if (oleValue->m_streamThreading) continue;
	//	HRESULT hr = ::CoMarshalInterThreadInterfaceInStream(IID_IDispatch, oleValue->m_dispatch, &oleValue->m_streamThreading);
	//	if (FAILED(hr)) oleValue->m_streamThreading = NULL;
	//}
#endif
}

void CValueOLE::ReleaseCoObjects()
{
#ifdef __WXMSW__
	for (auto dispOle : s_valueOLE) {
		CValueOLE* oleValue = dispOle.second;
		if (!oleValue->m_dispatch)
			continue;
		unsigned int countRef = oleValue->m_dispatch->Release();
		for (unsigned int i = 0; i < countRef; i++) {
			oleValue->m_dispatch->Release();
		}
		oleValue->m_dispatch = NULL;
	}
#endif
}

#ifdef __WXMSW__

void CValueOLE::AddFromArray(CValue& pvarRetValue, long* aPos, SAFEARRAY* psa, SAFEARRAYBOUND* safeArrayBound, int nLastDim) const
{
	VARIANT var = { 0 }; HRESULT hr;
	if (hr = ::SafeArrayGetElement(psa, aPos, &var))
		throw hr;
	aPos[nLastDim]++;
	if (aPos[nLastDim] > safeArrayBound[nLastDim].cElements) {
		aPos[nLastDim] = safeArrayBound[nLastDim].lLbound;
		//AddFromArray(CValue &Ret,long *aPos,SAFEARRAY *pArray,SAFEARRAYBOUND *aDims,int nLastDim)
	}
	/*
			AddFromArray(pArray,nMin
			if(i+1<nDim)
			{
				CValue Val;
				Val.CreateObject("array");
				Ret
			}
	*/
}

bool CValueOLE::FromVariant(const VARIANT& oleVariant, CValue& pvarRetValue) const
{
	switch (oleVariant.vt)
	{
	case VT_BOOL:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_BOOLEAN);
		pvarRetValue.m_bData = oleVariant.boolVal != 0;
		return true;
	}
	case VT_UI1:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.bVal;
		return true;
	}
	case VT_I2:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.iVal;
		return true;
	}
	case VT_I4:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.lVal;
		return true;
	}
#if wxUSE_LONGLONG
	case VT_I8:
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.llVal;
		return true;
#endif // wxUSE_LONGLONG
	case VT_R4:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.fltVal;
		return true;
	}
	case VT_R8:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.dblVal;
		return true;
	}
	case VT_I1:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.cVal;
		break;
	}
	case VT_UI2:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.uiVal;
		return true;
	}
	case VT_UI4:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
#if !defined _M_X64 && !defined __x86_64__
		pvarRetValue.m_fData = ttmath::ulint(oleVariant.ulVal);
#else 
		pvarRetValue.m_fData = uint64_t(oleVariant.ulVal);
#endif
		return true;
	}
	case VT_INT:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.intVal;
		return true;
	}
	case VT_UINT:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_NUMBER);
		pvarRetValue.m_fData = oleVariant.uintVal;
		return true;
	}
	case VT_BSTR:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_STRING);
		pvarRetValue.m_sData = wxConvertStringFromOle(oleVariant.bstrVal);
		return true;
	}
	case VT_DATE:
	{
		pvarRetValue.SetType(eValueTypes::TYPE_DATE);
#if wxUSE_DATETIME
		{
			SYSTEMTIME st;
			VariantTimeToSystemTime(oleVariant.date, &st);
			wxDateTime date;
			date.SetFromMSWSysTime(st);
			wxLongLong llValue = date.GetValue();
			pvarRetValue.m_dData = llValue.GetValue();
		}
#endif // wxUSE_DATETIME
		return true;
	}
	case VT_DISPATCH:
		if (oleVariant.pdispVal != NULL)
			pvarRetValue = new CValueOLE(m_clsId, oleVariant.pdispVal, m_objectName);
		return true;
	case VT_SAFEARRAY:
		if (oleVariant.parray != NULL)
			pvarRetValue = FromVariantArray(oleVariant.parray);
		return true;
	case VT_NULL:
		pvarRetValue.SetType(eValueTypes::TYPE_NULL);
		return true;
	case VT_EMPTY:
		pvarRetValue.Reset();
		return true;
	}

	return false;
}

#include "valueArray.h"

CValue CValueOLE::FromVariantArray(SAFEARRAY* psa) const
{
	HRESULT hr;

	long nDim = SafeArrayGetDim(psa);
	long* aPos = new long[nDim];

	SAFEARRAYBOUND* aDims = new SAFEARRAYBOUND[nDim];
	for (int i = 0; i < nDim; i++) {
		long nMin, nMax;
		if (hr = SafeArrayGetLBound(psa, i + 1, &nMin))
			throw hr;
		if (hr = SafeArrayGetUBound(psa, i + 1, &nMax))
			throw hr;
		aPos[i] = nMin;//начальное положение
		aDims[i].lLbound = nMin;
		aDims[i].cElements = nMax;//-nMin+1;
	}

	CValue cRet = new CValueArray;
	AddFromArray(cRet, aPos, psa, aDims, nDim - 1);

	delete[]aPos;
	delete[]aDims;

	return cRet;
}

VARIANT CValueOLE::FromValue(const CValue& varRetValue) const
{
	VARIANT oleVariant = { 0 };

	switch (varRetValue.GetType())
	{
	case eValueTypes::TYPE_EMPTY:
		oleVariant.vt = VT_EMPTY;
		break;
	case eValueTypes::TYPE_NULL:
		oleVariant.vt = VT_NULL;
		break;
	case eValueTypes::TYPE_BOOLEAN:
	{
		oleVariant.vt = VT_BOOL;
		oleVariant.boolVal = varRetValue.GetBoolean();
		break;
	}
	case eValueTypes::TYPE_NUMBER:
	{
		number_t fData = varRetValue.GetNumber();
		if (fData == fData.Round()) {
			oleVariant.vt = VT_I4;
			oleVariant.lVal = fData.ToInt();
		}
		else {
			oleVariant.vt = VT_R8;
			oleVariant.dblVal = fData.ToDouble();
		}
		break;
	}
	case eValueTypes::TYPE_STRING:
	{
		oleVariant.vt = VT_BSTR;
		oleVariant.bstrVal = wxConvertStringToOle(varRetValue.GetString());
		break;
	}
	case eValueTypes::TYPE_DATE:
	{
#if wxUSE_DATETIME
		wxDateTime date(wxLongLong(varRetValue.GetDate()));
		oleVariant.vt = VT_DATE;
		SYSTEMTIME st;
		date.GetAsMSWSysTime(&st);
		SystemTimeToVariantTime(&st, &oleVariant.date);
#endif
		break;
	}
	case eValueTypes::TYPE_OLE: {
		CValueOLE* valueOLE = NULL;
		if (varRetValue.ConvertToValue(valueOLE)) {
			oleVariant.vt = VT_DISPATCH;
			oleVariant.pdispVal = valueOLE->GetDispatch();
			oleVariant.pdispVal->AddRef();
		}
		else {
			oleVariant.vt = VT_ERROR;
		}
		break;
	}
	}

	return oleVariant;
}

void CValueOLE::FreeValue(VARIANT& varValue)
{
	switch (varValue.vt)
	{
	case VT_BSTR:
		SysFreeString(varValue.bstrVal);
		break;
	}
}

IDispatch* CValueOLE::DoCreateInstance()
{
	// get the server IDispatch interface
	//
	// NB: using CLSCTX_INPROC_HANDLER results in failure when getting
	//     Automation interface for Microsoft Office applications so don't use
	//     CLSCTX_ALL which includes it

	IDispatch* pDispatch = NULL;
	HRESULT hr = ::CoCreateInstance(m_clsId, NULL, CLSCTX_SERVER,
		IID_IDispatch, (void**)&pDispatch);

	if (FAILED(hr)) {
		wxLogSysError(hr, _("Failed to create an instance of \"%s\""), m_objectName);
		return NULL;
	}

	return pDispatch;
}

#endif 

#ifdef __WXMSW__

CValueOLE::CValueOLE() : CValue(eValueTypes::TYPE_OLE),
m_clsId({ 0 }), m_dispatch(NULL),
m_methodHelper(new CMethodHelper()), m_objectName(wxEmptyString)
{
}

CValueOLE::CValueOLE(const CLSID& clsId, IDispatch* dispatch, const wxString& objectName) : CValue(eValueTypes::TYPE_OLE),
m_clsId(clsId), m_dispatch(dispatch),
m_methodHelper(new CMethodHelper()), m_objectName(objectName)
{
	if (m_dispatch != NULL) {
		m_dispatch->AddRef();
	}
	PrepareNames();
}

#else 
CValueOLE::CValueOLE() : CValue(eValueTypes::TYPE_OLE), m_methodHelper(new CMethodHelper()), m_objectName(wxEmptyString) {}
#endif 

CValueOLE::~CValueOLE()
{
#ifdef __WXMSW__
	if (m_dispatch != NULL) {
		s_valueOLE.erase(m_dispatch);
		m_dispatch->Release(); m_dispatch = NULL;
	}
#endif 

	wxDELETE(m_methodHelper);
}

bool CValueOLE::Init(CValue** paParams, const long lSizeArray)
{
	const wxString& oleName = paParams[0]->GetString();
#ifdef __WXMSW__
	if (!Create(oleName))
		CTranslateError::Error("Failed to create an instance of '%s'", oleName);
	return true;
#else
	return false;
#endif
}

bool CValueOLE::Create(const wxString& oleName)
{
	if (appData->DesignerMode())
		return true;

#ifdef __WXMSW__
	if (m_dispatch != NULL)
		return false;
#endif

#ifdef __WXMSW__

	HRESULT hr = ::CLSIDFromProgID(oleName, &m_clsId);
	if (FAILED(hr))
		return false;

	BSTR strClassName = NULL;
	if (SUCCEEDED(::ProgIDFromCLSID(m_clsId, &strClassName))) {
		m_objectName = strClassName;
		// free memory allocated by ProgIDFromCLSID 
		CoTaskMemFree(strClassName);
	}
	else {
		return false;
	}
	m_dispatch = DoCreateInstance();
	PrepareNames();
	s_valueOLE.insert_or_assign(m_dispatch, this);
	return m_dispatch != NULL;
#else 
	return false;
#endif
}

void CValueOLE::PrepareNames() const
{
#ifdef __WXMSW__
	
	m_methodHelper->ClearHelper();
	
	if (m_dispatch == NULL)
		return;
	
	unsigned int count = 0;
	
	if (m_dispatch->GetTypeInfoCount(&count) != S_OK)
		return;
	
	for (unsigned int i = 0; i < count; i++) {

		HRESULT hr = S_OK;
		ITypeInfo* typeInfo = NULL; TYPEATTR* typeAttr = NULL;

		hr = m_dispatch->GetTypeInfo(i, LOCALE_SYSTEM_DEFAULT, &typeInfo);
		if (hr != S_OK)
			continue;

		hr = typeInfo->GetTypeAttr(&typeAttr);
		if (hr != S_OK)
			continue;

		if (typeAttr->typekind == TKIND_INTERFACE ||
			typeAttr->typekind == TKIND_DISPATCH) {
			for (unsigned int m = 0; m < (UINT)typeAttr->cFuncs; m++)
			{
				FUNCDESC* funcInfo = NULL;
				hr = typeInfo->GetFuncDesc(m, &funcInfo);

				if (hr != S_OK) continue;
				if (funcInfo->invkind != INVOKE_FUNC && funcInfo->invkind != INVOKE_PROPERTYGET)
					continue;

				BSTR strName = NULL;
				// Получаем название метода
				typeInfo->GetDocumentation(funcInfo->memid, &strName,
					NULL, NULL, NULL);

				wxString methodName = wxConvertStringFromOle(strName);
				SysFreeString(strName);
				wxString methodHelper = methodName;

				if (methodName.Length() > 2) {
					wxString sMethodL = methodName.Left(2);
					sMethodL.MakeLower();
					if (sMethodL[1] == methodName[1]) {
						methodName[0] = sMethodL[0];
						methodHelper[0] = sMethodL[0];
					}
				}
				else {
					methodName.MakeLower();
					methodHelper.MakeLower();
				}

				methodHelper += wxT("(");

				for (unsigned int k = 0; k < (UINT)funcInfo->cParams; k++) {
					VARTYPE vt = funcInfo->lprgelemdescParam[k].tdesc.vt & ~0xF000;
					if (vt <= VT_CLSID)
					{
						switch (vt)
						{
						case VT_BOOL:
							methodHelper += wxT("bool");
							break;
						case VT_UI1:
						case VT_I2:
						case VT_I4:
#if wxUSE_LONGLONG
						case VT_I8:
#endif // wxUSE_LONGLONG
						case VT_R4:
						case VT_R8:
						case VT_I1:
						case VT_UI2:
						case VT_UI4:
						case VT_INT:
						case VT_UINT:
							methodHelper += wxT("number");
							break;
						case VT_BSTR:
							methodHelper += wxT("string");
							break;
						case VT_DATE:
							methodHelper += wxT("date");
							break;
						case VT_DISPATCH:
							methodHelper += wxT("oleRef");
							break;
						case VT_PTR:
							methodHelper += wxT("olePtr");
							break;
						case VT_SAFEARRAY:
							methodHelper += wxT("oleArray");
							break;
						case VT_NULL:
							methodHelper += wxT("null");
							break;
						case VT_EMPTY:
							methodHelper += wxT("empty");
							break;
						default: methodHelper += wxT("other"); break;
						}
					}
					else methodHelper += wxT("badType");
					if (k < (UINT)funcInfo->cParams - 1) {
						methodHelper += wxT(", ");
					}
				}
				methodHelper += wxT(")");
				if (funcInfo->invkind == INVOKE_FUNC) {
					m_methodHelper->AppendFunc(
						methodName,
						funcInfo->cParams,
						methodHelper,
						funcInfo->memid
					);
				}
				else if (funcInfo->invkind == INVOKE_PROPERTYGET) {
					m_methodHelper->AppendProp(
						methodName,
						funcInfo->memid
					);
				}
			}
		}
		typeInfo->ReleaseTypeAttr(typeAttr);
		typeInfo->Release();
	}
#endif 
}

long CValueOLE::FindProp(const wxString& sName) const
{
#ifdef __WXMSW__

	if (!m_dispatch)
		return wxNOT_FOUND;

	LPOLESTR cbstrName = (WCHAR*)sName.wc_str();
	DISPID dispid = -1;
	OLECHAR FAR* szMember = cbstrName;

	HRESULT hr = m_dispatch->GetIDsOfNames(
		IID_NULL,
		&szMember,
		1, LOCALE_SYSTEM_DEFAULT,
		&dispid);

	if (hr != S_OK || dispid == 1000)
		return wxNOT_FOUND;

	return dispid;

#else 
	return wxNOT_FOUND;
#endif 
}

#ifdef __WXMSW__

static HRESULT PutProperty(
	IDispatch* pDisp,
	DISPID dwDispID,
	VARIANT* pVar)
{
	DISPPARAMS dispparams = { NULL, NULL, 1, 1 };
	dispparams.rgvarg = pVar;
	DISPID dispidPut = DISPID_PROPERTYPUT;
	dispparams.rgdispidNamedArgs = &dispidPut;

	if (pVar->vt == VT_UNKNOWN
		|| pVar->vt == VT_DISPATCH
		|| (pVar->vt & VT_ARRAY)
		|| (pVar->vt & VT_BYREF)) {
		HRESULT hr = pDisp->Invoke(
			dwDispID,
			IID_NULL,
			LOCALE_SYSTEM_DEFAULT,
			DISPATCH_PROPERTYPUTREF,
			&dispparams,
			NULL,
			NULL,
			NULL
		);
		if (SUCCEEDED(hr))
			return hr;
	}

	return pDisp->Invoke(
		dwDispID,
		IID_NULL,
		LOCALE_SYSTEM_DEFAULT,
		DISPATCH_PROPERTYPUT,
		&dispparams,
		NULL,
		NULL,
		NULL
	);
};

#endif 

bool CValueOLE::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
#ifdef __WXMSW__
	if (!m_dispatch)
		return false;
	VARIANT oleVariant = FromValue(varPropVal);
	HRESULT hr = PutProperty(m_dispatch, lPropNum, &oleVariant);
	FreeValue(oleVariant);
	return hr == S_OK;
#else 
	return false;
#endif 
}

bool CValueOLE::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
#ifdef __WXMSW__

	if (m_dispatch == NULL)
		return false;

	VARIANT oleVariant = { 0 };
	DISPPARAMS dp = { NULL, NULL, 0, 0 };

	unsigned int uiArgErr = 0;

	EXCEPINFO FAR ExcepInfo;
	wxSecureZeroMemory(&ExcepInfo, sizeof(EXCEPINFO));

	HRESULT hr = m_dispatch->Invoke(
		lPropNum,
		IID_NULL,
		LOCALE_SYSTEM_DEFAULT,
		DISPATCH_PROPERTYGET,
		&dp,
		&oleVariant,
		&ExcepInfo,
		&uiArgErr
	);

	if (hr != S_OK) {
		CTranslateError::Error("%s:\n%s", ExcepInfo.bstrSource, ExcepInfo.bstrDescription);
		return false;
	}

	return FromVariant(oleVariant, pvarPropVal);

#else 
	return false;
#endif 
}

bool CValueOLE::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
#ifdef __WXMSW__

	if (m_dispatch == NULL)
		return false;

	VARIANT oleVariant = { 0 };

	//переводим параметры в тип VARIANT
	VARIANT* pvarArgs = new VARIANT[lSizeArray];
	for (long arg = 0; arg < lSizeArray; arg++)
		pvarArgs[lSizeArray - arg - 1] = FromValue(paParams[arg]);

	unsigned int uiArgErr = 0;
	EXCEPINFO FAR ExcepInfo;
	wxSecureZeroMemory(&ExcepInfo, sizeof(EXCEPINFO));

	DISPPARAMS dispparams = { pvarArgs, NULL, (UINT)lSizeArray,  0 };

	HRESULT hr = m_dispatch->Invoke(lMethodNum,
		IID_NULL,
		LOCALE_SYSTEM_DEFAULT,
		DISPATCH_METHOD | DISPATCH_PROPERTYGET,
		&dispparams, &oleVariant, &ExcepInfo, &uiArgErr);

	for (long arg = 0; arg < lSizeArray; arg++)
		FreeValue(pvarArgs[arg]);

	delete[]pvarArgs;

	if (hr != S_OK) {
		switch (hr)
		{
		case DISP_E_EXCEPTION:
			CTranslateError::Error("%s:\n%s", ExcepInfo.bstrSource, ExcepInfo.bstrDescription);
			return false;
		case DISP_E_BADPARAMCOUNT:
			CTranslateError::Error("The number of elements provided to DISPPARAMS is different from the number of arguments accepted by the method or property.");
			return false;
		case DISP_E_BADVARTYPE:
			CTranslateError::Error("One of the arguments in rgvarg is not a valid variant type. ");
			return false;
		case DISP_E_MEMBERNOTFOUND:
			CTranslateError::Error("The requested member does not exist, or the call to Invoke tried to set the value of a read-only property.");
			return false;
		case DISP_E_NONAMEDARGS:
			CTranslateError::Error("This implementation of IDispatch does not support named arguments.");
			return false;
		case DISP_E_OVERFLOW:
			CTranslateError::Error("One of the arguments in rgvarg could not be coerced to the specified type.");
			return false;
		case DISP_E_PARAMNOTFOUND:
			CTranslateError::Error("One of the parameter DISPIDs does not correspond to a parameter on the method. In this case, puArgErr should be set to the first argument that contains the error.");
			return false;
		case DISP_E_TYPEMISMATCH:
			CTranslateError::Error("One or more of the arguments could not be coerced. The index within rgvarg of the first parameter with the incorrect type is returned in the puArgErr parameter.");
			return false;
		case DISP_E_UNKNOWNINTERFACE:
			CTranslateError::Error("The interface identifier passed in riid is not IID_NULL.)");
			return false;
		case DISP_E_UNKNOWNLCID:
			CTranslateError::Error("The member being invoked interprets string arguments according to the LCID, and the LCID is not recognized. If the LCID is not needed to interpret arguments, this error should not be returned.");
			return false;
		case DISP_E_PARAMNOTOPTIONAL:
			CTranslateError::Error("A required parameter was omitted.");
			return false;
		default:
			CTranslateError::Error("Error: get method return unknown code");
			return false;
		}
	}

	return FromVariant(oleVariant, pvarRetValue);

#else 
	return false;
#endif
}

#pragma warning(pop)

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueOLE, "comObject", TEXT2CLSID("VL_OLE"));
