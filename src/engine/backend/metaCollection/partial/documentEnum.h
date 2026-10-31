#ifndef __DOCUMENT_ENUM_H__
#define __DOCUMENT_ENUM_H__

enum ibDocumentWriteMode {
	ibDocumentWriteMode_Posting,
	ibDocumentWriteMode_UndoPosting,
	ibDocumentWriteMode_Write
};

enum ibDocumentPostingMode {
	ibDocumentPostingMode_RealTime,
	ibDocumentPostingMode_Regular
};

// WHAT THE PLATFORM DOES WITH A DOCUMENT'S MOVEMENTS when it is posted again or its posting undone (Max, 2026-09-14).
// A deleted document takes them with it whatever this says (Max, 2026-09-15). The numbers are what a saved
// configuration holds.
enum ibDocumentRecordsDeletion {
	ibDocumentRecordsDeletion_Automatically,    // cleared before a posting again and when a posting is undone
	ibDocumentRecordsDeletion_OnUndoPosting,    // kept when posted again — a set the handler writes replaces its own —
	                                            // and cleared when a posting is undone
	ibDocumentRecordsDeletion_Never             // not cleared by the platform on either: the configuration clears them
};

// When a document's numbers start again. The numbers are what a saved configuration holds, so a new
// one goes at the end. Nonperiodical is zero: a document saved before the setting existed, and every
// new document, stays on the one series it already had.
enum ibNumberPeriodicity {
	ibNumberPeriodicity_Nonperiodical,
	ibNumberPeriodicity_Year,
	ibNumberPeriodicity_Quarter,
	ibNumberPeriodicity_Month,
	ibNumberPeriodicity_Day,
};

// The sequence bucket a nonperiodical document — and every catalog — still uses. A periodic
// document's bucket lives above a billion, so it cannot land on this one and restart that series.
constexpr int ibNumberSeriesNonperiodical = 20000101;

#pragma region enumeration
#include "backend/compiler/enumUnit.h"

class ibValueEnumDocumentWriteMode : public ibValueEnumeration<ibDocumentWriteMode> {
	public:
	ibValueEnumDocumentWriteMode() : ibValueEnumeration() {}
	//ibValueEnumDocumentWriteMode(ibDocumentWriteMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_Posting, ibT("Posting"), _("Posting"));
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting, ibT("UndoPosting"), _("Undo posting"));
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_Write, ibT("Write"), _("Write"));
	}
};
class ibValueEnumDocumentPostingMode : public ibValueEnumeration<ibDocumentPostingMode> {
	public:
	ibValueEnumDocumentPostingMode() : ibValueEnumeration() {}
	//ibValueEnumDocumentPostingMode(ibDocumentPostingMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentPostingMode::ibDocumentPostingMode_RealTime, ibT("RealTime"), _("Real time"));
		AddEnumeration(ibDocumentPostingMode::ibDocumentPostingMode_Regular, ibT("Regular"), _("Regular"));
	}
};
class ibValueEnumDocumentRecordsDeletion : public ibValueEnumeration<ibDocumentRecordsDeletion> {
	public:
	ibValueEnumDocumentRecordsDeletion() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_Automatically, ibT("Automatically"), _("Delete automatically"));
		AddEnumeration(ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_OnUndoPosting, ibT("OnUndoPosting"), _("Delete automatically on undo posting"));
		AddEnumeration(ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_Never, ibT("Never"), _("Do not delete automatically"));
	}
};
class ibValueEnumNumberPeriodicity : public ibValueEnumeration<ibNumberPeriodicity> {
	public:
	ibValueEnumNumberPeriodicity() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical, ibT("Nonperiodical"), _("Nonperiodical"));
		AddEnumeration(ibNumberPeriodicity::ibNumberPeriodicity_Year, ibT("Year"), _("Year"));
		AddEnumeration(ibNumberPeriodicity::ibNumberPeriodicity_Quarter, ibT("Quarter"), _("Quarter"));
		AddEnumeration(ibNumberPeriodicity::ibNumberPeriodicity_Month, ibT("Month"), _("Month"));
		AddEnumeration(ibNumberPeriodicity::ibNumberPeriodicity_Day, ibT("Day"), _("Day"));
	}
};
#pragma endregion

BACKEND_API int ibDocumentNumberInterval(ibNumberPeriodicity period, const class ibDateTime& when);

// The stretch a periodic number repeats in, as the unit a date is cut to (BeginOfPeriod) — what
// GetPeriodicityUnit is to a register. Asked of a periodic number only: a nonperiodical one has no
// stretch, and the series key and FindByNumber both answer for it before they ask.
enum ibTotalsPeriod ibNumberPeriodicityUnit(ibNumberPeriodicity period);

#endif // ! _DOCUMENT_EMUN_H_
