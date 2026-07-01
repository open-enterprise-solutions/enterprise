#ifndef __VALUE_COMPOSER_H__
#define __VALUE_COMPOSER_H__

// L5-1 — the runtime script value object "DataComposer": the script face of
// ibDataDBComposer (backend/composition/dataComposer.h). The validation surface of
// the simplified first slice — both composer moves are visible from script:
//
//   c = New DataComposer("Catalog.Goods");      // a registered source — the query is RENDERED
//   c = New DataComposer("SELECT … FROM …");    // the author's verbatim query (any whitespace => text)
//   text = c.QueryText();                        // the rendered L4-1 text (the debug view / the AI seam)
//   res  = c.Execute();                          // -> QueryResult — the STANDARD L4-1 result,
//                                                //    .Select() / row walk exactly as with New Query(…)

#include "backend/compiler/value.h"
#include "backend/composition/dataComposer.h"

void ibValueDataComposer_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueDataComposer : public ibValueStaticMembers<&ibValueDataComposer_BindNames>
{
	enum { enSetParameter = 0, enExecute = 1, enQueryText = 2 };   // unified method index space (proc + func)

	ibDataDBComposer m_composer;

public:
	ibValueDataComposer() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE) {}

	bool Init(ibValue** paParams, const long lSizeArray) override;                                   // New DataComposer(source)
	bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;       // SetParameter
	bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
	                ibValue** paParams, const long lSizeArray) override;                              // Execute / QueryText
};

#endif
