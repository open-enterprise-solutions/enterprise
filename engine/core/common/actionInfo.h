#ifndef _ACTION_INFO_H__
#define _ACTION_INFO_H__

#include "compiler/compiler.h"

class IActionSource {
protected:
	class actionData_t {
		CValue *m_srcData;
		struct action_t {
			wxString m_name;
			wxString m_caption;
			wxBitmap m_bitmap;
			CValue *m_srcData;
			action_t() : m_name(wxEmptyString),
				m_caption(wxEmptyString),
				m_bitmap(wxNullBitmap)
			{
			}
			action_t(const wxString &name, const wxString &description, wxBitmap bitmap, CValue *srcData) :
				m_name(name), m_caption(description), m_bitmap(bitmap), m_srcData(srcData)
			{
			}
		};
		std::map<action_identifier_t, action_t> m_aActions;
	public:

		CValue *GetSourceData() const { return m_srcData; }
		actionData_t(CValue *srcData = NULL) : m_srcData(srcData) {}

		void AddAction(const wxString &name, action_identifier_t action, CValue *srcData = NULL)
		{
			m_aActions[action] = action_t(name, wxEmptyString, wxNullBitmap, srcData ? srcData : m_srcData);
		}

		void AddAction(const wxString &name, const wxString &caption, action_identifier_t action, CValue *srcData = NULL)
		{
			m_aActions[action] = action_t(name, caption, wxNullBitmap, srcData ? srcData : m_srcData);
		}

		void AddAction(const wxString &name, const wxString &caption, wxBitmap bitmap, action_identifier_t action, CValue *srcData = NULL)
		{
			m_aActions[action] = action_t(name, caption, bitmap, srcData ? srcData : m_srcData);
		}

		wxString GetNameByID(action_identifier_t action)
		{
			if (m_aActions.find(action) != m_aActions.end())
				return m_aActions[action].m_name;

			return wxEmptyString;
		}

		CValue *GetSourceDataByID(action_identifier_t action)
		{
			if (m_aActions.find(action) != m_aActions.end())
				return m_aActions[action].m_srcData;

			return NULL;
		}

		wxString GetCaptionByID(action_identifier_t action)
		{
			if (m_aActions.find(action) != m_aActions.end()) {
				action_t &act = m_aActions.at(action);
				if (act.m_caption.IsEmpty()) {
					return act.m_name;
				}
				return act.m_caption;
			}

			return wxEmptyString;
		}

		action_identifier_t GetID(unsigned int position)
		{
			if (position > GetCount())
				return wxNOT_FOUND;

			auto it = m_aActions.begin();
			std::advance(it, position);

			return it->first;
		}

		unsigned int GetCount() const { return m_aActions.size(); }
	};

public:

	//support actions 
	virtual actionData_t GetActions(form_identifier_t formType) = 0;
	virtual void AddActions(actionData_t &actions, form_identifier_t formType)
	{
		actionData_t aActions = GetActions(formType);
		for (unsigned int i = 0; i < aActions.GetCount(); i++) {
			action_identifier_t action_id = aActions.GetID(i);
			actions.AddAction(aActions.GetNameByID(action_id), action_id, aActions.GetSourceDataByID(action_id));
		}
	}
	virtual void ExecuteAction(action_identifier_t action, class CValueForm *srcForm) = 0;
};

#endif 