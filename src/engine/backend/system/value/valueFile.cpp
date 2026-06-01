#include "valueFile.h"

#include <wx/filename.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


ibValue::ibValueMethodHelper ibValueFile::m_methodHelper;

void ibValueFile::PrepareNames() const
{
	m_methodHelper.AppendConstructor(1, wxT("File(path : string)"));

	m_methodHelper.AppendProp(wxT("BaseName"));
	m_methodHelper.AppendProp(wxT("Extension"));
	m_methodHelper.AppendProp(wxT("FullName"));
	m_methodHelper.AppendProp(wxT("Name"));
	m_methodHelper.AppendProp(wxT("Path"));

	m_methodHelper.AppendFunc(wxT("Exist"), wxT("Exist()"));
	//m_methodHelper.AppendMethod(wxT("GetHidden"), wxT("GetHidden()"));
	m_methodHelper.AppendFunc(wxT("GetModificationTime"), wxT("GetModificationTime()"));
	m_methodHelper.AppendFunc(wxT("GetReadOnly"), wxT("GetReadOnly()"));
	m_methodHelper.AppendFunc(wxT("IsDirectory"), wxT("IsDirectory()"));
	m_methodHelper.AppendFunc(wxT("IsFile"), wxT("IsFile()"));
	//m_methodHelper.AppendMethod("SetHidden", "SetHidden(bool)");
	//m_methodHelper.AppendMethod("SetModificationTime", "SetModificationTime(date)");
	//m_methodHelper.AppendMethod("SetReadOnly", "SetReadOnly(bool)");
	m_methodHelper.AppendFunc(wxT("Size"), wxT("Size()"));
}

#include "backend/appData.h"

bool ibValueFile::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	wxFileName strFileName(m_fileName);

	switch (lMethodNum)
	{
	case enExist:
		pvarRetValue = m_fileName.Length() > 0 &&
			(!strFileName.IsDir() && strFileName.Exists()) || (strFileName.IsDir() && strFileName.DirExists());
		return true;
		//case enGetHidden: break;
	case enGetModificationTime: pvarRetValue = strFileName.GetModificationTime();
		return true;
	case enGetReadOnly: pvarRetValue = m_fileName.Length() > 0 &&
		(!strFileName.IsDir() && strFileName.IsFileReadable()) || (strFileName.IsDir() && strFileName.IsDirReadable());
		return true;
	case enIsDirectory: pvarRetValue = strFileName.IsDir();
		return true;
	case enIsFile: pvarRetValue = !strFileName.IsDir();
		return true;
		//case enSetHidden: break;
		//case enSetModificationTime: break;
		//case enSetReadOnly: break;
	case enSize: {
		const wxULongLong& size = strFileName.GetSize();
		pvarRetValue = ibNumber(size.GetValue());
		return true;
	}
	}

	return false;
}

bool ibValueFile::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueFile::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	wxFileName strFileName(m_fileName);

	switch (lPropNum)
	{
	case enBaseName:
		pvarPropVal = strFileName.GetName();
		return true;
	case enExtension:
		pvarPropVal = strFileName.GetExt();
		return true;
	case enFullName:
		pvarPropVal = strFileName.GetFullPath();
		return true;
	case enName:
		pvarPropVal = strFileName.GetFullName();
		return true;
	case enPath:
		pvarPropVal = strFileName.GetPath();
		return true;
	}

	return false;
}

ibValueFile::ibValueFile() : ibValue(ibValueTypes::TYPE_VALUE)
{
}

ibValueFile::~ibValueFile()
{
}

bool ibValueFile::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	m_fileName = paParams[0]->GetString();
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueFile, "File", string_to_clsid("VL_FILE"));
