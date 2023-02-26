#ifndef _BASE_VARIANT_H_
#define _BASE_VARIANT_H_

#include "core/metadata/metadata.h"

class wxVariantGenerationData : public wxVariantData {
	wxString MakeString() const;
public:

	void SetMetatype(const meta_identifier_t& id) {
		m_genData.insert(id);
	}

	bool Contains(const meta_identifier_t& record_id) const {
		auto itFounded = m_genData.find(record_id);
		return itFounded != m_genData.end();
	}

	meta_identifier_t GetByIdx(unsigned int idx) const {
		if (m_genData.size() == 0)
			return wxNOT_FOUND;
		auto itStart = m_genData.begin();
		std::advance(itStart, idx);
		return *itStart;
	}

	unsigned int GetCount() const {
		return m_genData.size();
	}

	bool Eq(wxVariantData& data) const {
		return true;
	}

	wxVariantGenerationData(IMetadata* metaData) : wxVariantData(), m_metaData(metaData) {}

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
		return wxT("wxVariantGenerationData");
	}

protected:
	IMetadata* m_metaData;
	std::set<meta_identifier_t> m_genData;
};

#endif