#ifndef __VALUE_MAP_H__
#define __VALUE_MAP_H__

#include "backend/compiler/value.h"

#include <vector>
#include <unordered_map>
#include <string>

// A key -> value map (script "Container"; "Structure" is the string-keyed
// variant). Keys are DATA: they live in the store below and are reached by NAME,
// never mirrored into the member table. Two consequences the previous design got
// wrong and this one fixes:
//
//   * Lookup is O(1). The old store was a std::map keyed by ibValue with a
//     comparator that materialised and uppercased BOTH keys on every comparison
//     — O(log n) allocations per access. Here a hash index over a once-folded key
//     answers in one probe.
//   * Building is O(n). The old member table published every key as a script
//     property and rebuilt that O(size) surface on each mutation, so filling an
//     n-key container was O(n^2) (docs/runtime-perf.md §1g). The member table now
//     carries only the fixed METHODS and is built once; the keys never touch it.
class BACKEND_API ibValueContainer : public ibValueDynamicMembers {
	public:
private:
	enum Func  {
		enCount = 0,
		enProperty,
		enClear,
		enDelete,
		enInsert
	};

	// The store. `m_entries` keeps insertion order, which gives every key a
	// stable index for the property protocol (FindProp -> GetPropVal) and a
	// deterministic iteration / serialisation order. `m_index` buckets it.
	// The two are maintained together by every mutating method.
	std::vector<std::pair<ibValue, ibValue>> m_entries;

	// TWO KINDS OF KEY, TWO RULES, and neither renders the value to text:
	//
	//   a STRING key folds case — a script reaches a field by name and does not care how it was
	//   typed (`Структура.Имя` and `структура.имя` are one field). The fold runs once per LOOKUP
	//   while hashing; inside a bucket the comparison decides most candidates on length alone and
	//   folds only the characters that differ.
	//
	//   anything else compares AS A VALUE, through ibValue's own ordering — a reference by its guid,
	//   a number by its magnitude. This is what replaced the old rendered identity, and with it the
	//   rule that `1` and "1" were one key: they are different keys now, as they are everywhere else
	//   in the language.

	// THE INDEX HOLDS POSITIONS, NOT A SECOND COPY OF THE KEY. It used to be
	// keyed by the ibValue itself, so every insert copied the key — and a string
	// key copies its buffer, an allocation per field. The footprint probe reads
	// that as ~1 KB per field of a Structure against 40 bytes of data
	// (docs/runtime-perf.md §9); the key was already in m_entries, one hop away.
	//
	// So: bucket by the key's HASH, map to entry positions, and settle equality
	// against the entry itself — which is exactly what a hash table does on a
	// collision anyway. Multimap because two different keys may share a hash and
	// both must keep their position.
	std::unordered_multimap<size_t, size_t> m_index;

	// Hash and lookup, split because every mutating path wants both and hashing
	// twice was the other half of the old shape's cost.
	static size_t HashOf(const ibValue& key);
	long FindWithHash(const ibValue& key, size_t hash) const;

protected:
	// -1 when absent; the entry index otherwise. The single lookup primitive the
	// key-facing methods share.
	long IndexOf(const ibValue& key) const;

public:
	// THE PAIRS, IN INSERTION ORDER — read-only, for a consumer that takes a whole map at once rather
	// than asking key by key (an accounting posting is written as *(kind -> value)* pairs and poured
	// into the movement's slots). Nothing else about the store is exposed: this is the same order a
	// script sees when it iterates, so a caller cannot observe an arrangement the language does not.
	const std::vector<std::pair<ibValue, ibValue>>& Entries() const { return m_entries; }

protected:

public:

	//Attribute -> String key
	//working with an array as an aggregate object:

	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);
	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& cValue);

	//check is empty
	virtual bool IsEmpty() const {
		return m_entries.empty();
	}

public:

	class BACKEND_API ibValueReturnContainer : public ibValueDynamicMembers {
	public:

		enum Prop {
			enKey,
			enValue
		};

		ibValue m_key;
		ibValue m_value;

	public:

		ibValueReturnContainer() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true) {
			m_members.Bind(this, &ibValueReturnContainer::FillMembers);
		}

		ibValueReturnContainer(const ibValue& key, ibValue& value) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_key(key), m_value(value) {
			m_members.Bind(this, &ibValueReturnContainer::FillMembers);
		}

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		virtual bool SetPropVal(const long lPropNum, const ibValue& cValue) override;        //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	};

