#ifndef _ACTION_INFO_H__
#define _ACTION_INFO_H__

#include "core/compiler/compiler.h"

class IActionSource {
protected:	
	class actionData_t {
		CValue* m_srcData;
		struct action_t {
			action_identifier_t m_act_id;
			wxString m_name;
			wxString m_caption;
			wxBitmap m_bitmap;
			bool m_createDef;
			CValue* m_srcData;
			action_t() : m_name(wxEmptyString),
				m_caption(wxEmptyString),
				m_bitmap(wxNullBitmap),
				m_act_id(wxNOT_FOUND) {
			}
			action_t(const action_identifier_t& act_id, const wxString& name, const wxString& description, const wxBitmap& bitmap, bool createDef, CValue* srcData) :
				m_act_id(act_id), m_name(name), m_caption(description), m_bitmap(bitmap), m_createDef(createDef), m_srcData(srcData) {
			}
		};
		std::vector<action_t> m_actions;
	private:

		bool find_act(const action_identifier_t& lNumAction) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [lNumAction](const action_t& act) {
				return lNumAction == act.m_act_id;
				}
			);
			return foundedIt == m_actions.end();
		}

	public:

		CValue* GetSourceData() const {
			return m_srcData;
		}

		actionData_t(CValue* srcData = NULL) : m_srcData(srcData) {}

		void AddAction(const wxString& name, const action_identifier_t& lNumAction, bool createDef = true, CValue* srcData = NULL) {
			wxASSERT(find_act(lNumAction) == NULL);
			m_actions.emplace_back(
				lNumAction,
				name,
				wxEmptyString,
				wxNullBitmap,
				createDef,
				srcData ? srcData : m_srcData
			);
		}

		void AddAction(const wxString& name, const wxString& caption, const action_identifier_t& lNumAction, bool createDef = true, CValue* srcData = NULL) {
			wxASSERT(find_act(lNumAction));
			m_actions.emplace_back(
				lNumAction,
				name,
				caption,
				wxNullBitmap,
				createDef,
				srcData ? srcData : m_srcData
			);
		}

		void AddAction(const wxString& name, const wxString& caption, const wxBitmap& bitmap, const action_identifier_t& lNumAction, bool createDef = true, CValue* srcData = NULL) {
			wxASSERT(find_act(lNumAction));
			m_actions.emplace_back(
				lNumAction,
				name,
				caption,
				bitmap,
				createDef,
				srcData ? srcData : m_srcData
			);
		}

		void AddSeparator() {
			m_actions.emplace_back();
		}

		void RemoveAction(const action_identifier_t& lNumAction) {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [lNumAction](const action_t& act) {
				return lNumAction == act.m_act_id; }
			);
			if (foundedIt != m_actions.end()) {
				m_actions.erase(foundedIt);
			}
		}

		wxString GetNameByID(const action_identifier_t& lNumAction) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [lNumAction](const action_t& act) {
				return lNumAction == act.m_act_id; });
			if (foundedIt != m_actions.end())
				return foundedIt->m_name;
			return wxEmptyString;
		}

		wxString GetCaptionByID(const action_identifier_t& lNumAction) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [lNumAction](const action_t& act) {
				return lNumAction == act.m_act_id; }
			);
			if (foundedIt != m_actions.end()) {
				wxString caption = foundedIt->m_caption;
				return caption.Length() > 0 ?
					caption : foundedIt->m_name;
			}
			return wxEmptyString;
		}

		bool IsCreateInForm(const action_identifier_t& lNumAction) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [lNumAction](const action_t& act) {
				return lNumAction == act.m_act_id; }
			);
			if (foundedIt != m_actions.end()) 
				return foundedIt->m_createDef;

			return true;
		}

		CValue* GetSourceDataByID(const action_identifier_t& lNumAction) const {
			auto foundedIt = std::find_if(m_actions.begin(), m_actions.end(), [lNumAction](const action_t& act) {
				return lNumAction == act.m_act_id; }
			);
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
	//support action 
	virtual actionData_t GetActions(const form_identifier_t& formType) = 0;
	virtual void AddActions(actionData_t& actions, const form_identifier_t& formType) {
		const actionData_t& data = GetActions(formType);
		for (unsigned int i = 0; i < data.GetCount(); i++) {
			const action_identifier_t& id = data.GetID(i);
			if (id != wxNOT_FOUND) {
				actions.AddAction(
					data.GetNameByID(id), data.GetCaptionByID(id),
					id,
					data.IsCreateInForm(id), data.GetSourceDataByID(id)
				);
			}
			else {
				actions.AddSeparator();
			}
		}
	}
		
	// execute action 
	virtual void ExecuteAction(const action_identifier_t& lNumAction, class CValueForm* srcForm) = 0;
};

#endif 