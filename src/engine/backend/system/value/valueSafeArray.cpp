////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : COM safe array
////////////////////////////////////////////////////////////////////////////

#include "valueSafeArray.h"
#include "valueArray.h"
#include "valueCOM.h"       // FromVariant / FromValue — an element crosses as any COM value does
#include "backend/backend_exception.h"

#ifdef __IBMSW__

//*********************************************************************************************************************
//*                                                      Elements                                                     *
//*********************************************************************************************************************

// An array goes as one SAFEARRAY of at most this many dimensions — Visual Basic's own limit; rows deeper than
// that go inside it as arrays of their own.
static constexpr size_t s_comArrayDimsBound = 60;

// The element types by the names a script gives them — automation's own.
static const struct {
	const ibChar* m_name;
	VARTYPE m_type;
} s_elementTypes[] = {
	{ ibT("VT_I1"), VT_I1 },
	{ ibT("VT_I2"), VT_I2 },
	{ ibT("VT_I4"), VT_I4 },
	{ ibT("VT_I8"), VT_I8 },
	{ ibT("VT_UI1"), VT_UI1 },
	{ ibT("VT_UI2"), VT_UI2 },
	{ ibT("VT_UI4"), VT_UI4 },
	{ ibT("VT_UI8"), VT_UI8 },
	{ ibT("VT_INT"), VT_INT },
	{ ibT("VT_UINT"), VT_UINT },
	{ ibT("VT_R4"), VT_R4 },
	{ ibT("VT_R8"), VT_R8 },
	{ ibT("VT_CY"), VT_CY },
	{ ibT("VT_DECIMAL"), VT_DECIMAL },
	{ ibT("VT_DATE"), VT_DATE },
	{ ibT("VT_BSTR"), VT_BSTR },
	{ ibT("VT_BOOL"), VT_BOOL },
	{ ibT("VT_ERROR"), VT_ERROR },
	{ ibT("VT_VARIANT"), VT_VARIANT },
	{ ibT("VT_DISPATCH"), VT_DISPATCH },
	{ ibT("VT_UNKNOWN"), VT_UNKNOWN },
};

static VARTYPE ElementType(const ibString& name)
{
	for (const auto& entry : s_elementTypes) {
		if (name.IsSameAs(entry.m_name, false))
			return entry.m_type;
	}
	ibBackendCoreException::Error(_("ComSafeArray: \"%s\" is not an element type"), name);
}

static ibString ElementTypeName(const VARTYPE type)
{
	for (const auto& entry : s_elementTypes) {
		if (entry.m_type == type)
			return ibString(entry.m_name);
	}
	return ibString::Format(ibT("VARTYPE %d"), static_cast<int>(type));
}

// The bounds of `dimension` — counted from 0, the left-most first, as a script counts them.
static void GetBounds(const VARIANT& array, const long dimension, LONG& lower, LONG& upper)
{
	if (dimension < 0 || dimension >= static_cast<long>(::SafeArrayGetDim(array.parray)))
		ibBackendCoreException::Error(_("ComSafeArray: the array has no dimension %d"), static_cast<int>(dimension));
	HRESULT hr = ::SafeArrayGetLBound(array.parray, static_cast<UINT>(dimension) + 1, &lower);
	if (SUCCEEDED(hr))
		hr = ::SafeArrayGetUBound(array.parray, static_cast<UINT>(dimension) + 1, &upper);
	if (FAILED(hr))
		ibBackendCoreException::Error(_("ComSafeArray: %s"), ibSysErrorMsgStr(hr));
}

// A size a script gives: a whole number, 0 or more.
static ULONG GetSize(const ibValue& size)
{
	const ibNumber number = size.GetNumber();
	unsigned long count = 0;
	if (size.GetType() != ibValueTypes::TYPE_NUMBER || number != number.Round() || number < ibNumber(0) || number.ToInt(count) != 0)
		ibBackendCoreException::Error(_("ComSafeArray: a size is a whole number, 0 or more"));
	return count;
}

