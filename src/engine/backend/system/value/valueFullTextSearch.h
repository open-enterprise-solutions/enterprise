#ifndef __VALUE_FULL_TEXT_SEARCH_H__
#define __VALUE_FULL_TEXT_SEARCH_H__

#include "backend/compiler/value.h"
#include "backend/fullTextSearch/fullTextIndex.h"
#include "backend/system/systemEnum.h"

// ⭐⭐ THE BASE'S FULL-TEXT SEARCH, as a script reaches it - the global `FullTextSearch`:
//
//     List = FullTextSearch.CreateList("wheel 205/55", 20);
//     List.FirstPart();
//     for (i = 0; i < List.Count(); i++) { Message(List.Get(i).Presentation); }
//
// THE MODE IS THE BASE'S, one row of sys_settings with no user, read by every session and written by whoever holds
// Data administration. Nothing saved reads as Disable.
//
// THE INDEX keeps itself: a record's words are written with the record, in its transaction (ibFullTextIndex). What
// is left to the verbs here is the BUILD after the index was cleared or a field's flag switched on - IndexTrue says
// whether anything is left, UpdateIndex builds it, ClearIndex empties it, UpdateDate says when it last completed.
void ibValueFullTextSearchManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueFullTextSearchManager : public ibValueStaticMembers<&ibValueFullTextSearchManager_BindNames> {
	enum Func {
		enGetFullTextSearchMode,
		enSetFullTextSearchMode,
		enIndexTrue,
		enUpdateIndex,
		enClearIndex,
		enUpdateDate,
		enCreateList,
	};
public:

	ibValueFullTextSearchManager();

	virtual ibString GetClassName() const override { return ibT("FullTextSearchManager"); }
	virtual ibString GetString() const override { return ibT("FullTextSearchManager"); }

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// The same verbs for a caller in C++ (the administration form, the tests). Each reaches the base the calling
	// thread works for, and raises where there is none. Changing the mode and the index needs Data administration.
	static ibFullTextSearchMode GetMode();
	static void SetMode(ibFullTextSearchMode mode);
	static bool IsIndexTrue();
	static ibFullTextIndex::ibProgress UpdateIndex(bool portion);
	static ibFullTextIndex::ibProgress GetProgress();
	static void ClearIndex();
	static ibDateTime GetUpdateDate();
};

// ⭐ WHAT A SEARCH FOUND - one record: its reference, its metaobject, and how it is shown.
void ibValueFullTextSearchItem_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueFullTextSearchItem : public ibValueStaticMembers<&ibValueFullTextSearchItem_BindNames> {
	enum Prop {
		enValue,
		enMetadata,
		enPresentation,
	};
public:

	ibValueFullTextSearchItem();
	ibValueFullTextSearchItem(const ibValue& value, const ibValue& metadata, const ibString& presentation);

	virtual ibString GetString() const override { return m_presentation; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

private:
	ibValue m_value;
	ibValue m_metadata;
	ibString m_presentation;
};

// ⭐ A SEARCH AND ITS PARTS - made by FullTextSearch.CreateList(text, portionSize). Nothing is searched until
// FirstPart; then the found records are read a part at a time, the part's items by Get(index).
void ibValueFullTextSearchList_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueFullTextSearchList : public ibValueStaticMembers<&ibValueFullTextSearchList_BindNames> {
	enum Func {
		enFirstPart,
		enNextPart,
		enPreviousPart,
		enCount,
		enTotalCount,
		enStartIndex,
		enGet,
	};
	enum Prop {
		enSearchString,
		enPortionSize,
	};
public:

	ibValueFullTextSearchList();
	ibValueFullTextSearchList(const ibString& searchString, int portionSize);

	virtual ibString GetString() const override { return ibT("FullTextSearchList"); }

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	// The verbs for C++ (the Find dialog, the tests).
	void FirstPart();
	void NextPart();
	void PreviousPart();
	long Count() const;
	long TotalCount() const;
	long StartIndex() const { return m_start; }
	ibValue Get(long index) const;

	static constexpr int kDefaultPortionSize = 20;

private:
	void Searched() const;

	ibString m_searchString;
	int m_portionSize = kDefaultPortionSize;
	bool m_searched = false;
	long m_start = 0;
	std::vector<ibFullTextIndex::ibHit> m_hits;
};

#endif // !__VALUE_FULL_TEXT_SEARCH_H__
