#include "valueFile.h"

#include <wx/filename.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueFile, CValue);

CValue::CMethodHelper CValueFile::m_methodHelper;

void CValueFile::PrepareNames() const
{
	m_methodHelper.AppendConstructor(1, "file(string)");

	m_methodHelper.AppendProp("baseName");
	m_methodHelper.AppendProp("extension");
	m_methodHelper.AppendProp("fullName");
	m_methodHelper.AppendProp("name");
	m_methodHelper.AppendProp("path");

	m_methodHelper.AppendFunc("exist", "exist()");
	//m_methodHelper.AppendMethod("getHidden", "getHidden()");
	m_methodHelper.AppendFunc("getModificationTime", "getModificationTime()");
	m_methodHelper.AppendFunc("getReadOnly", "getReadOnly()");
	m_methodHelper.AppendFunc("isDirectory", "isDirectory()");
	m_methodHelper.AppendFunc("isFile", "isFile()");
	//m_methodHelper.AppendMethod("setHidden", "setHidden(bool)");
	//m_methodHelper.AppendMethod("setModificationTime", "setModificationTime(date)");
	//m_methodHelper.AppendMethod("setReadOnly", "setReadOnly(bool)");
	m_methodHelper.AppendFunc("size", "size()");
}

#include "appData.h"

bool CValueFile::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	wxFileName fileName(m_fileName);

	switch (lMethodNum)
	{
	case enExist:
		pvarRetValue = m_fileName.Length() > 0 &&
			(!fileName.IsDir() && fileName.Exists()) || (fileName.IsDir() && fileName.DirExists());
		return true;
		//case enGetHidden: break;
	case enGetModificationTime: pvarRetValue = fileName.GetModificationTime();
		return true;
	case enGetReadOnly: pvarRetValue = m_fileName.Length() > 0 &&
		(!fileName.IsDir() && fileName.IsFileReadable()) || (fileName.IsDir() && fileName.IsDirReadable());
		return true;
	case enIsDirectory: pvarRetValue = fileName.IsDir(); 		
		return true;
	case enIsFile: pvarRetValue = !fileName.IsDir(); 		
		return true;
		//case enSetHidden: break;
		//case enSetModificationTime: break;
		//case enSetReadOnly: break;
	case enSize: {
		wxULongLong size = fileName.GetSize();
		pvarRetValue = number_t(size.GetValue());
		return true;
	}
	}

	return false;
}

bool CValueFile::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CValueFile::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	wxFileName fileName(m_fileName);

	switch (lPropNum)
	{
	case enBaseName: 
		pvarPropVal = fileName.GetName();
		return true;
	case enExtension:
		pvarPropVal = fileName.GetExt();
		return true;
	case enFullName:
		pvarPropVal = fileName.GetFullPath(); 
		return true;
	case enName: 
		pvarPropVal = fileName.GetFullName(); 
		return true;
	case enPath: 
		pvarPropVal = fileName.GetPath();
		return true;
	}

	return false;
}

CValueFile::CValueFile() : CValue(eValueTypes::TYPE_VALUE)
{
}

CValueFile::~CValueFile()
{
}

bool CValueFile::Init(CValue** paParams, const long lSizeArray)
{
	m_fileName = paParams[0]->GetString();
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueFile, "file", TEXT2CLSID("VL_FILE"));
