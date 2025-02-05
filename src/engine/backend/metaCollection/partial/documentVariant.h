#ifndef _DOCUMENT_VARIANT_
#define _DOCUMENT_VARIANT_

#include "backend/metaData.h"

class BACKEND_API wxVariantRecordData : public wxVariantData {
	wxString MakeString() const;
public:

	void SetMetatype(const meta_identifier_t& record_id) {
		m_recordData.insert(record_id);
	}

	bool Contains(const meta_identifier_t& record_id) const {
		auto it = m_recordData.find(record_id);
		return it != m_recordData.end();
	}

	meta_identifier_t GetByIdx(unsigned int idx) const {
		if (m_recordData.size() == 0)
			return wxNOT_FOUND;
		auto itStart = m_recordData.begin();
		std::advance(itStart, idx);
		return *itStart;
	}

	unsigned int GetCount() const {
		return m_recordData.size();
	}

	bool Eq(wxVariantData& data) const {
		return true;
	}

	wxVariantRecordData(IMetaData* metaData) : wxVariantData(), m_metaData(metaData) {}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	wxString GetType() const {
		return wxT("wxVariantRecordData");
	}

protected:
	IMetaData* m_metaData;
	std::set<meta_identifier_t> m_recordData;
};

#endif // !_DOCUMENT_VARIANT_
