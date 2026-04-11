#ifndef __ACTION_INFO_H__
#define __ACTION_INFO_H__

struct ibActionDescription {

	ibActionID m_lAction;
	wxString m_strAction;

	ibActionDescription(const ibActionID& lAction) : m_lAction(lAction), m_strAction() {}
	ibActionDescription(const wxString& strAction) : m_lAction(wxNOT_FOUND), m_strAction(strAction) {}

	wxString GetCustomAction() const { return m_strAction; }
	ibActionID GetSystemAction() const { return m_lAction; }

	bool IsEmptyAction() const {
		if (m_lAction == wxNOT_FOUND)
			return m_strAction.IsEmpty();
		return true;
	}

	bool operator == (const ibActionDescription& rhs) const {
		if (m_lAction == wxNOT_FOUND)
			return m_strAction == rhs.m_strAction;
		return m_lAction == rhs.m_lAction;
	}

	friend class ibActionDescriptionMemory;
};

class ibActionDescriptionMemory {
public:
	//load & save object in control 
	static bool LoadData(class ibReaderMemory& reader, ibActionDescription& typeDesc);
	static bool SaveData(class ibWriterMemory& writer, ibActionDescription& typeDesc);
};

#include "backend_picture.h"

class ibActionDataObject {
protected:

	class ibActionCollection {

		ibValue* m_srcData;

		struct ibActionItem {
			ibActionID m_act_id;
			wxString m_name;
			wxString m_caption;
			ibPictureDescription m_pictureDescription;
			bool m_pictureAndText;
			bool m_createDef;
			ibValue* m_srcData;

			ibActionItem() : m_act_id(wxNOT_FOUND), m_name(wxEmptyString), m_caption(wxEmptyString), m_pictureDescription(), m_pictureAndText(false), m_createDef(false) {}
			ibActionItem(const ibActionID& act_id, const wxString& name, const wxString& description, bool createDef, ibValue* srcData) : m_act_id(act_id), m_name(name), m_caption(description), m_pictureDescription(), m_pictureAndText(false), m_createDef(createDef), m_srcData(srcData) {}
			ibActionItem(const ibActionID& act_id, const wxString& name, const wxString& description, const ibPictureDescription& pictureDescription, bool pictureAndText, bool createDef, ibValue* srcData) : m_act_id(act_id), m_name(name), m_caption(description), m_pictureDescription(pictureDescription), m_pictureAndText(pictureAndText), m_createDef(createDef), m_srcData(srcData) {}
		};

		std::vector<ibActionItem> m_vecAction;

	private:

