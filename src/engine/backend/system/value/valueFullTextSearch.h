#ifndef __VALUE_FULL_TEXT_SEARCH_H__
#define __VALUE_FULL_TEXT_SEARCH_H__

#include "backend/compiler/value.h"
#include "backend/system/systemEnum.h"

// ⭐⭐ THE BASE'S FULL-TEXT SEARCH, as a script reaches it - the global `FullTextSearch`:
//
//     if (FullTextSearch.GetFullTextSearchMode() = FullTextSearchMode.Enable) {
//         FullTextSearch.UpdateIndex(false, true);
//     }
//
// THE MODE IS THE BASE'S, not a session's and not a person's: one row of sys_settings with no user (the Search
// category), read by every session of the base and written by whoever holds Data administration. Nothing saved
// yet reads as Disable - a base where nobody switched the search on has none.
//
// ⚠ THERE IS NO INDEX YET. What keeps one - Firebird's own full-text search or an index the platform builds over the
// attributes marked for it - is the second step of the same work. Until it lands the maintenance verbs succeed and
// do nothing, so a module that only keeps an index fresh (a scheduled job, an administration form) compiles and runs:
// IndexTrue() is False, UpdateDate() is the empty date, UpdateIndex() and ClearIndex() return having changed nothing.
// They answer the same whatever the mode is: switched off, there is nothing to update either.
void ibValueFullTextSearchManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueFullTextSearchManager : public ibValueStaticMembers<&ibValueFullTextSearchManager_BindNames> {
	enum Func {
		enGetFullTextSearchMode,
		enSetFullTextSearchMode,
		enIndexTrue,
		enUpdateIndex,
		enClearIndex,
		enUpdateDate,
	};
public:

	ibValueFullTextSearchManager();

	virtual ibString GetClassName() const override { return ibT("FullTextSearchManager"); }
	virtual ibString GetString() const override { return ibT("FullTextSearchManager"); }

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// The same verbs for a caller in C++ (and for the tests). Each reaches the base the calling thread works for,
	// and raises where there is none: a mode read from nowhere would be a guess.
	static ibFullTextSearchMode GetMode();
	static void SetMode(ibFullTextSearchMode mode);
	static bool IsIndexTrue();
	static void UpdateIndex(bool merge, bool portion);
	static void ClearIndex();
	static ibDateTime GetUpdateDate();
};

#endif // !__VALUE_FULL_TEXT_SEARCH_H__
