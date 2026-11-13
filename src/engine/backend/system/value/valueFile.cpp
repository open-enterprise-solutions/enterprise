#include "valueFile.h"

#include "core/fileSystem.h"   // ibPathOf, ibFileModificationTime

#include <system_error>

namespace {

// Readable by its owner, as the system keeps the bits (wxIsReadable asked access()); false when nothing is there.
bool IsReadable(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status = std::filesystem::status(path, error);
	return !error && std::filesystem::exists(status)
		&& (status.permissions() & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
}

} // namespace

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
	const std::filesystem::path path = ibPathOf(m_fileName);
	// A name with nothing after its last separator is a folder's, as wxFileName read it - whether one is there or not.
	const bool folder = !path.has_filename();
	std::error_code error;

	switch (lMethodNum)
	{
	case enExist:
		// Parenthesised as intended: a name is required, AND then either the file or the
		// directory must exist. Without the brackets && bound tighter, so the whole thing
		// read as "(named AND file exists) OR directory exists" — the second arm answering
		// true for an EMPTY name.
		pvarRetValue = m_fileName.Length() > 0 &&
			(folder ? std::filesystem::is_directory(path, error) : std::filesystem::exists(path, error));
		return true;
		//case enGetHidden: break;
	case enGetModificationTime:
		// The file system's instant, by its local parts; none (no such file) - the empty date.
		pvarRetValue = ibFileModificationTime(m_fileName);
		return true;
	case enGetReadOnly: pvarRetValue = m_fileName.Length() > 0 && IsReadable(path);
		return true;
	case enIsDirectory: pvarRetValue = folder;
		return true;
	case enIsFile: pvarRetValue = !folder;
		return true;
		//case enSetHidden: break;
		//case enSetModificationTime: break;
		//case enSetReadOnly: break;
	case enSize:
		// No file to ask - the largest count, as wxInvalidSize was.
		pvarRetValue = ibNumber(std::filesystem::file_size(path, error));
		return true;
	}

	return false;
}

bool ibValueFile::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueFile::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	std::filesystem::path path = ibPathOf(m_fileName);

	switch (lPropNum)
	{
	case enBaseName:
		pvarPropVal = ibString(path.stem().wstring());
		return true;
	case enExtension: {
		// without its dot, as wxFileName::GetExt gave it
		const std::wstring extension = path.extension().wstring();
		pvarPropVal = ibString(extension.empty() ? extension : extension.substr(1));
		return true;
	}
	case enFullName:
		pvarPropVal = ibString(path.make_preferred().wstring());   // the separators this system writes, as wxFileName did
		return true;
	case enName:
		pvarPropVal = ibString(path.filename().wstring());
		return true;
	case enPath:
		pvarPropVal = ibString(path.parent_path().wstring());
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
