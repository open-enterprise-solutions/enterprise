////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, Vadim Zeitlin, 2C-Team
//	Description : OLE-supporter
////////////////////////////////////////////////////////////////////////////

#include "valueole.h"
#include "functions.h"
#include "methods.h"
#include "appData.h"

#include <wx/clipbrd.h>

#pragma warning(push)
#pragma warning(disable : 4018)

#ifdef __WXMSW__

static std::map<IDispatch *, CValueOLE *> s_aOLEValues;

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

wxString wxConvertStringFromOle(const BSTR &bStr)
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
	//for (auto dispOle : s_aOLEValues) {
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
	//for (auto dispOle : s_aOLEValues)
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
	for (auto dispOle : s_aOLEValues) {
		CValueOLE *oleValue = dispOle.second;
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

void CValueOLE::AddFromArray(CValue &Ret, long *aPos, SAFEARRAY *pArray, SAFEARRAYBOUND *aDims, int nLastDim)
{
	VARIANT Val = { 0 }; HRESULT hr;

	if (hr = ::SafeArrayGetElement(pArray, aPos, &Val)) throw hr;

	aPos[nLastDim]++;

	if (aPos[nLastDim] > aDims[nLastDim].cElements)
	{
		aPos[nLastDim] = aDims[nLastDim].lLbound;
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

CValue CValueOLE::FromVariant(VARIANT &oleVariant)
{
	CValue vObject;

	switch (oleVariant.vt)
	{
	case VT_BOOL:
	{
		vObject.SetType(eValueTypes::TYPE_BOOLEAN);
		vObject.m_bData = oleVariant.boolVal != 0;
		break;
	}
	case VT_UI1:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.bVal;
		break;
	}
	case VT_I2:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.iVal;
		break;
	}
	case VT_I4:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.lVal;
		break;
	}
#if wxUSE_LONGLONG
	case VT_I8:
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.llVal;
		break;
#endif // wxUSE_LONGLONG
	case VT_R4:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.fltVal;
		break;
	}
	case VT_R8:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.dblVal;
		break;
	}
	case VT_I1:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.cVal;
		break;
	}
	case VT_UI2:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.uiVal;
		break;
	}
	case VT_UI4:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
#if !defined _M_X64 && !defined __x86_64__
		vObject.m_fData = ttmath::ulint(oleVariant.ulVal);
#else 
		vObject.m_fData = uint64_t(oleVariant.ulVal);
#endif
		break;
	}
	case VT_INT:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.intVal;
		break;
	}
	case VT_UINT:
	{
		vObject.SetType(eValueTypes::TYPE_NUMBER);
		vObject.m_fData = oleVariant.uintVal;
		break;
	}
	case VT_BSTR:
	{
		vObject.SetType(eValueTypes::TYPE_STRING);
		vObject.m_sData = wxConvertStringFromOle(oleVariant.bstrVal);
		break;
	}
	case VT_DATE:
	{
		vObject.SetType(eValueTypes::TYPE_DATE);
#if wxUSE_DATETIME
		{
			SYSTEMTIME st;
			VariantTimeToSystemTime(oleVariant.date, &st);

			wxDateTime date;
			date.SetFromMSWSysTime(st);

			wxLongLong m_llValue = date.GetValue();
			vObject.m_dData = m_llValue.GetValue();
		}
#endif // wxUSE_DATETIME
		break;
	}
	case VT_DISPATCH:
	{
		if (oleVariant.pdispVal) {
			vObject = new CValueOLE(m_clsId, oleVariant.pdispVal, m_sObjectName);
		}
		break;
	}
	case VT_SAFEARRAY:
	{
		if (oleVariant.parray)
			vObject = FromVariantArray(oleVariant.parray);
		break;
	}
	case VT_NULL: vObject.SetType(eValueTypes::TYPE_NULL); break;
	case VT_EMPTY: vObject.Reset(); break;
	}

	return vObject;
}

