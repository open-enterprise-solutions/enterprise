#include "valueFile.h"

#include <wx/filename.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


void ibValueFile_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, ibT("File(path : string)"));

	helper.AppendProp(ibT("BaseName"));
	helper.AppendProp(ibT("Extension"));
	helper.AppendProp(ibT("FullName"));
	helper.AppendProp(ibT("Name"));
	helper.AppendProp(ibT("Path"));

	helper.AppendFunc(ibT("Exist"), ibT("Exist()"));
	//helper.AppendMethod(wxT("GetHidden"), wxT("GetHidden()"));
	helper.AppendFunc(ibT("GetModificationTime"), ibT("GetModificationTime()"));
	helper.AppendFunc(ibT("GetReadOnly"), ibT("GetReadOnly()"));
	helper.AppendFunc(ibT("IsDirectory"), ibT("IsDirectory()"));
	helper.AppendFunc(ibT("IsFile"), ibT("IsFile()"));
	//helper.AppendMethod("SetHidden", "SetHidden(bool)");
	//helper.AppendMethod("SetModificationTime", "SetModificationTime(date)");
	//helper.AppendMethod("SetReadOnly", "SetReadOnly(bool)");
	helper.AppendFunc(ibT("Size"), ibT("Size()"));
}


#include "backend/appData.h"

bool ibValueFile::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	wxFileName strFileName(m_fileName);

	switch (lMethodNum)
	{
	case enExist:
		// Parenthesised as intended: a name is required, AND then either the file or the
		// directory must exist. Without the brackets && bound tighter, so the whole thing
		// read as "(named AND file exists) OR directory exists" — the second arm answering
		// true for an EMPTY name.
		pvarRetValue = m_fileName.Length() > 0 &&
			((!strFileName.IsDir() && strFileName.Exists()) || (strFileName.IsDir() && strFileName.DirExists()));
		return true;
		//case enGetHidden: break;
	case enGetModificationTime: {
		// The file system's instant, by its local parts; none (no such file) - the empty date.
		const wxDateTime modified = strFileName.GetModificationTime();
		pvarRetValue = modified.IsValid() ? ibDateTime::OfInstant(modified.GetValue().GetValue()) : ibDateTime();
		return true;
	}
	case enGetReadOnly: pvarRetValue = m_fileName.Length() > 0 &&
		((!strFileName.IsDir() && strFileName.IsFileReadable()) || (strFileName.IsDir() && strFileName.IsDirReadable()));
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
		pvarPropVal = ibString(strFileName.GetName());
		return true;
	case enExtension:
		pvarPropVal = ibString(strFileName.GetExt());
		return true;
	case enFullName:
		pvarPropVal = ibString(strFileName.GetFullPath());
		return true;
	case enName:
		pvarPropVal = ibString(strFileName.GetFullName());
		return true;
	case enPath:
		pvarPropVal = ibString(strFileName.GetPath());
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

VALUE_TYPE_REGISTER(ibValueFile, "File", value_to_clsid("VL_FILE"));
