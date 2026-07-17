#ifndef __ROW_VALUES_H__
#define __ROW_VALUES_H__

#include <vector>
#include <utility>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <cstddef>

// =============================================================================
// ibRowValues<Key, T, Compare> — drop-in for std::map<Key, T> backed by ONE
// sorted std::vector<pair<Key,T>>. The engine's "one row of values" container:
// in practice the sole instantiation is ibRowValues<ibMetaID, ibValue> — the
// keyed value set of a single record object / table row (see ibRowMetaValues in
// backend_core.h), hence the name.
//
// Why, vs std::map (red-black tree):
//   - lookup: O(log n) but over CONTIGUOUS memory (binary search, no pointer
//     chasing) — markedly faster constant factor for the small n (5..50) here;
//   - allocation: ONE block for the whole map instead of a heap node PER entry;
//   - RAM: no ~32-byte RB-node header per value (× millions of loaded records /
//     table rows) — the same footprint philosophy as the ibValue audit.
//
// Keeps the std::map subset the codebase actually uses (find / at / [] / insert /
// insert_or_assign / erase / count / clear / size / empty / begin..end / ==,!=,<)
// and the SAME sorted iteration order, so it is a transparent swap behind the
// typedef. Used as a std::map key (objectSelector) → provides operator<.
//
// Differences from std::map (both benign for the current call sites):
//   - value_type holds a NON-const Key (the vector must stay sortable/movable);
//     never mutate it->first (would break the sorted invariant). Nothing does.
//   - refs/iterators from operator[] / insert are invalidated by the NEXT
//     structural mutation (vector growth), unlike std::map's stable nodes. The
//     hot paths never insert mid-iteration / mid-borrow, so this matches use.
//
// Completeness: the typedef in backend_core.h is only an ALIAS (ibValue is
// forward-declared there — no instantiation). Every site that declares an
// ibRowMetaValues *member* sees a complete ibValue (via value.h), which is what
// instantiating the vector<pair<Key,T>> needs — same requirement std::map had.
// =============================================================================
template <class Key, class T, class Compare = std::less<Key>>
class ibRowValues {
public:
	using key_type       = Key;
	using mapped_type    = T;
	using value_type     = std::pair<Key, T>;
	using container_type = std::vector<value_type>;
	using iterator       = typename container_type::iterator;
	using const_iterator = typename container_type::const_iterator;
	using size_type      = typename container_type::size_type;

	ibRowValues() = default;

	// --- iteration (sorted by key, like std::map) ---
	iterator       begin()        noexcept { return m_data.begin(); }
	iterator       end()          noexcept { return m_data.end(); }
	const_iterator begin()  const noexcept { return m_data.begin(); }
	const_iterator end()    const noexcept { return m_data.end(); }
	const_iterator cbegin() const noexcept { return m_data.cbegin(); }
	const_iterator cend()   const noexcept { return m_data.cend(); }

	// --- capacity ---
	bool      empty() const noexcept { return m_data.empty(); }
	size_type size()  const noexcept { return m_data.size(); }
	void      clear()       noexcept { m_data.clear(); }
	void      reserve(size_type n)   { m_data.reserve(n); }

	// --- lookup ---
	iterator       find(const Key& k)       { iterator it = LowerBound(k);       return Hit(it, k) ? it : m_data.end(); }
	const_iterator find(const Key& k) const { const_iterator it = LowerBound(k); return Hit(it, k) ? it : m_data.end(); }

	size_type count(const Key& k) const { return find(k) != m_data.end() ? 1 : 0; }

	// at() throws std::out_of_range on a miss — same as std::map (some callers
	// rely on the throw, e.g. model.h's try/catch around at()).
	T& at(const Key& k) {
		iterator it = LowerBound(k);
		if (!Hit(it, k)) throw std::out_of_range("ibRowValues::at");
		return it->second;
	}
	const T& at(const Key& k) const {
		const_iterator it = LowerBound(k);
		if (!Hit(it, k)) throw std::out_of_range("ibRowValues::at");
		return it->second;
	}

	// Default-inserts a value-constructed T if absent (std::map semantics).
	T& operator[](const Key& k) {
		iterator it = LowerBound(k);
		if (Hit(it, k)) return it->second;
		return m_data.emplace(it, k, T())->second;
	}

	// --- modifiers ---
	std::pair<iterator, bool> insert(const value_type& v) {
		iterator it = LowerBound(v.first);
		if (Hit(it, v.first)) return { it, false };
		return { m_data.insert(it, v), true };
	}
	std::pair<iterator, bool> insert(value_type&& v) {
		iterator it = LowerBound(v.first);
		if (Hit(it, v.first)) return { it, false };
		return { m_data.insert(it, std::move(v)), true };
	}

	// Templated like std::map::insert_or_assign — the mapped value arrives at the
	// call sites as ibValue / ibValue&& / ibValueTypes / bool, or as a raw
	// ibValue* (which, by convention, means "a runtime-owned object" — ibValue's
	// ctor/assign stores it as a TYPE_REFFER and ref-counts it). Forward M so each
	// rides the matching ibValue converting ctor / operator=.
	template <class M>
	std::pair<iterator, bool> insert_or_assign(const Key& k, M&& obj) {
		iterator it = LowerBound(k);
		if (Hit(it, k)) { it->second = std::forward<M>(obj); return { it, false }; }
		return { m_data.emplace(it, k, T(std::forward<M>(obj))), true };
	}

	size_type erase(const Key& k) {
		iterator it = LowerBound(k);
		if (!Hit(it, k)) return 0;
		m_data.erase(it);
		return 1;
	}
	iterator erase(iterator pos)                              { return m_data.erase(pos); }
	iterator erase(const_iterator first, const_iterator last) { return m_data.erase(first, last); }

	// --- comparison (operator< lets ibRowMetaValues be a std::map key) ---
	bool operator==(const ibRowValues& o) const { return m_data == o.m_data; }
	bool operator!=(const ibRowValues& o) const { return m_data != o.m_data; }
	bool operator< (const ibRowValues& o) const { return m_data <  o.m_data; }

private:
	// First element whose key is NOT less than k (binary search on the key).
	iterator LowerBound(const Key& k) {
		return std::lower_bound(m_data.begin(), m_data.end(), k,
			[](const value_type& e, const Key& key) { return Compare{}(e.first, key); });
	}
	const_iterator LowerBound(const Key& k) const {
		return std::lower_bound(m_data.begin(), m_data.end(), k,
			[](const value_type& e, const Key& key) { return Compare{}(e.first, key); });
	}
	// Equality under a strict-weak Compare: it points at k iff !(k < it->first).
	bool Hit(const_iterator it, const Key& k) const {
		return it != m_data.end() && !Compare{}(k, it->first);
	}

	container_type m_data;
};

#endif // __ROW_VALUES_H__
