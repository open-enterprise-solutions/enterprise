#ifndef _TRANSLATEMODULE_H__
#define _TRANSLATEMODULE_H__

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <map>
#include <vector>

#include "value.h"
#include "functions.h"

//Список ключевых слов
struct aKeyWordsDef {
	//char *Rus;
	char *Eng;
	char *sShortDescription;
};

extern struct aKeyWordsDef aKeyWords[];

enum
{
	LEXEM_ADD = 0,
	LEXEM_ADDDEF,
	LEXEM_IGNORE,
};

//Свойства функций:
enum
{
	RETURN_NONE = 0,//нет возврата (код модуля)
	RETURN_PROCEDURE,//возврат из процедуры
	RETURN_FUNCTION,//возврат из функции
};

//признаки переменных (задаются с отриц. значением в атрибут nArray байт-кода)
enum
{
	DEF_VAR_SKIP = -1,//пропущенный параметр
	DEF_VAR_DEFAULT = -2,//параметр по умолчанию
	DEF_VAR_TEMP = -3,//признак временной локальной переменной
	DEF_VAR_NORET = -7,//функция (процедура) не возвращает значения
	DEF_VAR_CONST = 1000,//загрузка констант

};

//определения

//хранение одного примитива из исходного кода
struct CLexem
{
	//тип лексемы:
	short       m_nType;

	//содержание лексемы:
	short	    m_nData;		//номер служебного слова(KEYWORD) или символ разделитель(DELIMITER)
	CValue	    m_vData;		//значение, если это константа или реальное имя идентификатора
	wxString	m_sData;		//или имя идентификатора (переменной, функции и пр.)

	//дополнительная информация: 
	wxString m_sModuleName;//имя модуля (т.к. возможны include подключения из разных модулей)
	wxString m_sDocPath; // уникальный путь к документу 
	wxString m_sFileName; // путь к файлу (если внешняя обработка) 

	unsigned int m_nNumberLine;	  //номер строки исходного текста (для точек останова)
	unsigned int m_nNumberString;	  //номер исходного текста (для вывода ошибок)

	//Конструктор: 
	CLexem() : m_nType(0), m_nData(0), m_nNumberString(0), m_nNumberLine(0) {}
};

typedef std::vector<CLexem> CLexemList;

//класс для хранения определений пользователя
class CDefList
{
public:

	CDefList() : pParent(NULL) {};
	~CDefList();

	std::map<wxString, CLexemList *> DefList;//содержит массивы лексем
	CDefList *pParent;

	void SetParent(CDefList *p);
	void RemoveDef(const wxString &sName);
	bool HasDef(const wxString &sName);
	CLexemList *GetDef(const wxString &sName);
	void SetDef(const wxString &sName, CLexemList*);
	void SetDef(const wxString &sName, wxString sValue);
};

/************************************************
CTranslateModule-этап парсинга исходного кода
Точка входа - процедуры Load() и TranslateModule().
Первая процедура выполняет инициализацию переменных и загрузку
текста выполняемого кода, вторая процедура выполняет трансляцию
(парсинг кода). В качестве результата в структуре класса заполняется
массив "сырого" байт-кода в переменной cByteCode.
*************************************************/

class CTranslateModule
{
	static CDefList glDefList;

public:

	CTranslateModule();
	CTranslateModule(const wxString &moduleName, const wxString &docPath);
	CTranslateModule(const wxString &fileName);

	virtual ~CTranslateModule();

	bool HasDef(const wxString &sName)
	{
		if (m_aDefList) {
			return m_aDefList->HasDef(sName);
		}
		return false;
	};

	//методы:
	void Load(const wxString &code);

	void AppendModule(CTranslateModule *module);
	void RemoveModule(CTranslateModule *module);

	void OnSetParent(CTranslateModule *setParent);

	void Clear();
	bool PrepareLexem();

	void SetError(int codeError, int currPos, const wxString &errorDesc = wxEmptyString);

	void SetError(int codeError, 
		const wxString &fileName, const wxString &moduleName, const wxString &docPath,
		int currPos, int currLine, 
		const wxString &errorDesc = wxEmptyString);

	const std::vector<CLexem> &GetLexems() const { return m_aLexemList; }

public:

	inline void SkipSpaces();

	bool IsByte(char c);
	char GetByte();

	bool IsWord();
	wxString GetWord(bool bOrigin = false, bool bGetPoint = false, wxString *psOrig = NULL);

	bool IsNumber();
	wxString GetNumber();

	bool IsString();
	wxString GetString();

	bool IsDate();
	wxString GetDate();

	bool IsEnd();

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	static long long IsKeyWord(const wxString &sKeyWord);
#else
	static int IsKeyWord(const wxString &sKeyWord);
#endif

	wxString GetStrToEndLine();
	void PrepareFromCurrent(int nMode, const wxString &sName = wxEmptyString);
	wxString GetModuleName() { return m_sModuleName; }

	unsigned int GetCurrentPos() { return m_nCurPos; }
	unsigned int GetCurrentLine() { return m_nCurLine; }

public:

	static std::map<wxString, void *> m_aHashKeyWords;
	static void LoadKeyWords();

protected:

	std::vector<CTranslateModule *> m_aTranslateModules; 

	//методы и переменные для парсинга текста
	//Поддержка "дефайнов":
	CDefList *m_aDefList;
	bool m_bAutoDeleteDefList;
	int m_nModePreparing;

	//атрибуты:
	wxString m_sModuleName;//имя компилируемого модуля (для вывода информации при ошибках)
	wxString m_sDocPath; // уникальный путь к документу 
	wxString m_sFileName; // путь к файлу (если внешняя обработка) 

	unsigned int m_nSizeText;//размер исходного текста

	//исходный текст:
	wxString m_sBuffer;
	wxString m_sBUFFER;//буфер - исходный код в верхнем регистре

	unsigned int m_nCurPos;//текущая позиция обрабатываемого текста
	unsigned int m_nCurLine;

	//промежуточный массив с лексемами:
	std::vector<CLexem> m_aLexemList;
};

#endif