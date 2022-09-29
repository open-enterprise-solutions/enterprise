#ifndef _METHODS_H__
#define _METHODS_H__

#include "compiler.h"

//Список ключевых слов, которые не могут быть именами переменных и функций
typedef struct SEnglishDef
{
	wxString sName;
	wxString sShortDescription;
	wxString sSynonym;

	int iName;

	SEnglishDef() : sName(wxEmptyString), sShortDescription(wxEmptyString), sSynonym("attribute"), iName(wxNOT_FOUND) {}
	SEnglishDef(const wxString &name) : sName(name), sShortDescription(wxEmptyString), sSynonym(sSynonym), iName(wxNOT_FOUND) {}
	SEnglishDef(const wxString &name, const wxString &shortdescription) : sName(name), sShortDescription(shortdescription), sSynonym("attribute"), iName(wxNOT_FOUND) {}
	SEnglishDef(const wxString &name, const wxString &shortdescription, const wxString &synonym) : sName(name), sShortDescription(shortdescription), sSynonym(sSynonym), iName(wxNOT_FOUND) {}
	SEnglishDef(const wxString &name, const wxString &shortdescription, const wxString &synonym, int realname) : sName(name), sShortDescription(shortdescription), sSynonym(sSynonym), iName(realname) {}

} SEng;

class CMethods
{
	//attributes & methods
	std::map<wxString, int> m_aTreeConstructors;//дерево наименований конструкторов
	std::map<wxString, int> m_aTreeMethods;//дерево наименований методов
	std::map<wxString, int> m_aTreeAttributes;//дерево наименований атрибутов

	std::vector<SEng> m_aConstructors; //список контрукторов (англ)
	std::vector<SEng> m_aMethods; //список методов (англ)
	std::vector<SEng> m_aAttributes; //список атрибутов (англ)

public:

	void ResetConstructors() { 
		m_aTreeConstructors.clear(); 
		m_aConstructors.clear();
	}
	
	void ResetAttributes() { 
		m_aTreeAttributes.clear(); 
		m_aAttributes.clear(); 
	}
	
	void ResetMethods() { 
		m_aTreeMethods.clear(); 
		m_aMethods.clear(); 
	}

	void PrepareConstructors(SEng *Constructors, unsigned int nCount);

	int AppendConstructor(const wxString &constructorName, const wxString &shortDescription);
	int AppendConstructor(const wxString &constructorName, const wxString &shortDescription, const wxString &synonym);
	int AppendConstructor(const wxString &constructorName, const wxString &shortDescription, const wxString &synonym, int realname);
	void RemoveConstructor(const wxString &constructorName);

	int FindConstructor(const wxString &constructorName) const;

	wxString GetConstructorName(unsigned int constructorID) const;
	wxString GetConstructorDescription(unsigned int constructorID) const;
	wxString GetConstructorSynonym(unsigned int constructorID) const;

	int GetConstructorPosition(unsigned int constructorID) const;
	unsigned int GetNConstructors() const noexcept { 
		return m_aConstructors.size();
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void PrepareMethods(SEng *Methods, unsigned int nCount);

	int AppendMethod(const wxString &methodName, const wxString &shortDescription);
	int AppendMethod(const wxString &methodName, const wxString &shortDescription, const wxString &synonym);
	int AppendMethod(const wxString &methodName, const wxString &shortDescription, const wxString &synonym, int realname);
	void RemoveMethod(const wxString &methodName);

	int FindMethod(const wxString &methodName) const;

	wxString GetMethodName(unsigned int methodID) const;
	wxString GetMethodDescription(unsigned int methodID) const;
	wxString GetMethodSynonym(unsigned int methodID) const;

	int GetMethodPosition(unsigned int methodID) const;
	unsigned int GetNMethods() const noexcept {
		return m_aMethods.size();
	}

	void PrepareAttributes(SEng *Attributes, unsigned int nCount);

	int AppendAttribute(const wxString &attributeName);
	int AppendAttribute(const wxString &attributeName, const wxString &synonym);
	int AppendAttribute(const wxString &attributeName, const wxString &synonym, int realname);
	void RemoveAttribute(const wxString &attributeName);

	int FindAttribute(const wxString &attributeName) const;

	wxString GetAttributeName(unsigned int attributeID) const;
	wxString GetAttributeSynonym(unsigned int attributeID) const;

	int GetAttributePosition(unsigned int attributeID) const;
	unsigned int GetNAttributes() const noexcept { 
		return m_aAttributes.size(); 
	}
};

#endif