// The sizes a constructor was given: numbers one by one, or one Array of them.
static std::vector<SAFEARRAYBOUND> GetSizes(ibValue** paParams, const long count)
{
	std::vector<SAFEARRAYBOUND> bounds;
	ibValueArray* sizes = nullptr;
	if (count == 1 && paParams[0]->ConvertToValue(sizes)) {
		for (const ibValue& size : sizes->Values())
			bounds.push_back({ GetSize(size), 0 });
		return bounds;
	}
	for (long i = 0; i < count; i++)
		bounds.push_back({ GetSize(*paParams[i]), 0 });
	return bounds;
}

// The indices a call gave, one for each dimension — the array's own, from its lower bounds.
static std::vector<LONG> GetIndices(const VARIANT& array, ibValue** paParams, const long count)
{
	const UINT dims = ::SafeArrayGetDim(array.parray);
	if (count != static_cast<long>(dims))
		ibBackendCoreException::Error(_("ComSafeArray: the array has %d dimensions, and %d indices were given"), static_cast<int>(dims), static_cast<int>(count));
	std::vector<LONG> indices;
	for (long i = 0; i < count; i++)
		indices.push_back(static_cast<LONG>(paParams[i]->GetInteger()));
	return indices;
}

// One element, whatever the array holds, as the value it is. The element's storage is the VARIANT's union (8 bytes
// in) for every kind but DECIMAL, which takes the whole VARIANT. SafeArrayGetElement takes the indices left-most
// first — measured on oleaut32; its documentation says the other way round.
static ibValue ReadValue(const VARIANT& array, LONG* indices)
{
	const VARTYPE elementType = array.vt & VT_TYPEMASK;
	if (elementType == VT_RECORD)
		ibBackendCoreException::Error(_("COM object \"%s\" returned an array of structures, which the language does not have"), ibString(ibT("ComSafeArray")));

	VARIANT element;
	::VariantInit(&element);
	HRESULT hr = S_OK;
	if (elementType == VT_VARIANT) {
		hr = ::SafeArrayGetElement(array.parray, indices, &element);
	}
	else if (elementType == VT_DECIMAL) {
		DECIMAL dec = {};
		hr = ::SafeArrayGetElement(array.parray, indices, &dec);
		element.decVal = dec;
		element.vt = VT_DECIMAL;      // after the copy: a DECIMAL's first word is where vt lives
	}
	else {
		hr = ::SafeArrayGetElement(array.parray, indices, &element.llVal);
		element.vt = elementType;
	}
	if (hr == DISP_E_BADINDEX)
		ibBackendCoreException::Error(_("Index goes beyond array"));
	if (FAILED(hr))
		ibBackendCoreException::Error(_("ComSafeArray: %s"), ibSysErrorMsgStr(hr));

	ibValue value;
	try {
		ibValueCOM::FromVariant(element, value, ibString(ibT("ComSafeArray")));
	}
	catch (...) {
		::VariantClear(&element);
		throw;
	}
	::VariantClear(&element);
	return value;
}

// One value into the array as its element type: another kind is changed to it the way automation changes one, in
// the invariant locale, and refused when it cannot be.
static void PutValue(VARIANT& array, LONG* indices, const ibValue& value)
{
	const VARTYPE elementType = array.vt & VT_TYPEMASK;
	VARIANT element;
	ibValueCOM::FromValue(value, element);
	HRESULT hr = S_OK;
	if (elementType != VT_VARIANT && element.vt != elementType)
		hr = ::VariantChangeTypeEx(&element, &element, LOCALE_INVARIANT, 0, elementType);
	if (SUCCEEDED(hr)) {
		// SafeArrayPutElement copies what it is given: a VARIANT or a DECIMAL whole, a string or an interface by the
		// pointer itself, anything else from the union.
		void* data = &element.llVal;
		if (elementType == VT_VARIANT)
			data = &element;
		else if (elementType == VT_DECIMAL)
			data = &element.decVal;
		else if (elementType == VT_BSTR)
			data = element.bstrVal;
		else if (elementType == VT_DISPATCH || elementType == VT_UNKNOWN)
			data = element.punkVal;
		hr = ::SafeArrayPutElement(array.parray, indices, data);
	}
	::VariantClear(&element);
	if (hr == DISP_E_BADINDEX)
		ibBackendCoreException::Error(_("Index goes beyond array"));
	if (FAILED(hr))
		ibBackendCoreException::Error(_("ComSafeArray: a value of type \"%s\" cannot be stored as %s"), value.GetClassName(), ElementTypeName(elementType));
}

