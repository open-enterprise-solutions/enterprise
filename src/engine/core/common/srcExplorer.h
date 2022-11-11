#ifndef _SRC_EXPLORER_H__
#define _SRC_EXPLORER_H__

class CSourceExplorer {
	struct source_data_t {
		wxString m_srcName;
		wxString m_srcSynonym;
		IMetaObject* m_metaObject = NULL;
		meta_identifier_t m_meta_id = wxNOT_FOUND;
		std::set<CLASS_ID> m_clsids;
		bool m_enabled = true;
		bool m_visible = true;
		bool m_tableSection = false;
		bool m_select = true;
	};
	std::vector<CSourceExplorer> m_aSrcData;
private:

	CSourceExplorer() {}

	CSourceExplorer(IMetaObjectWrapperData* refData, const CLASS_ID& clsid) {
		m_srcData = { wxT("ref"), _("ref"), refData, refData->GetMetaID(), { clsid }, true, false };
		for (auto attribute : refData->GetGenericAttributes()) {
			CSourceExplorer::AppendSource(attribute);
		}
	}

	CSourceExplorer(IMetaAttributeObject* attribute, bool enabled = true, bool visible = true) {
		m_srcData = { attribute->GetName(), attribute->GetSynonym(), attribute, attribute->GetMetaID(), attribute->GetClsids(), enabled, visible, false };
	}

	CSourceExplorer(CMetaTableObject* tableSection) {
		m_srcData = { tableSection->GetName(), tableSection->GetSynonym(), tableSection, tableSection->GetMetaID(), { tableSection->GetClsidTable() }, true, true, true };
		for (auto attribute : tableSection->GetObjectAttributes()) {
			CSourceExplorer::AppendSource(attribute);
		}
	}

public:

	CSourceExplorer(IMetaObject* refData, const CLASS_ID& clsid, bool tableSection, bool select = false) {
		m_srcData = { wxT("ref"), _("ref"), refData, refData->GetMetaID(), { clsid }, true, true, tableSection, select };
	}

	// this object 
	CSourceExplorer(IMetaObjectWrapperData* refData, const CLASS_ID& clsid, bool tableSection, bool select = false) {
		if (refData->IsDeleted())
			return;
		m_srcData = { refData->GetName(), refData->GetSynonym(), refData, refData->GetMetaID(), { clsid },  true, true, tableSection, select };
	}

	wxString GetSourceName() const { return m_srcData.m_srcName; }
	wxString GetSourceSynonym() const { return m_srcData.m_srcSynonym; }
	bool IsEnabled() const { return m_srcData.m_enabled; }
	bool IsVisible() const { return m_srcData.m_visible; }
	bool IsTableSection() const { return m_srcData.m_tableSection; }
	bool IsSelect() const { return m_srcData.m_select && IsAllowed(); }
	bool IsAllowed() const { return GetMetaSource()->IsAllowed(); }
	IMetaObject* GetMetaSource() const { return m_srcData.m_metaObject; }
	meta_identifier_t GetMetaIDSource() const {
		return m_srcData.m_meta_id;
	}

	std::set<CLASS_ID> GetTypeIDSource() const {
		return m_srcData.m_clsids;
	}

	bool ContainType(const eValueTypes& valType) const {
		
		if (valType == eValueTypes::TYPE_ENUM) {
			for (auto clsid : m_srcData.m_clsids) {
				if (CValue::IsRegisterObject(clsid)) {
					ISimpleObjectValueSingle* singleObject =
						dynamic_cast<ISimpleObjectValueSingle*>(CValue::GetAvailableObject(clsid));
					if (singleObject != NULL) {
						if (singleObject->GetValueType() == eValueTypes::TYPE_ENUM) {
							return true;
						}
					}
				}
			}
			return false;
		}
		
		return m_srcData.m_clsids.find(
			CValue::GetIDByVT(valType)) != m_srcData.m_clsids.end();
	}

	bool ContainType(const CLASS_ID& clsid) const {
		return m_srcData.m_clsids.find(clsid) != m_srcData.m_clsids.end();
	}

	void AppendSource(IMetaObjectWrapperData* refData, const CLASS_ID& clsid) {
		if (refData->IsDeleted())
			return;
		m_aSrcData.emplace_back(
			CSourceExplorer{ refData, clsid }
		);
	}

	void AppendSource(IMetaAttributeObject* attribute, bool enabled = true, bool visible = true) {
		if (attribute->IsDeleted())
			return;
		m_aSrcData.emplace_back(
			CSourceExplorer{ attribute, enabled , visible }
		);
	}

	void AppendSource(CMetaTableObject* tableSection) {
		if (tableSection->IsDeleted())
			return;
		m_aSrcData.emplace_back(
			CSourceExplorer{ tableSection }
		);
	}

	CSourceExplorer GetHelper(unsigned int idx) {
		if (m_aSrcData.size() < idx) {
			return CSourceExplorer();
		}
		return m_aSrcData[idx];
	}

	unsigned int GetHelperCount() const { 
		return m_aSrcData.size();
	}

protected:

	source_data_t m_srcData;
};

#endif 