		bool IsExistAction(const ibActionID& lNumAction) const {
			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });
			return iterator == m_vecAction.end();
		}

	public:

		ibValue* GetSourceData() const { return m_srcData; }
		ibActionCollection(ibValue* srcData = nullptr) : m_srcData(srcData) {}

		void AddAction(const wxString& name, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.emplace_back(
				lNumAction,
				name,
				wxEmptyString,
				ibPictureDescription(),
				false,
				createDef,
				srcData ? srcData : m_srcData
			);
		}

		void AddAction(const wxString& name, const wxString& caption, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.emplace_back(
				lNumAction,
				name,
				caption,
				ibPictureDescription(),
				false,
				createDef,
				srcData ? srcData : m_srcData
			);
		}

		void AddAction(const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.emplace_back(
				lNumAction,
				name,
				caption,
				pictureDescription,
				false,
				createDef,
				srcData ? srcData : m_srcData
			);
		}

		void AddAction(const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, bool pictureAndText, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.emplace_back(
				lNumAction,
				name,
				caption,
				pictureDescription,
				pictureAndText,
				createDef,
				srcData ? srcData : m_srcData
			);
		}

		void AddSeparator() { m_vecAction.emplace_back(); }

		void InsertAction(unsigned int index, const wxString& name, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.insert(m_vecAction.begin() + index, {
				lNumAction,
				name,
				wxEmptyString,
				ibPictureDescription(),
				false,
				createDef,
				srcData ? srcData : m_srcData
				}
			);
		}

		void InsertAction(unsigned int index, const wxString& name, const wxString& caption, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.insert(m_vecAction.begin() + index, {
				lNumAction,
				name,
				caption,
				ibPictureDescription(),
				false,
				createDef,
				srcData ? srcData : m_srcData
				}
			);
		}

		void InsertAction(unsigned int index, const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.insert(m_vecAction.begin() + index, {
				lNumAction,
				name,
				caption,
				pictureDescription,
				createDef,
								false,
				srcData ? srcData : m_srcData
				}
			);
		}

		void InsertAction(unsigned int index, const wxString& name, const wxString& caption, const ibPictureDescription& pictureDescription, bool pictureAndText, const ibActionID& lNumAction, bool createDef = true, ibValue* srcData = nullptr) {
			wxASSERT(IsExistAction(lNumAction));
			m_vecAction.insert(m_vecAction.begin() + index, {
				lNumAction,
				name,
				caption,
				pictureDescription,
				createDef,
				pictureAndText,
				srcData ? srcData : m_srcData
				}
			);
		}

		void InsertSeparator(unsigned int index) { m_vecAction.insert(m_vecAction.begin() + index, {}); }

		void RemoveAction(const ibActionID& lNumAction) {

			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });

			if (iterator != m_vecAction.end()) m_vecAction.erase(iterator);
		}

		wxString GetNameByID(const ibActionID& lNumAction) const {

			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });

			if (iterator != m_vecAction.end())
				return iterator->m_name;

			return wxEmptyString;
		}

		wxString GetCaptionByID(const ibActionID& lNumAction) const {

			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });

			if (iterator != m_vecAction.end()) {
				wxString caption = iterator->m_caption;
				return caption.Length() > 0 ?
					caption : iterator->m_name;
			}

			return wxEmptyString;
		}

		ibPictureDescription GetPictureByID(const ibActionID& lNumAction) const {

			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });

			if (iterator != m_vecAction.end())
				return iterator->m_pictureDescription;

			return ibPictureDescription();
		}

		bool IsCreatePictureAndText(const ibActionID& lNumAction) const {

			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });

			if (iterator != m_vecAction.end())
				return iterator->m_pictureAndText;

			return true;
		}

		bool IsCreateInForm(const ibActionID& lNumAction) const {

			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });

			if (iterator != m_vecAction.end())
				return iterator->m_createDef;

			return true;
		}

		ibValue* GetSourceDataByID(const ibActionID& lNumAction) const {

			auto iterator = std::find_if(m_vecAction.begin(), m_vecAction.end(), [lNumAction](const ibActionItem& act) {
				return lNumAction == act.m_act_id; });

			if (iterator != m_vecAction.end())
				return iterator->m_srcData;
			return nullptr;
		}

		ibActionID GetID(unsigned int idx) const {
			if (idx > GetCount())
				return wxNOT_FOUND;
			return m_vecAction[idx].m_act_id;
		}

		unsigned int GetCount() const { return m_vecAction.size(); }
	};

public:

	//support action 
	virtual ibActionCollection GetActionCollection(const ibFormID& formType) = 0;
	virtual void AppendActionCollection(ibActionCollection& actionData, const ibFormID& formType) {
		const ibActionCollection& data = GetActionCollection(formType);
		for (unsigned int i = 0; i < data.GetCount(); i++) {
			const ibActionID& id = data.GetID(i);
			if (id != wxNOT_FOUND) {
				actionData.AddAction(
					data.GetNameByID(id),
					data.GetCaptionByID(id),
					data.GetPictureByID(id),
					data.IsCreatePictureAndText(id),
					id,
					data.IsCreateInForm(id),
					data.GetSourceDataByID(id)
				);
			}
			else {
				actionData.AddSeparator();
			}
		}
	}

	// execute action 
	virtual void ExecuteAction(const ibActionID& lNumAction, class ibBackendValueForm* srcForm) = 0;
};

#endif 