// Dimension `dimension` (0 = the left-most, the outer array) from `values`: an array for each item of the others,
// the elements at the right-most.
static void PutValues(VARIANT& array, const ibValueArray* values, const size_t dimension, std::vector<LONG>& indices)
{
	LONG lower = 0, upper = -1;
	GetBounds(array, static_cast<long>(dimension), lower, upper);
	if (static_cast<LONG>(values->Count()) != upper - lower + 1)
		ibBackendCoreException::Error(_("ComSafeArray: the array does not have the sizes given"));

	LONG i = lower;
	for (const ibValue& item : values->Values()) {
		indices[dimension] = i++;
		if (dimension + 1 < indices.size()) {
			ibValueArray* row = nullptr;
			if (!item.ConvertToValue(row))
				ibBackendCoreException::Error(_("ComSafeArray: the array does not have the sizes given"));
			PutValues(array, row, dimension + 1, indices);
			continue;
		}
		PutValue(array, indices.data(), item);
	}
}

#endif

//*********************************************************************************************************************
//*                                                      Safe array                                                   *
//*********************************************************************************************************************

// Order MUST match ibValueSafeArray::Func - the method number is the index into this table.
void ibValueSafeArray_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, ibT("ComSafeArray(source : ComSafeArray)"));
	helper.AppendConstructor(2, ibT("ComSafeArray(elementType : string, size1 : number, ..., sizeN : number)"));
	helper.AppendConstructor(2, ibT("ComSafeArray(elementType : string, sizes : Array)"));
	helper.AppendConstructor(2, ibT("ComSafeArray(values : Array, elementType : string)"));
	helper.AppendConstructor(3, ibT("ComSafeArray(values : Array, elementType : string, size1 : number, ..., sizeN : number)"));
	helper.AppendConstructor(3, ibT("ComSafeArray(values : Array, elementType : string, sizes : Array)"));

	helper.AppendFunc(ibT("GetDimensions"), ibT("GetDimensions()"));
	helper.AppendFunc(ibT("GetLength"), 1, ibT("GetLength(dimension : number)"));
	helper.AppendFunc(ibT("GetLowerBound"), 1, ibT("GetLowerBound(dimension : number)"));
	helper.AppendFunc(ibT("GetUpperBound"), 1, ibT("GetUpperBound(dimension : number)"));
	helper.AppendFunc(ibT("GetType"), ibT("GetType()"));
	helper.AppendFunc(ibT("GetValue"), wxNOT_FOUND, ibT("GetValue(index1 : number, ..., indexN : number)"));
	helper.AppendProc(ibT("SetValue"), wxNOT_FOUND, ibT("SetValue(index1 : number, ..., indexN : number, value : any)"));
	helper.AppendFunc(ibT("IsResizable"), ibT("IsResizable()"));
	helper.AppendProc(ibT("Resize"), 1, ibT("Resize(size : number)"));
	helper.AppendFunc(ibT("Unload"), ibT("Unload()"));
}

ibValueSafeArray::ibValueSafeArray() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