CValue CValueOLE::FromVariantArray(SAFEARRAY *pArray)
{
	HRESULT hr;

	CValue cRet = CValue::CreateObject("array");

	long nDim = SafeArrayGetDim(pArray);
	long *aPos = new long[nDim];

	SAFEARRAYBOUND *aDims = new SAFEARRAYBOUND[nDim];

	for (int i = 0; i < nDim; i++)
	{
		long nMin, nMax;

		if (hr = SafeArrayGetLBound(pArray, i + 1, &nMin))
			throw hr;

		if (hr = SafeArrayGetUBound(pArray, i + 1, &nMax))
			throw hr;

		aPos[i] = nMin;//начальное положение
		aDims[i].lLbound = nMin;
		aDims[i].cElements = nMax;//-nMin+1;
	}

	AddFromArray(cRet, aPos, pArray, aDims, nDim - 1);

	delete[]aPos;
	delete[]aDims;

	return cRet;
}

VARIANT CValueOLE::FromValue(CValue &cVal)
{
	VARIANT oleVariant = { 0 };

	switch (cVal.GetType())
	{
	case eValueTypes::TYPE_EMPTY: oleVariant.vt = VT_EMPTY; break;
	case eValueTypes::TYPE_NULL: oleVariant.vt = VT_NULL; break;
	case eValueTypes::TYPE_BOOLEAN:
	{
		oleVariant.vt = VT_BOOL;
		oleVariant.boolVal = cVal.GetBoolean();
		break;
	}
	case eValueTypes::TYPE_NUMBER:
	{
		number_t fData = cVal.GetNumber();
		if (fData == fData.Round())
		{
			oleVariant.vt = VT_I4;
			oleVariant.lVal = fData.ToInt();
		}
		else
		{
			oleVariant.vt = VT_R8;
			oleVariant.dblVal = fData.ToDouble();
		}
		break;
	}
	case eValueTypes::TYPE_STRING:
	{
		oleVariant.vt = VT_BSTR;
		oleVariant.bstrVal = wxConvertStringToOle(cVal.GetString());
		break;
	}
	case eValueTypes::TYPE_DATE:
	{
#if wxUSE_DATETIME
		wxDateTime date(wxLongLong(cVal.GetDate()));
		oleVariant.vt = VT_DATE;
		SYSTEMTIME st;
		date.GetAsMSWSysTime(&st);
		SystemTimeToVariantTime(&st, &oleVariant.date);
#endif
		break;
	}
	case eValueTypes::TYPE_OLE:
	{
		CValueOLE *m_pValueOLE = dynamic_cast<CValueOLE *>(cVal.GetRef());
		if (m_pValueOLE)
		{
			oleVariant.vt = VT_DISPATCH;
			oleVariant.pdispVal = m_pValueOLE->GetDispatch();
			oleVariant.pdispVal->AddRef();
		}
		else
		{
			oleVariant.vt = VT_ERROR;
		}
		break;
	}
	}

	return oleVariant;
}

void CValueOLE::FreeValue(VARIANT &cVal)
{
	switch (cVal.vt)
	{
	case VT_BSTR: SysFreeString(cVal.bstrVal); break;
	}
}

IDispatch *CValueOLE::DoCreateInstance()
{
	// get the server IDispatch interface
	//
	// NB: using CLSCTX_INPROC_HANDLER results in failure when getting
	//     Automation interface for Microsoft Office applications so don't use
	//     CLSCTX_ALL which includes it

	IDispatch *pDispatch = NULL;
	HRESULT hr = ::CoCreateInstance(m_clsId, NULL, CLSCTX_SERVER,
		IID_IDispatch, (void **)&pDispatch);

	if (FAILED(hr))
	{
		wxLogSysError(hr, _("Failed to create an instance of \"%s\""), m_sObjectName);
		return NULL;
	}

	return pDispatch;
}

#endif 

#ifdef __WXMSW__

