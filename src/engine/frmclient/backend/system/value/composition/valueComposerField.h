#ifndef __VALUE_COMPOSER_FIELD_H__
#define __VALUE_COMPOSER_FIELD_H__

// The FIELD a composer setting points at — the engine's (backend/system/value/composition/valueComposerField.h) as the
// client's settings windows hold one: a path, how it reads, the column it resolved to and its type, held in a value so a
// condition's either side is a value that may or may not be a field. No script names one here, and no configuration
// builds a declared value into a reference: what is left is the data.

#include "frmclient/backend/compiler/value.h"
#include "frmclient/backend/typeDescription.h"

// The field's own class id, named once — the engine's.
constexpr ibClassID g_compositionFieldCLSID = value_to_clsid("VL_CFLD");

// A DECLARED value — the designer's (CompositionPredefinedValue): which metaobject, which of its declared values, how it
// reads. On the client it is one more value the server wrote; its class id is the engine's.
constexpr ibClassID g_compositionPredefinedCLSID = system_to_clsid("VL_CPRV");

class FRMCLIENT_API ibValueCompositionPredefined : public ibValue {
public:

	ibValueCompositionPredefined() : ibValue(ibValueTypes::TYPE_VALUE, true) {}
	ibValueCompositionPredefined(const ibMetaID& metaId, const ibGuid& guid, const wxString& written)
		: ibValue(ibValueTypes::TYPE_VALUE, true), m_metaId(metaId), m_guid(guid), m_written(written) {
	}

	virtual ibClassID GetClassType() const override { return g_compositionPredefinedCLSID; }
	virtual ibString GetString() const override { return m_written; }
	virtual bool IsEmpty() const override { return m_metaId == wxNOT_FOUND && m_written.IsEmpty(); }

	ibMetaID GetMetaId() const { return m_metaId; }
	const ibGuid& GetGuid() const { return m_guid; }

protected:

	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

private:

	ibMetaID m_metaId = wxNOT_FOUND;   // the metaobject the value belongs to
	ibGuid   m_guid;                   // which of its declared values — empty = the empty reference
	wxString m_written;                // how it reads: `CatalogRef.Goods.Chair`
};

class FRMCLIENT_API ibValueCompositionField : public ibValue {
public:

	ibValueCompositionField();
	explicit ibValueCompositionField(const wxString& path,
		const wxString& presentation = wxEmptyString);
	virtual ~ibValueCompositionField() {}

	virtual ibClassID GetClassType() const override { return g_compositionFieldCLSID; }
	virtual bool IsEmpty() const override { return m_path.IsEmpty(); }
	virtual ibString GetString() const override;

	// Two fields are the same field when their paths are — case-insensitively, as the engine's.
	virtual bool CompareValueEQ(const ibValue& cParam) const override;
	virtual bool CompareValueNE(const ibValue& cParam) const override;

	const wxString& GetPath() const { return m_path; }
	void SetPath(const wxString& path) { m_path = path; }

	const wxString& GetPresentation() const { return m_presentation; }
	void SetPresentation(const wxString& presentation) { m_presentation = presentation; }

	ibMetaID GetLeafId() const { return m_leafId; }
	const ibTypeDescription& GetTypeDescription() const { return m_typeDescription; }

	void SetTypeInfo(const ibMetaID& leafId, const ibTypeDescription& typeDesc) {
		m_leafId = leafId; m_typeDescription = typeDesc;
	}

protected:

	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

private:

	wxString          m_path;            // dot-path in the source's technical names
	wxString          m_presentation;    // what a user sees
	ibMetaID          m_leafId = wxNOT_FOUND;   // the queryable column id, against ONE source
	ibTypeDescription m_typeDescription;        // the field's type — for AdjustValue / choice
};

#endif // __VALUE_COMPOSER_FIELD_H__