#ifdef __IBMSW__
ibValueSafeArray::ibValueSafeArray(const VARIANT& oleVariant) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
	const HRESULT hr = ::VariantCopy(&m_array, &oleVariant);
	if (FAILED(hr))
		ibBackendCoreException::Error(_("ComSafeArray: %s"), ibSysErrorMsgStr(hr));
}
#endif

ibValueSafeArray::~ibValueSafeArray()
{
#ifdef __IBMSW__
	::VariantClear(&m_array);
#endif
}

bool ibValueSafeArray::Init(ibValue** paParams, const long lSizeArray)
{
#ifdef __IBMSW__
	if (lSizeArray < 1)
		return false;

	// FROM ANOTHER: a copy of it.
	ibValueSafeArray* source = nullptr;
	if (paParams[0]->ConvertToValue(source)) {
		if (lSizeArray > 1)
			return false;
		source->CopyTo(m_array);
		return true;
	}

	// FROM AN ARRAY: its values as the element type, in the sizes given or in the array's own shape.
	ibValueArray* values = nullptr;
	if (paParams[0]->ConvertToValue(values)) {
		if (lSizeArray < 2)
			return false;
		m_array = FromValueArray(values, ElementType(paParams[1]->GetString()), GetSizes(paParams + 2, lSizeArray - 2));
		return true;
	}

	// BY THE ELEMENT TYPE: as many elements as the sizes say, each the type's empty one — zero, "", Undefined.
	const VARTYPE elementType = ElementType(paParams[0]->GetString());
	std::vector<SAFEARRAYBOUND> bounds = GetSizes(paParams + 1, lSizeArray - 1);
	if (bounds.empty())
		return false;
	m_array.parray = ::SafeArrayCreate(elementType, static_cast<UINT>(bounds.size()), bounds.data());
	if (m_array.parray == nullptr)
		ibBackendCoreException::Error(_("ComSafeArray: %s"), ibSysErrorMsgStr(E_OUTOFMEMORY));
	m_array.vt = VT_ARRAY | elementType;
	return true;
#else
	// The type is here so that a module which makes one compiles on every platform; the array is not.
	ibBackendCoreException::Error(_("COM objects are available on Windows only"));
#endif
}

bool ibValueSafeArray::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
#ifdef __IBMSW__
	if (m_array.vt == VT_EMPTY)
		return false;

	switch (lMethodNum) {
	case enSetValue: {
		if (lSizeArray < 2)
			return false;
		std::vector<LONG> indices = GetIndices(m_array, paParams, lSizeArray - 1);
		PutValue(m_array, indices.data(), *paParams[lSizeArray - 1]);
		return true;
	}
	case enResize: {
		// The right-most dimension, the only one SafeArrayRedim changes: the elements already there stay where they are.
		if (lSizeArray < 1)
			return false;
		LONG lower = 0, upper = -1;
		GetBounds(m_array, static_cast<long>(::SafeArrayGetDim(m_array.parray)) - 1, lower, upper);
		SAFEARRAYBOUND bound = { GetSize(*paParams[0]), lower };
		const HRESULT hr = ::SafeArrayRedim(m_array.parray, &bound);
		if (FAILED(hr))
			ibBackendCoreException::Error(_("ComSafeArray: %s"), ibSysErrorMsgStr(hr));
		return true;
	}
	}
#endif
	return false;
}

