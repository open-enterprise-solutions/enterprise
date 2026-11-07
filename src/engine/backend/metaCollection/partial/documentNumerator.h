#ifndef __DOCUMENT_NUMERATOR_H__
#define __DOCUMENT_NUMERATOR_H__

// A DOCUMENT NUMERATOR — one number series that several kinds of document take their numbers from.
//
// Each document kind is a series of its own until it names a numerator (its `Numerator` property).
// From then on the NUMERATOR says what a number is — its type and length, the period it starts again
// in, whether a number may stand twice — and every document that names it takes the next number of
// the one series, checked against all of them. The document's own settings give way while it is
// bound and come back when it is not.
//
// It stores nothing of its own: no table, no rows. Its counter is the row of sys_sequence keyed by
// its path, the same table a lone document's counter lives in. And it does not keep a list of its
// documents — membership lives on the member (the document names it), and GetMembers() asks the
// configuration.

#include "backend/metaCollection/metaObjectComposite.h"
#include "backend/metaCollection/partial/documentEnum.h"

class ibValueMetaObjectDocument;

class BACKEND_API ibValueMetaObjectDocumentNumerator : public ibValueMetaObject {
public:

	ibValueMetaObjectDocumentNumerator(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString);

	//support icons
	virtual ibPicture GetIcon() const override;
	static ibPicture GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags) override;
	virtual bool OnLoadMetaObject(ibMetaData* metaData) override;
	virtual bool OnSaveMetaObject(int flags) override;
	virtual bool OnDeleteMetaObject() override;
	// A numerator documents number from is not removed: the refusal names it and the first of them.
	virtual void RefuseDelete() const override;
	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;
	virtual bool OnAfterCloseMetaObject() override;

	// WHAT A NUMBER OF THIS SERIES IS — a string or a number, and its length. A bound document's Number
	// answers this attribute's type (ibValueMetaObjectRecordDataRecorderRef::GetPredefinedTypeSource).
	ibValueMetaObjectAttributePredefined* GetNumberAttribute() const { return m_propertyNumber->GetMetaObject(); }

	// When the series starts again — the document's own enumeration, the same buckets.
	ibNumberPeriodicity GetNumberPeriodicity() const { return m_propertyNumberPeriodicity->GetValueAsEnum(); }

	// Whether a write is refused when another document of the series already has its number in its period.
	bool IsCheckUnique() const { return m_propertyCheckUnique->GetValueAsBoolean(); }

	// The documents that number from this numerator, in configuration order — asked, never kept.
	std::vector<ibValueMetaObjectDocument*> GetMembers() const;

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(ibT("Data"), _("Data"));

	ibPropertyPredefinedType* m_propertyNumber = ibPropertyObject::CreateProperty<ibPropertyPredefinedType>(m_categoryData,
		CreateMetaObjectAndSetParent<ibValueMetaObjectAttributePredefined>(ibT("Number"), ibGETTEXT_IN_CONTEXT("document attribute", "Number"),
			_("What a number of this series is: a string or a number, and its length. Every document that numbers from the numerator has its Number of this type; a string number is padded with zeros to the length."),
			ibQualifierString(11), false, ibValueTypes::TYPE_STRING, ibItemMode::ibItemMode_Item, ibSelectMode::ibSelectMode_Items, ibIndexingMode::ibIndexingMode_DontIndex));

	ibPropertyEnum<ibValueEnumNumberPeriodicity>* m_propertyNumberPeriodicity = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumNumberPeriodicity>>(m_categoryData, ibT("NumberPeriodicity"), _("Number periodicity"),
		_("When the numerator's numbers start again. Nonperiodical (the default): one series for the life of the numerator. Year, quarter, month or day: the next number is the next one inside that period of the document's date, whichever of the numerator's documents took the last one, and a new period starts again at one."),
		ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical);

	ibPropertyBoolean* m_propertyCheckUnique = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, ibT("CheckUnique"), _("Check unique"),
		_("A document is not written while another document of this numerator - of any of its kinds - already has the same number in the same period. The refusal names that document. Off: a number typed by hand may stand twice."),
		true);
};

#endif
