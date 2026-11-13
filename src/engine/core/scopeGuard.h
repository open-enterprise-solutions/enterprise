#ifndef _CORE_SCOPE_GUARD_H__
#define _CORE_SCOPE_GUARD_H__

// ibON_BLOCK_EXIT_SET / ibON_BLOCK_EXIT_NULL - what wx/scopeguard.h's macros of the same names do, without wx: a
// variable takes a value when the block it is written in ends, however it ends (a return, an exception).
//
//   m_fetching = true;
//   ibON_BLOCK_EXIT_SET(m_fetching, false);

#include <type_traits>
#include <utility>

#include "core/coreApi.h"   // ibCONCAT

namespace ibScopeGuardPrivate {

// The variable and the value it takes at the end of the block - wxPrivate::VariableSetter.
template<class T>
class ibVariableSetter {
public:
	template<class U>
	ibVariableSetter(T& variable, U&& value) : m_variable(variable), m_value(std::forward<U>(value)) {}
	~ibVariableSetter() { m_variable = std::move(m_value); }

	ibVariableSetter(const ibVariableSetter&) = delete;
	ibVariableSetter& operator=(const ibVariableSetter&) = delete;

private:
	T& m_variable;
	T  m_value;
};

} // namespace ibScopeGuardPrivate

#define ibON_BLOCK_EXIT_SET(var, value) \
	const ibScopeGuardPrivate::ibVariableSetter<std::remove_reference_t<decltype(var)>> \
		ibCONCAT(ibBlockExitSetter_, __LINE__)((var), (value))

#define ibON_BLOCK_EXIT_NULL(ptr) ibON_BLOCK_EXIT_SET(ptr, nullptr)

#endif