CValueOLE::CValueOLE() : CValue(eValueTypes::TYPE_OLE),
m_clsId({ 0 }),
m_dispatch(NULL),
m_methods(new CMethods()),
m_sObjectName(wxEmptyString)
{
}

CValueOLE::CValueOLE(CLSID clsId, IDispatch *dispatch, const wxString &objectName) : CValue(eValueTypes::TYPE_OLE),
m_clsId(clsId),
m_dispatch(dispatch),
m_methods(new CMethods()),
m_sObjectName(objectName)
{
	if (m_dispatch) {
		m_dispatch->AddRef();
	}
	PrepareNames();
}

#else 
CValueOLE::CValueOLE() : CValue(eValueTypes::TYPE_OLE), m_methods(new CMethods()), m_sObjectName(wxEmptyString) {}
#endif 

CValueOLE::~CValueOLE()
{
#ifdef __WXMSW__
	if (m_dispatch) {
		s_aOLEValues.erase(m_dispatch);

		m_dispatch->Release();
		m_dispatch = NULL;
	}
#endif 
	wxDELETE(m_methods);
}

bool CValueOLE::Init(CValue **aParams)
{
	wxString sObject = aParams[0]->GetString();
#ifdef __WXMSW__
	if (!Create(sObject))
		CTranslateError::Error(_("Failed to create an instance of '%s'"), sObject.wc_str());
	return true;
#else
	return false;
#endif
}

bool CValueOLE::Create(const wxString &sName)
{
	if (appData->DesignerMode())
		return true;

#ifdef __WXMSW__
	if (m_dispatch)
		return false;
#endif

#ifdef __WXMSW__

	HRESULT hr = CLSIDFromProgID(sName, &m_clsId);
	if (FAILED(hr)) return false;

	BSTR strClassName = NULL;
	if (SUCCEEDED(ProgIDFromCLSID(m_clsId, &strClassName))) {
		m_sObjectName = strClassName;
		// free memory allocated by ProgIDFromCLSID 
		CoTaskMemFree(strClassName);
	}
	else {
		return false;
	}

	m_dispatch = DoCreateInstance();

	PrepareNames();

	if (wxThread::IsMain()) {
		s_aOLEValues.insert_or_assign(m_dispatch, this);
	}

	return m_dispatch != NULL;
#else 
	return false;
#endif
}

