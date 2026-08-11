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
	// deterministic iteration / serialisation order. `m_index` maps the KEY
	// ITSELF to its entry, so a lookup is one hash probe with NO string built:
	// the hash and equality read the key's identity IN PLACE (see KeyHash/KeyEq).
	// The two are maintained together by every mutating method.

	// The key's identity as a wide C-string, read WITHOUT allocating for the
	// common case: a string value is returned zero-copy through its own buffer;
	// anything else (number / reference) is materialised into `scratch` from its
	// hash identity (guid / text). Hash and equality both go through this ONE
	// helper, so they can never disagree — the invariant an unordered_map lives on.
	static const wchar_t* Identity(const ibValue& key, ibString& scratch);

	struct KeyHash { size_t operator()(const ibValue& k) const; };
	struct KeyEq   { bool   operator()(const ibValue& a, const ibValue& b) const; };

	std::vector<std::pair<ibValue, ibValue>> m_entries;
	std::unordered_map<ibValue, size_t, KeyHash, KeyEq> m_index;

protected:
	// -1 when absent; the entry index otherwise. The single lookup primitive the
	// key-facing methods share.
	long IndexOf(const ibValue& key) const;

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