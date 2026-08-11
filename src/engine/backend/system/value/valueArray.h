#ifndef __VALUE_ARRAY_H__
#define __VALUE_ARRAY_H__

#include "backend/compiler/value.h"

//Array support
// Type-invariant contributor — free function (external linkage) so it can be a
// template non-type arg in the base clause below (the class is incomplete here).
void ibValueArray_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueArray : public ibValueStaticMembers<&ibValueArray_BindNames> {
	public:
private:
	std::vector <ibValue> m_listValue;
private:

	enum Func {
		enAdd = 0,
		enInsert,
		enCount,
		enFind,
		enClear,
		enGet,
		enSet,
		enRemove,
		enContains,
		enSort,
		enSortByKeys,
		// Aggregations (no-arg, numeric assumption — operate on each
		// element via ibValue arithmetic / comparison operators). Lambda-
		// selector variants deferred until closure capture lands; for
		// non-numeric arrays the user can pre-project via a LINQ block
		// (`from r in src select r.Amount`) then call .Sum() on the result.
		enSum,
		enMin,
		enMax,
		enAverage
	};

	// NOT inline: the definition lives in valueArray.cpp, and an inline function must be
	// defined in every TU that uses it. This one is called from Insert() right in this
	// header, so the promise was one no TU could keep — it only ever linked because the
	// callers happened to sit in the same TU as the definition.
	void CheckIndex(unsigned int index) const;

public:

	ibValueArray() :
		ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {
	}

	ibValueArray(const std::vector <ibValue>& arr) :
		ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_listValue(arr) {
	}

	virtual ~ibValueArray() {
		Clear();
	}

	virtual bool Init();
	virtual bool Init(ibValue** paParams, const long lSizeArray);

	//check is empty
	virtual bool IsEmpty() const {
		return m_listValue.empty();
	}

public:

	// DoGetPMethods (protected) + Shared<&ibValueArray_BindNames> come from the base.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //method call

	// LINQ virtual-dispatch override. Intercepts hot operators that
	// have O(1) direct-access shortcuts on the underlying vector
	// (Count / ToArray / ElementAt / Contains); everything else
	// falls through to the default base impl (drain via CreateIterator
	// + state-class wrapping). See `project_value_dispatch_via_virtual.md`.
	virtual void DispatchLinqMethod(ibLinqMethod method, ibValue& ret,
	                                ibValue** args, long n) override;

protected:

	// Packing — CONTENTS only (the header is the base's). Each element becomes a
	// child node and is asked the same question, so the walk continues itself.
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

public:

	// Append to the end.
	void Add(const ibValue& varValue) {
		m_listValue.push_back(varValue);
	}

	// Insert BEFORE `index`; index == size appends. Out of range refuses outside
	// the designer and does nothing inside it — never an out-of-bounds insert.
	// Defined in the .cpp so the bounds guard can use appData / the exception.
	void Insert(unsigned int index, const ibValue& varValue);

	unsigned int Count() const {
		return m_listValue.size();
	}

	ibValue Find(const ibValue& varValue) {
		auto it = std::find(m_listValue.begin(), m_listValue.end(), varValue);
		if (it != m_listValue.end())
			return ibValue(static_cast<signed int>(std::distance(m_listValue.begin(), it)));
		return ibValue();
	}

	// Boolean Contains — avoids the Find/index-0 vs empty ambiguity
	// when callers want to know if an element is present without
	// caring about position. LINQ block-syntax `distinct` uses this:
	// `NOT Find(v)` mis-fires for index 0 because IsEmpty(NUMBER 0)
	// is true (treats 0 as empty); `NOT Contains(v)` works correctly
	// because IsEmpty for booleans is unambiguous.
	bool Contains(const ibValue& varValue) const {
		return std::find(m_listValue.begin(), m_listValue.end(), varValue)
			!= m_listValue.end();
	}

	// Sort in place by ibValue::operator< (CompareValueLS). Ascending
	// by default; pass true for descending.
	void Sort(bool descending = false) {
		if (descending)
			std::sort(m_listValue.begin(), m_listValue.end(),
				[](const ibValue& a, const ibValue& b) { return b < a; });
		else
			std::sort(m_listValue.begin(), m_listValue.end());
	}

	// Sort in place using a parallel keys array. The keys array must
	// have the same length as this; element [i] in this corresponds
	// to key [i] in the keys array. After sort, both arrays are
	// reordered consistently (this gets the sorted values; keys
	// array is left untouched — caller's view). Used by LINQ
	// block-syntax `orderby` where the key extracted per-row is
	// pushed to a parallel array alongside the projected value.
	void SortByKeys(const ibValueArray& keys, bool descending = false) {
		const size_t n = m_listValue.size();
		if (keys.m_listValue.size() != n) {
			// The keys array is built lock-step with this one (LINQ `orderby`), so a
			// length mismatch is an emitter bug, not runtime data. Catch it in Debug;
			// stay graceful in Release rather than sort against a short key array.
			wxFAIL_MSG(wxT("SortByKeys: keys length does not match the array"));
			return;
		}
		std::vector<size_t> idx(n);
		for (size_t i = 0; i < n; ++i) idx[i] = i;
		if (descending)
			std::sort(idx.begin(), idx.end(),
				[&keys](size_t a, size_t b) { return keys.m_listValue[b] < keys.m_listValue[a]; });
		else
			std::sort(idx.begin(), idx.end(),
				[&keys](size_t a, size_t b) { return keys.m_listValue[a] < keys.m_listValue[b]; });
		std::vector<ibValue> sorted;
		sorted.reserve(n);
		for (size_t i : idx) sorted.push_back(std::move(m_listValue[i]));
		m_listValue = std::move(sorted);
	}

	// Erase the element AT `index`. Out of range refuses outside the designer and
	// does nothing inside it (never an out-of-bounds erase). Defined in the .cpp.
	void Remove(unsigned int index);

	void Clear() {
		m_listValue.clear();
	}

	// Sum — accumulate via ibValue::operator+ (works numerically for
	// number/date, concatenates for string). Empty array returns the
	// default ibValue (TYPE_EMPTY).
	ibValue Sum() const {
		if (m_listValue.empty()) return ibValue();
		ibValue acc = m_listValue.front();
		for (size_t i = 1; i < m_listValue.size(); ++i)
			acc = acc + m_listValue[i];
		return acc;
	}

	// Min / Max via ibValue::operator< (CompareValueLS — the same
	// comparator Sort uses, so ordering matches). Empty array → TYPE_EMPTY.
	ibValue Min() const {
		if (m_listValue.empty()) return ibValue();
		const ibValue* best = &m_listValue.front();
		for (size_t i = 1; i < m_listValue.size(); ++i)
			if (m_listValue[i] < *best) best = &m_listValue[i];
		return *best;
	}

	ibValue Max() const {
		if (m_listValue.empty()) return ibValue();
		const ibValue* best = &m_listValue.front();
		for (size_t i = 1; i < m_listValue.size(); ++i)
			if (*best < m_listValue[i]) best = &m_listValue[i];
		return *best;
	}

	// Average = Sum / Count. Numeric only (extracts via GetNumber).
	// Empty array → TYPE_EMPTY (avoids divide-by-zero). No display
	// trim — result has whatever precision `ibNumber::operator/`
	// produces (kExtra=30 fractional digits for repeating decimals).
	// User who wants a clamped display passes the value through
	// `ToString(format)` or rounds explicitly via the `.Round(n)`
	// method. Consistent with manual `Sum() / Count()` behaviour.
	ibValue Average() const {
		if (m_listValue.empty()) return ibValue();
		const ibNumber count(static_cast<int64_t>(m_listValue.size()));
		return ibValue(Sum().GetNumber() / count);
	}

	// Selector-lambda aggregation variants — project each element
	// through λ(x) before accumulating. Lambda invocation goes
	// through the host-API InvokeLambdaWithArg helper (procUnit.h);
	// non-callable selector throws via that path. Defined in .cpp
	// to keep the header free of procUnit.h.
	ibValue SumWithSelector(ibValue& selector) const;
	ibValue MinWithSelector(ibValue& selector) const;
	ibValue MaxWithSelector(ibValue& selector) const;
	ibValue AverageWithSelector(ibValue& selector) const;

	//array support
	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	//Working with iterators
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override {
		class ArrayIteratorState : public ibValueIteratorState {
		public:
			explicit ArrayIteratorState(const std::vector<ibValue>& list) : m_list(list) {}
			bool MoveNext(ibValue& current) override {
				if (m_started) ++m_pos; else m_started = true;
				if (m_pos >= m_list.size()) return false;
				current = m_list[m_pos];
				return true;
			}
			void Reset() override { m_pos = 0; m_started = false; }
		private:
			const std::vector<ibValue>& m_list;
			size_t m_pos = 0;
			bool m_started = false;
		};
		return std::make_shared<ArrayIteratorState>(m_listValue);
	}
};

#endif