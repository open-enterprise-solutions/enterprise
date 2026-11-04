#ifndef _VALUE_COLLECTION_H__
#define _VALUE_COLLECTION_H__

//*************************************************************************************************************************************
//*                                                         value collection                                                          *
//*************************************************************************************************************************************

// Included at the end of compiler/value.h, as value_ptr.h and value_cast.h are: it is built on a complete ibValue.

// An index past the elements - "Index goes beyond array". Out of line (value_collection.cpp), as ThrowErrorTypeOperation
// is for the cast: the collection below raises it without this header knowing the exception.
extern BACKEND_API void ThrowErrorIndexBeyond();

// ⭐ A VECTOR OF ELEMENTS A SCRIPT COUNTS, GETS AND WALKS - and nothing else: every other method is the heir's
// (Max, 2026-11-04: "the array adds its own methods, the users list its own"). The array and the structure
// are two cases of it, and so is every list a kind hands out of itself - its jobs, its managers, its
// metadata. What is the same for all of them is here: the vector, Count, Get, [i], For Each. What differs
// is not: a structure finds by its key, a users list by its own fields, and an array lets anything be added
// where a kind's list takes only its own elements.
//
// An heir says what its ELEMENT is (ElementT) and what that element is to a script (ElementValue: the array -
// the value itself, the structure - a key with its value). Get and [i] read by index; a keyed heir reads by
// its key instead (GetAt). Its own methods come after the base's in the table: its binder calls
// FillCollection first and numbers its own from enCollectionFuncs, so the table's order and the numbers agree.
template<typename ElementT, ibValue::ibMemberTable::ibNameBinder Binder>
class ibValueCollection : public ibValueStaticMembers<Binder> {
public:

	ibValueCollection(ibValueTypes type = ibValueTypes::TYPE_VALUE, bool readOnly = false) :
		ibValueStaticMembers<Binder>(type, readOnly) {
	}

	ibValueCollection(const std::vector<ElementT>& values, ibValueTypes type = ibValueTypes::TYPE_VALUE, bool readOnly = false) :
		ibValueStaticMembers<Binder>(type, readOnly), m_listValue(values) {
	}

	virtual bool IsEmpty() const override {
		return m_listValue.empty();
	}

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override {
		switch (lMethodNum) {
		case enCount:
			pvarRetValue = Count();
			return true;
		case enGet:
			return GetAt(*paParams[0], pvarRetValue);
		}
		return false;
	}

	// The index starts from 0.
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override {
		const unsigned int index = varKeyValue.GetUInteger();
		if (index >= m_listValue.size())
			ThrowErrorIndexBeyond();
		pvarValue = ElementValue(m_listValue[index]);
		return true;
	}

	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override {
		// ⭐⭐ THE ITERATOR OWNS THE COLLECTION IT WALKS - a lazy pipeline outlives the expression that
		// made its source, and a collection that only lends a reference dies underneath it.
		//
		// `arr.Where(f)` returns a STATE, not rows: nothing is read until Count / Foreach asks. So
		// the collection has to survive the statement that created it, and the only thing that knows
		// the state still needs it is the state itself. A reference to `m_listValue` said the opposite -
		// "somebody else is keeping this alive" - and for a NAMED array that is true.
		//
		// 🛑 It is false for a TEMPORARY one, which is exactly what the pushed-down road builds:
		// ibValueQueryable::MaterialiseThenRam streams the query into a local array and dispatches
		// the untranslatable op on it. The op returned a lazy state, the local went out of scope, and
		// the walk then read a freed vector - `Data.Catalogs.Goods.Where(f).Count()` answered 0 where
		// 10 rows existed, SILENTLY, and the contract that road is built on ("the RAM floor, always
		// correct") quietly did not hold (measured 2026-09-04).
		//
		// Holding a counted reference costs one increment per iterator and is what "always correct"
		// requires; the vector is still read by reference, so the walk itself is unchanged.
		class CollectionIteratorState : public ibValueIteratorState {
		public:
			explicit CollectionIteratorState(ibValueCollection* owner) : m_owner(owner) {
				m_owner->IncrRef();
			}
			~CollectionIteratorState() override {
				m_owner->DecrRef();
			}
			bool MoveNext(ibValue& current) override {
				if (m_started) ++m_pos; else m_started = true;
				if (m_pos >= m_owner->m_listValue.size()) return false;
				current = m_owner->ElementValue(m_owner->m_listValue[m_pos]);
				return true;
			}
			void Reset() override { m_pos = 0; m_started = false; }
			long Remaining() const override {
				const size_t next = m_started ? m_pos + 1 : m_pos;
				return next < m_owner->m_listValue.size() ? (long)(m_owner->m_listValue.size() - next) : 0;
			}
			// The editor's hint for `For Each x In ...`: what an EMPTY element is to a script. An array's is
			// an empty value, which says nothing - untyped, as it always was; a structure's is a key with
			// its value.
			bool PeekSample(ibValue& current) const override {
				current = m_owner->ElementValue(ElementT());
				return current.GetType() != ibValueTypes::TYPE_EMPTY;
			}
		private:
			ibValueCollection* m_owner;   // kept alive for the walk - see the note above
			size_t m_pos = 0;
			bool m_started = false;
		};
		return std::make_shared<CollectionIteratorState>(this);
	}

	unsigned int Count() const {
		return m_listValue.size();
	}

	// The elements, for a caller in C++ that only reads them.
	const std::vector<ElementT>& Values() const { return m_listValue; }

	// THE BASE'S SCRIPT METHODS, which every heir's binder puts first. `getHelper` - what Get reads by: an
	// index, or a keyed heir's key.
	static void FillCollection(ibValue::ibMemberTable& helper, const ibString& getHelper = ibT("Get(index)")) {
		helper.AppendFunc(ibT("Count"), ibT("Count()"));
		helper.AppendFunc(ibT("Get"), 1, getHelper);
	}

protected:

	enum {
		enCount = 0,
		enGet,
		enCollectionFuncs   // where an heir's own methods start
	};

	// What the element is to a script.
	virtual ibValue ElementValue(const ElementT& element) const = 0;

	std::vector<ElementT> m_listValue;
};

#endif
