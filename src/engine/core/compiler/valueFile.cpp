#include "valueFile.h"
#include "methods.h"

#include <wx/filename.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueFile, CValue);

enum {
	enBaseName,
	enExtension,
	enFullName,
	enName,
	enPath,
};

CMethods CValueFile::m_methods;

enum {
	enExist,
	//enGetHidden,
	enGetModificationTime,
	enGetReadOnly,
	enIsDirectory,
	enIsFile,
	//enSetHidden,
	//enSetModificationTime,
	//enSetReadOnly,
	enSize,
};

void CValueFile::PrepareNames() const
{
	m_methods.AppendConstructor("file", "file(string)");

	m_methods.AppendAttribute("baseName");
	m_methods.AppendAttribute("extension");
	m_methods.AppendAttribute("fullName");
	m_methods.AppendAttribute("name");
	m_methods.AppendAttribute("path");

	m_methods.AppendMethod("exist", "exist()");
	//m_methods.AppendMethod("getHidden", "getHidden()");
	m_methods.AppendMethod("getModificationTime", "getModificationTime()");
	m_methods.AppendMethod("getReadOnly", "getReadOnly()");
	m_methods.AppendMethod("isDirectory", "isDirectory()");
	m_methods.AppendMethod("isFile", "isFile()");
	//m_methods.AppendMethod("setHidden", "setHidden(bool)");
	//m_methods.AppendMethod("setModificationTime", "setModificationTime(date)");
	//m_methods.AppendMethod("setReadOnly", "setReadOnly(bool)");
	m_methods.AppendMethod("size", "size()");
}

#include "appData.h"

CValue CValueFile::Method(methodArg_t &aParams)
{
	wxFileName fileName(m_fileName);

	switch (aParams.GetIndex())
	{
	case enExist:
		return m_fileName.Length() > 0 &&
			(!fileName.IsDir() && fileName.Exists()) || (fileName.IsDir() && fileName.DirExists());
		//case enGetHidden: break;
	case enGetModificationTime: return fileName.GetModificationTime();
	case enGetReadOnly: return m_fileName.Length() > 0 && 
			(!fileName.IsDir() && fileName.IsFileReadable()) || (fileName.IsDir() && fileName.IsDirReadable());
	case enIsDirectory: return fileName.IsDir(); break;
	case enIsFile: return !fileName.IsDir(); break;
		//case enSetHidden: break;
		//case enSetModificationTime: break;
		//case enSetReadOnly: break;
	case enSize: {
		wxULongLong size = fileName.GetSize();
		return number_t(size.GetValue());
	}
	}

	return CValue();
}

void CValueFile::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
}

CValue CValueFile::GetAttribute(attributeArg_t &aParams)
{
	wxFileName fileName(m_fileName);

	switch (aParams.GetIndex())
	{
	case enBaseName: return fileName.GetName();
	case enExtension: return fileName.GetExt();
	case enFullName: return fileName.GetFullPath();
	case enName: return fileName.GetFullName();
	case enPath: return fileName.GetPath();
	}

	return CValue();
}

CValueFile::CValueFile() : CValue(eValueTypes::TYPE_VALUE)
{
}

CValueFile::~CValueFile()
{
}

bool CValueFile::Init(CValue **aParams)
{
	m_fileName = aParams[0]->GetString();
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueFile, "file", TEXT2CLSID("VL_FILE"));
