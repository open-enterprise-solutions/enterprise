////////////////////////////////////////////////////////////////////////////
//	Description : the computed provider — rows computed in memory (the source's ComputeRows), read as one
//	              statement in a base in memory (ibQueryComposer::OverMemorySpace). What only metadata
//	              answers is the interface's default. See docs/private/query-one-road.md §7.
////////////////////////////////////////////////////////////////////////////

#include "computedProvider.h"
#include "backend/query/provider/queryComposer.h"            // ibQueryComposer::OverMemorySpace — where a computed source is read
#include "backend/query/queryException.h"

// ⭐ A COMPUTED SOURCE ONLY PRODUCES ROWS. Its read filtered, ordered, paged and folded them here, a second engine beside
// the one a statement runs — and every feature of the language had to be written twice, the second time in memory: a
// window, a page from its anchor, a DISTINCT over the walks. Copied into a base in memory, the rows are read by the
// statement the database provider writes for any source (query-one-road.md §7, S3).
static ibDataQuerySpec InMemory(const ibDataQuerySpec& spec)
{
	if (spec.m_queryable == nullptr)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			_("a computed reading has no source to compute its rows"));
	return ibQueryComposer::OverMemorySpace(spec);
}

ibDataQueryResult ibComputedProvider::ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& req)
{
	const ibDataQuerySpec over = InMemory(spec);
	return over.m_queryable->GetProvider().ExecuteRead(over, req);
}

ibDataQueryResult ibComputedProvider::ExecuteAggregate(const ibDataQuerySpec& spec)
{
	const ibDataQuerySpec over = InMemory(spec);
	return over.m_queryable->GetProvider().ExecuteAggregate(over);
}

// The shared stateless computed provider — vended by ibComputedRegisterQueryable (in queryable.h, which may not name
// the concrete ibComputedProvider) and the temp / subquery computed sources. Declared in queryable.h, defined here
// where ibComputedProvider is complete.
ibBackendQueryProvider& ibComputedProviderInstance()
{
	static ibComputedProvider s_computedProvider;
	return s_computedProvider;
}