bool ibValueSafeArray::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
#ifdef __IBMSW__
	if (m_array.vt == VT_EMPTY)
		return false;

	switch (lMethodNum) {
	case enGetDimensions:
		pvarRetValue = ibNumber(static_cast<long long>(::SafeArrayGetDim(m_array.parray)));
		return true;
	case enGetLength:
	case enGetLowerBound:
	case enGetUpperBound: {
		if (lSizeArray < 1)
			return false;
		LONG lower = 0, upper = -1;
		GetBounds(m_array, paParams[0]->GetInteger(), lower, upper);
		const LONG answer = lMethodNum == enGetLength ? upper - lower + 1 : lMethodNum == enGetLowerBound ? lower : upper;
		pvarRetValue = ibNumber(static_cast<long long>(answer));
		return true;
	}
	case enGetType:
		pvarRetValue = ElementTypeName(m_array.vt & VT_TYPEMASK);
		return true;
	case enGetValue: {
		std::vector<LONG> indices = GetIndices(m_array, paParams, lSizeArray);
		pvarRetValue = ReadValue(m_array, indices.data());
		return true;
	}
	case enIsResizable:
		pvarRetValue = ibValue((m_array.parray->fFeatures & FADF_FIXEDSIZE) == 0);
		return true;
	case enUnload:
		pvarRetValue = Unload();
		return true;
	}
#endif
	return false;
}

ibValue ibValueSafeArray::Unload() const
{
	const ibValuePtr<ibValueArray> arr(new ibValueArray());   // born held
#ifdef __IBMSW__
	const UINT dims = m_array.vt != VT_EMPTY ? ::SafeArrayGetDim(m_array.parray) : 0;
	if (dims > 0) {
		std::vector<LONG> indices(dims, 0);
		Unload(arr, 0, indices);
	}
#endif
	return arr;
}

#ifdef __IBMSW__
void ibValueSafeArray::Unload(ibValueArray* arr, const size_t dimension, std::vector<LONG>& indices) const
{
	LONG lower = 0, upper = -1;
	GetBounds(m_array, static_cast<long>(dimension), lower, upper);
	for (LONG i = lower; i <= upper; i++) {
		indices[dimension] = i;
		if (dimension + 1 < indices.size()) {
			const ibValuePtr<ibValueArray> inner(new ibValueArray());
			Unload(inner, dimension + 1, indices);
			arr->Add(inner);
			continue;
		}
		arr->Add(ReadValue(m_array, indices.data()));
	}
}

void ibValueSafeArray::CopyTo(VARIANT& oleVariant) const
{
	::VariantInit(&oleVariant);
	const HRESULT hr = ::VariantCopy(&oleVariant, &m_array);
	if (FAILED(hr))
		ibBackendCoreException::Error(_("ComSafeArray: %s"), ibSysErrorMsgStr(hr));
}

VARIANT ibValueSafeArray::FromValueArray(const ibValueArray* values, const VARTYPE elementType, std::vector<SAFEARRAYBOUND> bounds)
{
	// The dimensions, when none are given: one more for as long as every item of the level is an array, all of one
	// length.
	if (bounds.empty()) {
		std::vector<const ibValueArray*> level = { values };
		for (;;) {
			bounds.push_back({ static_cast<ULONG>(level.front()->Count()), 0 });
			std::vector<const ibValueArray*> below;
			bool rectangular = bounds.size() < s_comArrayDimsBound;
			for (size_t r = 0; rectangular && r < level.size(); r++) {
				for (const ibValue& item : level[r]->Values()) {
					ibValueArray* inner = nullptr;
					rectangular = item.ConvertToValue(inner) && (below.empty() || inner->Count() == below.front()->Count());
					if (!rectangular)
						break;
					below.push_back(inner);
				}
			}
			if (!rectangular || below.empty())
				break;
			level = std::move(below);
		}
	}

	VARIANT array;
	::VariantInit(&array);
	array.parray = ::SafeArrayCreate(elementType, static_cast<UINT>(bounds.size()), bounds.data());
	if (array.parray == nullptr)
		ibBackendCoreException::Error(_("ComSafeArray: %s"), ibSysErrorMsgStr(E_OUTOFMEMORY));
	array.vt = VT_ARRAY | elementType;
	std::vector<LONG> indices(bounds.size(), 0);
	try {
		PutValues(array, values, 0, indices);
	}
	catch (...) {
		::VariantClear(&array);
		throw;
	}
	return array;
}
#endif

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueSafeArray, "ComSafeArray", value_to_clsid("VL_SFAR"));
