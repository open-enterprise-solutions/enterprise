#ifndef _TRANSLATEMODULE_H__
#define _TRANSLATEMODULE_H__

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <map>
#include <vector>

#include "codeDef.h"
#include "backend/backend_exception.h"

#include "value/value.h"

//Список ключевых слов
struct aKeyWordsDef {
	//char *Rus;
	char* Eng;
	char* strShortDescription;
};

extern BACKEND_API struct aKeyWordsDef s_aKeyWords[];

enum {
	LEXEM_ADD = 0,
	LEXEM_ADDDEF,
	LEXEM_IGNORE,
};

//Свойства функций:
enum {
	RETURN_NONE = 0,//нет возврата (код модуля)
	RETURN_PROCEDURE,//возврат из процедуры
	RETURN_FUNCTION,//возврат из функции
};

//признаки переменных (задаются с отриц. значением в атрибут nArray байт-кода)
enum {
	DEF_VAR_SKIP = -1,//пропущенный параметр
	DEF_VAR_DEFAULT = -2,//параметр по умолчанию
	DEF_VAR_TEMP = -3,//признак временной локальной переменной
	DEF_VAR_NORET = -7,//функция (процедура) не возвращает значения
	DEF_VAR_CONST = 1000,//загрузка констант

};

//определения

//хранение одного примитива из исходного кода
struct lexem_t {

	//тип лексемы:
	short       m_nType;

	//содержание лексемы:
	short		m_nData;		//номер служебного слова(KEYWORD) или символ разделитель(DELIMITER)
	CValue	    m_vData;		//значение, если это константа или реальное имя идентификатора
	wxString	m_strData;		//или имя идентификатора (переменной, функции и пр.)

	//дополнительная информация: 
	wxString m_strModuleName;//имя модуля (т.к. возможны include подключения из разных модулей)
	wxString m_strDocPath; // уникальный путь к документу 
	wxString m_strFileName; // путь к файлу (если внешняя обработка) 

	unsigned int m_nNumberLine;	  //номер строки исходного текста (для точек останова)
	unsigned int m_nNumberString;	  //номер исходного текста (для вывода ошибок)

public:

	//Конструктор: 
	lexem_t() :
		m_nType(0),
		m_nData(0),
		m_nNumberString(0),
		m_nNumberLine(0) {
	}
};

typedef std::vector<lexem_t> CLexemList;

/************************************************
CTranslateCode-этап парсинга исходного кода
Точка входа - процедуры Load() и TranslateModule().
Первая процедура выполняет инициализацию переменных и загрузку
текста выполняемого кода, вторая процедура выполняет трансляцию
(парсинг кода). В качестве результата в структуре класса заполняется
массив "сырого" байт-кода в переменной cByteCode.
*************************************************/

class BACKEND_API CTranslateCode {

	//класс для хранения определений пользователя
	class CDefineList {
		CDefineList* m_parentDefine;
		std::map<wxString, CLexemList*> m_defineList;//содержит массивы лексем
	public:

		CDefineList() : m_parentDefine(nullptr) {};
		~CDefineList() { Clear(); }

		void Clear() { m_defineList.clear(); }

		void SetParent(CDefineList* parent) {
			m_parentDefine = parent;
		}

		void RemoveDef(const wxString& strName);
		bool HasDefine(const wxString& strName) const;
		CLexemList* GetDefine(const wxString& strName);
		void SetDefine(const wxString& strName, CLexemList*);
		void SetDefine(const wxString& strName, const wxString& strValue);
	};

	static CDefineList s_glDefineList;

public:

	CTranslateCode();
	CTranslateCode(const wxString& strModuleName, const wxString& strDocPath);
	CTranslateCode(const wxString& strFileName);

	virtual ~CTranslateCode();

	bool HasDefine(const wxString& strName) const {
		if (m_defineList != nullptr)
			return m_defineList->HasDefine(strName);
		return false;
	};

	//методы:
	void Load(const wxString& strCode);

	void AppendModule(CTranslateCode* module);
	void RemoveModule(CTranslateCode* module);

	virtual void OnSetParent(CTranslateCode* setParent);

	virtual void Clear();
	bool PrepareLexem();

protected:
	void SetError(int codeError, int currPos, const wxString& errorDesc = wxEmptyString) const;
	void SetError(int codeError,
		const wxString& strFileName, const wxString& strModuleName, const wxString& strDocPath,
		int currPos, int currLine,
		const wxString& errorDesc = wxEmptyString) const;
public:
	virtual void ProcessError(const wxString& strFileName,
		const wxString& strModuleName, const wxString& strDocPath,
		unsigned int currPos, unsigned int currLine,
		const wxString& strCodeLineError, int codeError, const wxString& strErrorDesc 
	) const;
public:

	inline void SkipSpaces() const;

	bool IsByte(const wxUniChar&c) const;
	wxUniChar GetByte() const;

	bool IsWord() const;
	wxString GetWord(bool bOrigin = false, bool bGetPoint = false, wxString* psOrig = nullptr);

	bool IsNumber() const;
	wxString GetNumber() const;

	bool IsString() const;
	wxString GetString() const;

	bool IsDate() const;
	wxString GetDate() const;

	bool IsEnd() const;

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	static long long IsKeyWord(const wxString& sKeyWord);
#else
	static int IsKeyWord(const wxString& sKeyWord);
#endif

	wxString GetStrToEndLine() const;
	void PrepareFromCurrent(int nMode, const wxString& strName = wxEmptyString);

	wxString GetModuleName() const {
		return m_strModuleName;
	}

	unsigned int GetBufferSize() const {
		return m_strBuffer.size();
	}

	unsigned int GetCurrentPos() const {
		return m_nCurPos;
	}

	unsigned int GetCurrentLine() const {
		return m_nCurLine;
	}

public:

	static std::map<wxString, void*> m_aHashKeyWords;
	static void LoadKeyWords();

protected:

	//методы и переменные для парсинга текста
	std::vector<CTranslateCode*> m_listTranslateCode;


	//Поддержка "дефайнов":
	CDefineList* m_defineList;

	bool m_bAutoDeleteDefList;
	int m_nModePreparing;

	//атрибуты:
	wxString m_strModuleName;//имя компилируемого модуля (для вывода информации при ошибках)
	wxString m_strDocPath; // уникальный путь к документу 
	wxString m_strFileName; // путь к файлу (если внешняя обработка) 

	unsigned int m_bufferSize;//размер исходного текста

	//исходный текст:
	wxString m_strBuffer;
		
	mutable unsigned int m_nCurPos;//текущая позиция обрабатываемого текста
	mutable unsigned int m_nCurLine;

	//промежуточный массив с лексемами:
	std::vector<lexem_t> m_listLexem;
};

#endif