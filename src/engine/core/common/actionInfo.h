#ifndef _ACTION_INFO_H__
#define _ACTION_INFO_H__

#include "compiler/compiler.h"

class IActionSource {
protected:
	class actionData_t {
		CValue* m_srcData;
		struct action_t {
			action_identifier_t m_act_id;
			wxString m_name;
			wxString m_caption;
			wxBitmap m_bitmap;
			CValue* m_srcData;
			action_t() : m_name(wxEmptyString),
				m_caption(wxEmptyString),
				m_bitmap(wxNullBitmap),
				m_act_id(wxNOT_FOUND) {
			}
			action_t(const action_identifier_t& act_id, const wxString& name, const wxString& description, wxBitmap bitmap, CValue* srcData) :
				m_act_id(act_id), m_name(name), m_caption(description), m_bitmap(bitmap), m_srcData(srcData) {
			}
		};
		std::vector<action_t> m_actions;
	private:

		bool find_act(const action_identifier_t& action) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [action](const action_t& act) {
				return action == act.m_act_id;
				}
			);
			return foundedIt == m_actions.end();
		}

	public:

		CValue* GetSourceData() const {
			return m_srcData;
		}

		actionData_t(CValue* srcData = NULL) : m_srcData(srcData) {}

		void AddAction(const wxString& name, const action_identifier_t& action, CValue* srcData = NULL) {
			wxASSERT(find_act(action) == NULL);
			m_actions.emplace_back(
				action,
				name,
				wxEmptyString,
				wxNullBitmap,
				srcData ? srcData : m_srcData
			);
		}

		void AddAction(const wxString& name, const wxString& caption, const action_identifier_t& action, CValue* srcData = NULL) {
			wxASSERT(find_act(action));
			m_actions.emplace_back(
				action,
				name,
				caption,
				wxNullBitmap,
				srcData ? srcData : m_srcData
			);
		}

		void AddAction(const wxString& name, const wxString& caption, wxBitmap bitmap, const action_identifier_t& action, CValue* srcData = NULL) {
			wxASSERT(find_act(action));
			m_actions.emplace_back(
				action,
				name,
				caption,
				bitmap,
				srcData ? srcData : m_srcData
			);
		}

		void AddSeparator() {
			m_actions.emplace_back();
		}

		wxString GetNameByID(const action_identifier_t& action) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [action](const action_t& act) { return action == act.m_act_id; });
			if (foundedIt != m_actions.end())
				return foundedIt->m_name;
			return wxEmptyString;
		}

		wxString GetCaptionByID(const action_identifier_t& action) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [action](const action_t& act) { return action == act.m_act_id; });
			if (foundedIt != m_actions.end()) {
				wxString caption = foundedIt->m_caption;
				return caption.Length() > 0 ?
					caption : foundedIt->m_name;
			}
			return wxEmptyString;
		}

		CValue* GetSourceDataByID(const action_identifier_t& action) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [action](const action_t& act) { return action == act.m_act_id; });
			if (foundedIt != m_actions.end())
				return foundedIt->m_srcData;
			return NULL;
		}

		action_identifier_t GetID(unsigned int idx) const {
			if (idx > GetCount())
				return wxNOT_FOUND;
			return m_actions[idx].m_act_id;
		}

		unsigned int GetCount() const {
			return m_actions.size();
		}
	};

public:

	//support actions 
	virtual actionData_t GetActions(const form_identifier_t& formType) = 0;
	virtual void AddActions(actionData_t& actions, const form_identifier_t& formType) {
		actionData_t& data = GetActions(formType);
		for (unsigned int i = 0; i < data.GetCount(); i++) {
			const action_identifier_t& id = data.GetID(i);
			if (id != wxNOT_FOUND) {
				actions.AddAction(
					data.GetNameByID(id), data.GetCaptionByID(id), id,
					data.GetSourceDataByID(id)
				);
			}
			else {
				actions.AddSeparator();
			}
		}
	}
	virtual void ExecuteAction(const action_identifier_t& action, class CValueForm* srcForm) = 0;
};

#endif 