CMethods *CValueOLE::GetPMethods() const { return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов

void CValueOLE::PrepareNames() const
{
#ifdef __WXMSW__
	if (!m_dispatch)
		return; unsigned int count = 0;
	if (m_dispatch->GetTypeInfoCount(&count) != S_OK)
		return;
	std::vector<SEng> aMethods, aAttributes;
	for (unsigned int i = 0; i < count; i++) {
		HRESULT hr = S_OK;
		ITypeInfo *typeInfo = NULL; TYPEATTR *typeAttr = NULL;
		hr = m_dispatch->GetTypeInfo(i, LOCALE_SYSTEM_DEFAULT, &typeInfo);
		if (hr != S_OK) continue;
		hr = typeInfo->GetTypeAttr(&typeAttr);
		if (hr != S_OK) continue;

		if (typeAttr->typekind == TKIND_INTERFACE ||
			typeAttr->typekind == TKIND_DISPATCH)
		{
			for (unsigned int m = 0; m < (UINT)typeAttr->cFuncs; m++)
			{
				FUNCDESC *funcInfo = NULL;
				hr = typeInfo->GetFuncDesc(m, &funcInfo);

				if (hr != S_OK) continue;
				if (funcInfo->invkind != INVOKE_FUNC && funcInfo->invkind != INVOKE_PROPERTYGET) continue;

				BSTR strName = NULL;

				// Получаем название метода
				typeInfo->GetDocumentation(funcInfo->memid, &strName,
					NULL, NULL, NULL);

				wxString sMethod = wxConvertStringFromOle(strName); wxString sMethodDescription = sMethod;
				SysFreeString(strName);

				if (sMethod.Length() > 2) {
					wxString sMethodL = sMethod.Left(2);
					sMethodL.MakeLower();
					if (sMethodL[1] == sMethod[1]) {
						sMethod[0] = sMethodL[0];
						sMethodDescription[0] = sMethodL[0];
					}
				}
				else {
					sMethod.MakeLower();
					sMethodDescription.MakeLower();
				}

				sMethodDescription += wxT("(");

				for (unsigned int k = 0; k < (UINT)funcInfo->cParams; k++) {
					VARTYPE vt = funcInfo->lprgelemdescParam[k].tdesc.vt & ~0xF000;
					if (vt <= VT_CLSID)
					{
						switch (vt)
						{
						case VT_BOOL: sMethodDescription += wxT("bool"); break;
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
						case VT_UINT: sMethodDescription += wxT("number"); break;
						case VT_BSTR: sMethodDescription += wxT("string"); break;
						case VT_DATE: sMethodDescription += wxT("date"); break;
						case VT_DISPATCH: sMethodDescription += wxT("oleRef"); break;
						case VT_PTR: sMethodDescription += wxT("olePtr"); break;
						case VT_SAFEARRAY: sMethodDescription += wxT("oleArray"); break;
						case VT_NULL: sMethodDescription += wxT("null"); break;
						case VT_EMPTY: sMethodDescription += wxT("empty"); break;
						default: sMethodDescription += wxT("other"); break;
						}
					}
					else sMethodDescription += wxT("badType");

					if (k < (UINT)funcInfo->cParams - 1) {
						sMethodDescription += wxT(", ");
					}
				}

				sMethodDescription += wxT(")");

				if (funcInfo->invkind == INVOKE_FUNC) {
					aMethods.push_back({ sMethod, sMethodDescription, wxT("ole"), funcInfo->memid });
				}
				else if (funcInfo->invkind == INVOKE_PROPERTYGET) {
					aAttributes.push_back({ sMethod, wxEmptyString, wxT("ole"), funcInfo->memid });
				}
			}
		}

		typeInfo->ReleaseTypeAttr(typeAttr);
		typeInfo->Release();
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	m_methods->PrepareMethods(aMethods.data(), aMethods.size());

#endif 
}

int CValueOLE::FindAttribute(const wxString &sName) const
{
#ifdef __WXMSW__

	if (!m_dispatch)
		return wxNOT_FOUND;

	LPOLESTR cbstrName = (WCHAR *)sName.wc_str();
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

CValue CValueOLE::GetAttribute(attributeArg_t &aParams)
{
#ifdef __WXMSW__

	VARIANT vRes = { 0 };
	DISPPARAMS dp = { NULL, NULL, 0, 0 };

	if (!m_dispatch)
		return CValue();

	unsigned int uiArgErr = 0;

	EXCEPINFO FAR ExcepInfo;
	ZeroMemory(&ExcepInfo, sizeof(EXCEPINFO));

	HRESULT hr = m_dispatch->Invoke(
		aParams.GetIndex(),
		IID_NULL,
		LOCALE_SYSTEM_DEFAULT,
		DISPATCH_PROPERTYGET,
		&dp,
		&vRes,
		&ExcepInfo,
		&uiArgErr
	);

	if (hr != S_OK) {
		CTranslateError::Error(_("%s:\n%s"), ExcepInfo.bstrSource, ExcepInfo.bstrDescription);
	}

	return FromVariant(vRes);

#else 
	return CValue();
#endif 
}

#ifdef __WXMSW__

static HRESULT PutProperty(
	IDispatch * pDisp,
	DISPID dwDispID,
	VARIANT *pVar
)
{
	DISPPARAMS dispparams = { NULL, NULL, 1, 1 };
	dispparams.rgvarg = pVar;
	DISPID dispidPut = DISPID_PROPERTYPUT;
	dispparams.rgdispidNamedArgs = &dispidPut;

	if (pVar->vt == VT_UNKNOWN
		|| pVar->vt == VT_DISPATCH
		|| (pVar->vt & VT_ARRAY)
		|| (pVar->vt & VT_BYREF))
	{
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

void CValueOLE::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
#ifdef __WXMSW__
	if (!m_dispatch) return;
	VARIANT var = FromValue(cVal);
	PutProperty(m_dispatch, aParams.GetIndex(), &var);
	FreeValue(var);
#endif 
}

int CValueOLE::FindMethod(const wxString &sName) const
{
	return FindAttribute(sName);
}

CValue CValueOLE::Method(methodArg_t &aParams)
{
#ifdef __WXMSW__
	if (!m_dispatch)
		return CValue();

	VARIANT varRet = { 0 };
	unsigned int nParamCount = aParams.GetParamCount();

	//переводим параметры в тип VARIANT
	VARIANT *pvarArgs = new VARIANT[nParamCount];
	for (unsigned int i = 0; i < nParamCount; i++) {
		pvarArgs[nParamCount - i - 1] = FromValue(aParams[i]);
	}

	unsigned int uiArgErr = 0;

	EXCEPINFO FAR ExcepInfo;
	ZeroMemory(&ExcepInfo, sizeof(EXCEPINFO));

	DISPPARAMS dispparams = { pvarArgs, NULL, nParamCount,  0 };

	HRESULT hr = m_dispatch->Invoke(aParams.GetIndex(),
		IID_NULL,
		LOCALE_SYSTEM_DEFAULT,
		DISPATCH_METHOD | DISPATCH_PROPERTYGET,
		&dispparams, &varRet, &ExcepInfo, &uiArgErr);

	for (unsigned int i = 0; i < nParamCount; i++) {
		FreeValue(pvarArgs[i]);
	}

	delete[]pvarArgs;

	if (hr != S_OK)
	{
		switch (hr)
		{
		case DISP_E_EXCEPTION: CTranslateError::Error(_("%s:\n%s"), ExcepInfo.bstrSource, ExcepInfo.bstrDescription); break;
		case DISP_E_BADPARAMCOUNT: CTranslateError::Error(_("The number of elements provided to DISPPARAMS is different from the number of arguments accepted by the method or property.")); break;
		case DISP_E_BADVARTYPE: CTranslateError::Error(_("One of the arguments in rgvarg is not a valid variant type. ")); break;
		case DISP_E_MEMBERNOTFOUND: CTranslateError::Error(_("The requested member does not exist, or the call to Invoke tried to set the value of a read-only property.")); break;
		case DISP_E_NONAMEDARGS: CTranslateError::Error(_("This implementation of IDispatch does not support named arguments.")); break;
		case DISP_E_OVERFLOW: CTranslateError::Error(_("One of the arguments in rgvarg could not be coerced to the specified type.")); break;
		case DISP_E_PARAMNOTFOUND: CTranslateError::Error(_("One of the parameter DISPIDs does not correspond to a parameter on the method. In this case, puArgErr should be set to the first argument that contains the error.")); break;
		case DISP_E_TYPEMISMATCH: CTranslateError::Error(_("One or more of the arguments could not be coerced. The index within rgvarg of the first parameter with the incorrect type is returned in the puArgErr parameter.")); break;
		case DISP_E_UNKNOWNINTERFACE: CTranslateError::Error(_("The interface identifier passed in riid is not IID_NULL.)")); break;
		case DISP_E_UNKNOWNLCID: CTranslateError::Error(_("The member being invoked interprets string arguments according to the LCID, and the LCID is not recognized. If the LCID is not needed to interpret arguments, this error should not be returned.")); break;
		case DISP_E_PARAMNOTOPTIONAL: CTranslateError::Error(_("A required parameter was omitted.")); break;
		default: CTranslateError::Error(_("Error: get method return unknown code")); break;
		}
	}

	return FromVariant(varRet);

#else 
	return CValue();
#endif
}

#pragma warning(pop)

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueOLE, "comObject", TEXT2CLSID("VL_OLE"));
