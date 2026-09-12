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
// insert_or_assign / erase / count / clear / size / empty / swap / begin..end /
// ==,!=,<) and the SAME sorted iteration order, so it is a transparent swap behind
// the typedef. Used as a std::map key (objectSelector) → provides operator<.
// Two additions a tree does not offer: swap_sorted (a whole row handed over at once)
// and find_value (the value where it lies — a lookup that makes no iterator).
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
	void      swap(ibRowValues& o) noexcept { m_data.swap(o.m_data); }

	// A WHOLE ROW LAID DOWN AT ONCE — for a caller that already holds its entries in key order (a
	// reference filled from a batch read, a table row re-keyed). `sorted` must be ordered by key with no
	// key twice. The two are EXCHANGED: this takes `sorted`, and `sorted` is handed back what was here,
	// so a caller laying down row after row clears it and fills it again in the same memory. Entry by
	// entry the same row cost a search and an insert per entry — a quarter of a batch fill of forty
	// thousand rows (stack samples 2026-09-12).
	void      swap_sorted(container_type& sorted) noexcept { m_data.swap(sorted); }

	// ⭐ EVERY LOOKUP BELOW IS MADE BY POSITION, OVER THE ARRAY ITSELF — an iterator is built only where
	// one is handed out. A checked build (MSVC, _ITERATOR_DEBUG_LEVEL=2) registers every iterator under
	// ONE GLOBAL LOCK and unregisters it again, and a search made of begin, end, the search's own result
	// and the end it was compared with took that lock eight times: on a handful of entries it cost more
	// than a tree's lookup, and moving the RAM snapshot's rows onto this container made a report SLOWER
	// until the searches stopped making iterators (measured 2026-09-12, Debug).

	// --- lookup ---
	iterator       find(const Key& k)       { const size_type pos = LowerIndex(k); return Hit(pos, k) ? m_data.begin() + static_cast<difference_type>(pos) : m_data.end(); }
	const_iterator find(const Key& k) const { const size_type pos = LowerIndex(k); return Hit(pos, k) ? m_data.begin() + static_cast<difference_type>(pos) : m_data.end(); }

	// THE VALUE WHERE IT LIES, or null — a lookup that makes no iterator at all, for the hot readers that
	// only want the value (a snapshot's cell, a fold node's figure). find() answers the same question in
	// the std::map shape, and pays for the iterator it returns.
	T*       find_value(const Key& k)       { const size_type pos = LowerIndex(k); return Hit(pos, k) ? &m_data[pos].second : nullptr; }
	const T* find_value(const Key& k) const { const size_type pos = LowerIndex(k); return Hit(pos, k) ? &m_data[pos].second : nullptr; }

	size_type count(const Key& k) const { return Hit(LowerIndex(k), k) ? 1 : 0; }

	// at() throws std::out_of_range on a miss — same as std::map (some callers
	// rely on the throw, e.g. model.h's try/catch around at()).
	T& at(const Key& k) {
		const size_type pos = LowerIndex(k);
		if (!Hit(pos, k)) throw std::out_of_range("ibRowValues::at");
		return m_data[pos].second;
	}
	const T& at(const Key& k) const {
		const size_type pos = LowerIndex(k);
		if (!Hit(pos, k)) throw std::out_of_range("ibRowValues::at");
		return m_data[pos].second;
	}

	// Default-inserts a value-constructed T if absent (std::map semantics).
	T& operator[](const Key& k) {
		const size_type pos = LowerIndex(k);
		if (Hit(pos, k)) return m_data[pos].second;
		return EmplaceAt(pos, k, T());
	}

	// --- modifiers ---
	std::pair<iterator, bool> insert(const value_type& v) {
		const size_type pos = LowerIndex(v.first);
		if (Hit(pos, v.first)) return { m_data.begin() + static_cast<difference_type>(pos), false };
		return { m_data.insert(m_data.begin() + static_cast<difference_type>(pos), v), true };
	}
	std::pair<iterator, bool> insert(value_type&& v) {
		const size_type pos = LowerIndex(v.first);
		if (Hit(pos, v.first)) return { m_data.begin() + static_cast<difference_type>(pos), false };
		return { m_data.insert(m_data.begin() + static_cast<difference_type>(pos), std::move(v)), true };
	}

	// Templated like std::map::insert_or_assign — the mapped value arrives at the
	// call sites as ibValue / ibValue&& / ibValueTypes / bool, or as a raw
	// ibValue* (which, by convention, means "a runtime-owned object" — ibValue's
	// ctor/assign stores it as a TYPE_REFFER and ref-counts it). Forward M so each
	// rides the matching ibValue converting ctor / operator=.
	template <class M>
	std::pair<iterator, bool> insert_or_assign(const Key& k, M&& obj) {
		const size_type pos = LowerIndex(k);
		if (Hit(pos, k)) {
			m_data[pos].second = std::forward<M>(obj);
			return { m_data.begin() + static_cast<difference_type>(pos), false };
		}
		EmplaceAt(pos, k, T(std::forward<M>(obj)));
		return { m_data.begin() + static_cast<difference_type>(pos), true };
	}

	size_type erase(const Key& k) {
		const size_type pos = LowerIndex(k);
		if (!Hit(pos, k)) return 0;
		m_data.erase(m_data.begin() + static_cast<difference_type>(pos));
		return 1;
	}
	iterator erase(iterator pos)                              { return m_data.erase(pos); }
	iterator erase(const_iterator first, const_iterator last) { return m_data.erase(first, last); }

	// --- comparison (operator< lets ibRowMetaValues be a std::map key) ---
	bool operator==(const ibRowValues& o) const { return m_data == o.m_data; }
	bool operator!=(const ibRowValues& o) const { return m_data != o.m_data; }
	bool operator< (const ibRowValues& o) const { return m_data <  o.m_data; }

private:
	using difference_type = typename container_type::difference_type;

	// Where the first entry whose key is NOT less than k stands — a binary search over the raw array.
	size_type LowerIndex(const Key& k) const {
		const value_type* const first = m_data.data();
		const value_type* const hit = std::lower_bound(first, first + m_data.size(), k,
			[](const value_type& e, const Key& key) { return Compare{}(e.first, key); });
		return static_cast<size_type>(hit - first);
	}
	// Equality under a strict-weak Compare: position `pos` holds k iff !(k < its key).
	bool Hit(size_type pos, const Key& k) const {
		return pos < m_data.size() && !Compare{}(k, m_data[pos].first);
	}
	// A new entry at its place — at the end (the common case: keys arriving in order) without an
	// iterator at all.
	T& EmplaceAt(size_type pos, const Key& k, T&& value) {
		if (pos == m_data.size())
			return m_data.emplace_back(k, std::move(value)).second;
		return m_data.emplace(m_data.begin() + static_cast<difference_type>(pos), k, std::move(value))->second;
	}

	container_type m_data;
};

#endif // __ROW_VALUES_H__
