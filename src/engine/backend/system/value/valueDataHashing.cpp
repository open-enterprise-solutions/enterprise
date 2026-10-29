////////////////////////////////////////////////////////////////////////////
//	Description : a digest of data, computed by a script
////////////////////////////////////////////////////////////////////////////

#include "valueDataHashing.h"
#include "valueBinaryData.h"   // the bytes a sum is handed over as, and ReadWholeFile

#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"            // ConvertToEnumValue<> is declared in value.h and DEFINED here
#include "backend/system/systemManagerEnum.h"     // HashFunction, handed back by the property

namespace {

// HashFunction -> the core's. Anything that is not a member - a number, a string, a member of another
// enumeration - comes in as no function at all and is refused by the caller.
bool CoreFunctionOf(ibHashFunction function, ibDataHash::Function& made)
{
	switch (function) {
	case ibHashFunction_MD5:    made = ibDataHash::Function::MD5;    return true;
	case ibHashFunction_SHA1:   made = ibDataHash::Function::SHA1;   return true;
	case ibHashFunction_SHA256: made = ibDataHash::Function::SHA256; return true;
	case ibHashFunction_SHA384: made = ibDataHash::Function::SHA384; return true;
	case ibHashFunction_SHA512: made = ibDataHash::Function::SHA512; return true;
	}
	return false;
}

ibDataHash::Function CoreFunction(ibHashFunction function)
{
	ibDataHash::Function made = ibDataHash::Function::SHA256;
	if (!CoreFunctionOf(function, made))
		ibBackendCoreException::Error(_("DataHashing: the hash function is a HashFunction value - HashFunction.SHA256, for one"));
	return made;
}

// The bytes a value stands for: a BinaryData's own, a string's in UTF-8. Anything else is refused - a number or a
// date has no one spelling in bytes, and hashing the spelling it happens to show would sign something else.
void BytesOf(const ibValue& value, const ibChar* who, std::string& bytes)
{
	if (const ibValueBinaryData* binary = dynamic_cast<const ibValueBinaryData*>(value.GetRef())) {
		const ibMemoryBuffer& buffer = binary->GetBuffer();
		bytes.assign(static_cast<const char*>(buffer.GetData()), buffer.GetDataLen());
		return;
	}
	if (value.GetType() == ibValueTypes::TYPE_STRING) {
		bytes = value.GetString().ToUtf8();
		return;
	}
	ibBackendCoreException::Error(_("%s: the data is a string or a BinaryData"), ibString(who));
}

} // namespace

// Order MUST match ibValueDataHashing::Prop and ::Func - the number is the index into each table.
void ibValueDataHashing_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, ibT("DataHashing(function : HashFunction)"));
	helper.AppendConstructor(2, ibT("DataHashing(function : HashFunction, key : string | BinaryData)"));

	helper.AppendProp(ibT("HashFunction"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("HashSum"), true, false, wxNOT_FOUND);

	helper.AppendProc(ibT("Append"), 1, ibT("Append(data : string | BinaryData)"));
	helper.AppendProc(ibT("AppendFile"), 1, ibT("AppendFile(path : string)"));
}

ibValueDataHashing::ibValueDataHashing() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueDataHashing::~ibValueDataHashing() {}

bool ibValueDataHashing::Init()
{
	ibBackendCoreException::Error(_("DataHashing: the hash function is said when the object is built - New DataHashing(HashFunction.SHA256)"));
}

bool ibValueDataHashing::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return Init();

	const ibHashFunction function = paParams[0]->GetType() == ibValueTypes::TYPE_ENUM
		? paParams[0]->ConvertToEnumValue<ibHashFunction>() : ibHashFunction();

	// ⚠ WHETHER A KEY WAS GIVEN IS ASKED OF ITS TYPE, NOT OF ITS EMPTINESS. An empty string is a key - an HMAC
	// with an empty key is a value of its own - and only an argument left out means the plain digest.
	if (lSizeArray > 1 && paParams[1]->GetType() != ibValueTypes::TYPE_EMPTY) {
		std::string key;
		BytesOf(*paParams[1], ibT("DataHashing"), key);
		Start(function, key.data(), key.size());
	}
	else {
		Start(function);
	}
	return true;
}

void ibValueDataHashing::Start(ibHashFunction function)
{
	m_hash = std::make_unique<ibDataHash>(CoreFunction(function));
	m_function = function;
}

void ibValueDataHashing::Start(ibHashFunction function, const void* key, size_t keyLength)
{
	m_hash = std::make_unique<ibDataHash>(CoreFunction(function), key, keyLength);
	m_function = function;
}

void ibValueDataHashing::Append(const ibValue& data)
{
	if (m_hash == nullptr)
		ibBackendCoreException::Error(_("DataHashing: no hash function is set"));
	std::string bytes;
	BytesOf(data, ibT("DataHashing.Append"), bytes);
	m_hash->Append(bytes.data(), bytes.size());
}

void ibValueDataHashing::AppendFile(const ibString& fileName)
{
	if (m_hash == nullptr)
		ibBackendCoreException::Error(_("DataHashing: no hash function is set"));
	ibMemoryBuffer bytes;
	ibValueBinaryData::ReadWholeFile(fileName, ibT("DataHashing"), bytes);
	m_hash->Append(bytes.GetData(), bytes.GetDataLen());
}

ibMemoryBuffer ibValueDataHashing::GetHashSum() const
{
	if (m_hash == nullptr)
		ibBackendCoreException::Error(_("DataHashing: no hash function is set"));
	return m_hash->GetSum();
}

bool ibValueDataHashing::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enHashFunction:
		pvarPropVal = ibValue::CreateEnumObject<ibValueEnumHashFunction>(m_function);
		return true;
	case enHashSum: {
		const ibValuePtr<ibValueBinaryData> sum(new ibValueBinaryData(GetHashSum()));
		pvarPropVal = sum;
		return true;
	}
	}
	return false;
}

bool ibValueDataHashing::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enAppend:
		if (lSizeArray < 1)
			return false;
		Append(*paParams[0]);
		return true;
	case enAppendFile:
		if (lSizeArray < 1)
			return false;
		AppendFile(paParams[0]->GetString());
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueDataHashing, "DataHashing", value_to_clsid("VL_DHSH"));
