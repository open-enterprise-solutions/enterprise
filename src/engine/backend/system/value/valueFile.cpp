#include "valueFile.h"

#include <wx/filename.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


void ibValueFile_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("File(path : string)"));

	helper.AppendProp(wxT("BaseName"));
	helper.AppendProp(wxT("Extension"));
	helper.AppendProp(wxT("FullName"));
	helper.AppendProp(wxT("Name"));
	helper.AppendProp(wxT("Path"));

	helper.AppendFunc(wxT("Exist"), wxT("Exist()"));
	//helper.AppendMethod(wxT("GetHidden"), wxT("GetHidden()"));
	helper.AppendFunc(wxT("GetModificationTime"), wxT("GetModificationTime()"));
	helper.AppendFunc(wxT("GetReadOnly"), wxT("GetReadOnly()"));
	helper.AppendFunc(wxT("IsDirectory"), wxT("IsDirectory()"));
	helper.AppendFunc(wxT("IsFile"), wxT("IsFile()"));
	//helper.AppendMethod("SetHidden", "SetHidden(bool)");
	//helper.AppendMethod("SetModificationTime", "SetModificationTime(date)");
	//helper.AppendMethod("SetReadOnly", "SetReadOnly(bool)");
	helper.AppendFunc(wxT("Size"), wxT("Size()"));
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

ibValueFile::ibValueFile() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
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