public:

	ibValueContainer();
	ibValueContainer(const std::map<ibValue, ibValue>& containerValues);
	ibValueContainer(bool readOnly);

	virtual ~ibValueContainer();

	// KEY access. Resolved straight against the store — the member table carries
	// only methods, so FindProp returns a key's entry index (or -1), and
	// Get/SetPropVal read / write that entry. GetNProps / GetPropName expose the
	// keys to introspection (debugger, inspectors) without maintaining a live
	// surface: they read the store on demand.
	virtual long FindProp(const wxString& strPropName) const override;
	virtual long GetNProps() const override { return (long)m_entries.size(); }
	virtual wxString GetPropName(const long lPropNum) const override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& cValue) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	// A key index is readable and writable; the base checks the member table,
	// which no longer carries the keys, so it must be answered from the store.
	// (A key is never scope-local, so the base's IsPropScoped is already right.)
	virtual bool IsPropReadable(const long lPropNum) const override { return lPropNum >= 0 && lPropNum < (long)m_entries.size(); }
	virtual bool IsPropWritable(const long lPropNum) const override { return lPropNum >= 0 && lPropNum < (long)m_entries.size(); }

	// The FIXED method surface — methods only, no keys. Type-invariant given the
	// read-only flag, bound once in the ctor and never rebuilt on a mutation.
	static void BindContainerNames(ibMemberTable& helper, const ibValue* ctx);
	// DoGetPMethods (protected) + the by-value helper come from ibValueDynamicMembers.

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //method call

	// extended methods:
	virtual void Insert(const ibValue& varKeyValue, const ibValue& cValue);
	virtual void Delete(const ibValue& varKeyValue);
	virtual bool Property(const ibValue& varKeyValue, ibValue& cValueFound);
	unsigned int Count() const { return (unsigned int)m_entries.size(); }
	void Clear() { m_entries.clear(); m_index.clear(); }

	// COMPARES BY ITS ENTRIES, for the same reason the array does — the base
	// compares object kinds by class name, so any two containers (and any two
	// structures) came out EQUAL. A structure is the natural composite key of a
	// group-by, and it was collapsing every row into one group. Entry order is
	// insertion order, which the store already keeps deterministic.
	virtual int  CompareValueLS(const ibValue& cParam) const override;
	virtual bool CompareValueEQ(const ibValue& cParam) const override;
	// Hashes by the same entries the order walks — see ibValue::GetValueHash.
	virtual size_t GetValueHash() const override;
private:
	// The other side as a container, or nullptr. Cast choice is measured — see
	// the note in valueArray.cpp.
	const ibValueContainer* AsContainer(const ibValue& cParam) const;
public:

	// iterator support:
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

protected:

	// Packing — CONTENTS only: a pair is two child nodes (valueMap.cpp). A
	// structure inherits this unchanged; it differs in what it accepts as a KEY,
	// not in how it is written, and the header already says which it was.
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;
};

// structure
class BACKEND_API ibValueStructure : public ibValueContainer {
	public:

	ibValueStructure() : ibValueContainer(false) {}
	ibValueStructure(const std::map<wxString, ibValue>& structureValues) : ibValueContainer(true) {
		for (auto& strBVal : structureValues) ibValueContainer::SetAt(strBVal.first, strBVal.second);
	}

	ibValueStructure(bool readOnly) : ibValueContainer(readOnly) {}

	// `New Structure("Field1, Field2, ...", value1, value2, ...)` —
	// named-column ctor: first arg is comma-separated field-name list,
	// subsequent args are corresponding values (missing values default
	// to TYPE_EMPTY). No-arg form `New Structure` produces an empty
	// structure to be populated via Insert(name, value) later.
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual void Delete(const ibValue& varKeyValue) override;
	virtual void Insert(const ibValue& varKeyValue, const ibValue& cValue = ibValue()) override;
	virtual bool Property(const ibValue& varKeyValue, ibValue& cValueFound) override;

	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);
	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& cValue);
};

#include <locale>

#endif