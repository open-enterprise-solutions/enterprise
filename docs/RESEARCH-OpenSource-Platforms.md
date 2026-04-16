# Исследование open-source реализаций 1С для OES Enterprise

> **Дата:** 2026-04-14  
> **Цель:** Архитектурный референс для OES Enterprise — что можно подсмотреть и адаптировать  
> **Статус:** Только для изучения, код не копируем

---

## Часть 1: OneScript (C# реализация языка 1С)

**Репозиторий:** `github.com/EvilBeaver/OneScript`  
**Язык:** C# / .NET  
**Лицензия:** MPL-2.0  
**~3000 звёзд, активно поддерживается с 2013 года**

### 1.1 Компилятор / Парсер

**Архитектура:** Recursive descent парсер → полный AST → Visitor pattern → bytecode

**Pipeline компиляции:**
```
Текст скрипта
    → Lexer (токены по требованию)
    → Parser (строит полный AST)
    → SemanticAnalyzer (проверка типов, разрешение имён)
    → CodeGenerator : IAstVisitor (обход AST → emit bytecode)
    → ModuleImage (скомпилированный модуль)
```

**Ключевые файлы:**
- `src/ScriptEngine/Compiler/Parser.cs` — рекурсивный нисходящий парсер
- `src/ScriptEngine/Compiler/Lexer.cs` — токенизатор (~80 типов токенов)
- `src/ScriptEngine/Compiler/Ast/` — узлы AST
- `src/ScriptEngine/Compiler/LanguageDef.cs` — таблица ключевых слов RU/EN

**AST-узлы:**
```
NonTerminalNode : AstNode
  ├── ModuleNode
  ├── MethodNode (процедура/функция)
  ├── IfOperatorNode
  ├── WhileLoopNode
  ├── ForLoopNode / ForEachLoopNode
  ├── TryExceptNode
  ├── AssignmentNode
  └── ...
TerminalNode : AstNode (листья — литералы, идентификаторы)
```

**Двухпроходная компиляция:** первый проход собирает сигнатуры всех методов (forward calls), второй — генерирует bytecode тел. Аналогично нашему deferred-call-resolution в `ibCompileCode`.

> **Ключевое отличие от OES:** OneScript строит полноценный AST перед кодогенерацией. Наш `ibCompileCode` — single-pass, AST не строится явно. Это ограничивает статический анализ, оптимизации, форматтер кода и AI code completion.

### 1.2 Система типов

**`IValue` интерфейс** — аналог нашего `ibValue`:
```csharp
public interface IValue {
    DataType DataType { get; }        // тип значения
    TypeDescriptor SystemType { get; } // дескриптор типа
    
    decimal AsNumber();
    DateTime AsDate();
    string AsString();
    bool AsBoolean();
    IRuntimeContextInstance AsObject();
    IValue GetRawValue();
}
```

**`DataType` enum** — аналог наших `TYPE_*`:
| OneScript | OES | Описание |
|-----------|-----|----------|
| `Undefined` | `TYPE_UNDEFINED` | Неопределено |
| `Boolean` | `TYPE_BOOLEAN` | Булево |
| `Number` | `TYPE_NUMBER` | Число |
| `Date` | `TYPE_DATE` | Дата |
| `String` | `TYPE_STRING` | Строка |
| `Object` | `TYPE_REFFER` | Ссылка на объект |
| `Type` | — | Тип как значение (у нас нет) |

**Реализации:**
- `NumberValue` — обёртка над **`decimal`** (не `double`!). Точная арифметика для финансов
- `BooleanValue` — кешированные true/false синглтоны
- `UndefinedValue` — singleton
- `StringValue`, `DateValue`

> **ПРОВЕРЕНО (2026-04-14):** `ibValue` хранит числа как `ttmath::Big<128,128>` — 128-bit произвольная точность (`number.h:20`). Это НЕ `double`. Финансовая точность обеспечена, gap отсутствует.

### 1.3 Runtime / VM

**`MachineInstance`** — стек-машина, ~90 опкодов (у нас 66).

**`OperationCode` enum (ключевые):**
```
PushVar, PushConst, PushUndefined, PushTmp
PopVar, PopTmp
Add, Sub, Mul, Div, Mod, Pow, Neg
Equals, NotEquals, Less, Greater, ...
Jmp, JmpFalse, JmpTrue
CallFunc, CallProc, ArgNum, PushDefaultArg
Return, ReturnValue
BeginTry, EndTry, RaiseException
CreateObject
PushIndexed, ResolveProperty
```

**Отличие от OES:** нет `TYPE_DELTA*` offset trick — арифметические опкоды единые, тип проверяется в runtime через `IValue.DataType`.

**`ModuleImage`** — скомпилированный модуль (аналог `ibByteCode`):
- `Code[]` — массив инструкций (`Command`: opCode + argument)
- `Constants[]` — пул констант
- `MethodRefs[]` — таблица методов
- `VariableRefs[]` — переменные модуля

**Стек вызовов — `ExecutionFrame`:**
```csharp
class ExecutionFrame {
    IValue[]     Locals;             // локальные переменные
    int          InstructionPointer;
    ModuleImage  Image;
    IRunnable    Module;
    string       MethodName;         // для stack trace
}
```

> **ПРОВЕРЕНО (2026-04-14):** `ibByteCode` не имеет Serialize/Save/Load методов — только in-memory. `ibValue` имеет DoSerialize/DoDeserialize для отдельных значений. Bytecode cache — gap (низкий приоритет).

### 1.4 Встроенные функции

**Паттерн: атрибутная регистрация** (у нас — ручная в `ibSystemManager`):

```csharp
[ContextMethod("Сообщить", "Message")]
public static void Message(string text, MessageStatusEnum status) { ... }

[ContextMethod("СтрДлина", "StrLen")]
public IValue StrLen(IValue str) { ... }
```

При старте VM через reflection сканирует все `[ContextMethod]` и строит dispatch-таблицу. Dispatch по индексу (O(1)), не по имени.

**Классы контекстов (~120 функций):**
- `SystemGlobalContext` — `ТекущаяДата()`, `Строка()`, `Число()`, `Формат()`
- `StringOperations` — строковые
- `MathOperations` — математика
- `FileOperations` — файлы
- `XMLWriter/XMLReader` — XML

> **Идея для OES:** перевести `ibSystemManager` на макросы-саморегистраторы:
> ```cpp
> #define DECLARE_BUILTIN(nameRu, nameEn, func) \
>     static BuiltinRegistrar __reg_##func(nameRu, nameEn, &func)
> 
> // Каждый файл регистрирует свои функции
> // builtins/string.cpp:
> DECLARE_BUILTIN(L"СтрДлина", L"StrLen", BuiltinStrLen);
> ```

### 1.5 Расширяемость (плагины)

**Любой .NET-класс** с атрибутом `[ContextClass]` становится типом в скрипте:
```csharp
[ContextClass("МойТип", "MyType")]
public class MyType : AutoContext<MyType> {
    [ContextMethod("Метод", "Method")]
    public IValue DoSomething(IValue arg) { ... }
    
    [ContextProperty("Свойство", "Property")]
    public IValue MyProp { get; set; }
}

// Подключение:
engine.AttachExternalAssembly(Assembly.LoadFile("plugin.dll"));
```

> **Идея для OES:** `AutoContext<T>` через CRTP в C++:
> ```cpp
> template<typename Derived>
> class AutoContext : public ibRuntimeContextInstance {
>     // CRTP генерирует dispatch-таблицу из статических метаданных
> };
> ```

### 1.6 Встроенные объекты

| Тип | Класс | Есть в OES? |
|-----|-------|-------------|
| Массив | `ArrayImpl` | ? |
| Структура | `StructureImpl` | ? |
| Соответствие | `MapImpl` | ? |
| СписокЗначений | `ValueListImpl` | ? |
| **ТаблицаЗначений** | `ValueTableImpl` | **НЕТ — критический gap** |
| РезультатЗапроса | `QueryResultImpl` | Через БД |

> **ПРОВЕРЕНО (2026-04-14):** `ibValueModelTable` существует в OES (`valueTable.h`, CLSID `VL_TABL`). Поддерживает AddRow, Clone, Count, Find, Delete, Clear, Sort. Gap отсутствует.

---

## Часть 2: BSL Language Server и экосистема 1c-syntax

### 2.1 bsl-parser (ANTLR4 грамматика языка 1С)

**Репозиторий:** `github.com/1c-syntax/bsl-parser`  
**Язык:** Java  
**Технология:** ANTLR4

Это **формальная грамматика** языка BSL (Built-in Scripting Language) 1С:Предприятие.

**Ключевые файлы:**
- `src/main/antlr/BSLParser.g4` — основная грамматика парсера
- `src/main/antlr/BSLLexer.g4` — лексер
- `src/main/antlr/SDBLParser.g4` — грамматика языка запросов 1С (SDBL)
- `src/main/antlr/SDBLLexer.g4` — лексер запросов

**Структура грамматики BSL (упрощённо):**
```antlr
file : moduleVarSection? moduleBodySection? EOF;

moduleVarSection : (moduleVar ';')*;
moduleVar : (VAR_KEYWORD | ПЕРЕМ) varDescriptor (',' varDescriptor)* EXPORT_KEYWORD?;

moduleBodySection : codeBlock;

sub : procedure | function;
procedure : PROCEDURE_KEYWORD subName '(' params? ')' EXPORT? codeBlock ENDPROCEDURE;
function  : FUNCTION_KEYWORD subName '(' params? ')' EXPORT? codeBlock ENDFUNCTION;

statement : assignment | callStatement | ifStatement | whileStatement 
          | forStatement | forEachStatement | tryStatement 
          | returnStatement | continueStatement | breakStatement
          | raiseStatement | executeStatement | addHandlerStatement
          | removeHandlerStatement | gotoStatement | labelStatement;

expression : member (operation member)*;
member : unaryModifier? (complexIdentifier | constValue | '(' expression ')');
```

**Двуязычность в лексере:**
```antlr
IF_KEYWORD: 'If' | 'Если';
THEN_KEYWORD: 'Then' | 'Тогда';
PROCEDURE_KEYWORD: 'Procedure' | 'Процедура';
FUNCTION_KEYWORD: 'Function' | 'Функция';
EXPORT_KEYWORD: 'Export' | 'Экспорт';
VAR_KEYWORD: 'Var' | 'Перем';
// ... ~44 пары ключевых слов
```

**Препроцессор:**
```antlr
preprocessor : '#' (IF | ELSIF | ELSE | ENDIF | REGION | ENDREGION | USE);
// Условная компиляция:
// #Если Сервер Тогда ... #КонецЕсли
// #Область Инициализация ... #КонецОбласти
```

**Аннотации (с платформы 8.3.22+):**
```antlr
annotation : '&' annotationName ('(' annotationParams? ')')?;
// &НаКлиенте, &НаСервере, &НаСервереБезКонтекста
// &AtClient, &AtServer, &AtServerNoContext
```

> **Ценность для OES:** Эта ANTLR-грамматика — наиболее полное формальное описание синтаксиса 1С. Можно использовать для верификации совместимости нашего парсера с 1С-синтаксисом.

### 2.2 bsl-language-server (LSP для 1С)

**Репозиторий:** `github.com/1c-syntax/bsl-language-server`  
**Язык:** Java  
**~1200 звёзд**

Language Server Protocol реализация для BSL. Даёт: автодополнение, диагностики, навигация, рефакторинг.

**Архитектура:**
```
BSL-исходник → bsl-parser (ANTLR4 → AST) → Символьная таблица → Диагностики
                                           → Провайдеры LSP (completion, hover, etc.)
```

**Ключевые компоненты:**

**Символьная таблица** (`src/main/java/com/github/_1c_syntax/bsl/languageserver/context/`):
- `DocumentContext` — контекст одного модуля
- `ServerContext` — глобальный контекст (все модули)
- `SymbolTree` — дерево символов (переменные, методы, области)

**Диагностики** (`src/main/java/com/github/_1c_syntax/bsl/languageserver/diagnostics/`):
~200 правил проверки кода. Примеры:
- `EmptyCodeBlock` — пустой блок кода
- `MissingTempStorageCleanup` — утечка временного хранилища
- `CognitiveComplexity` — сложность методов
- `DeprecatedMethods` — устаревшие функции
- `UsingServiceTag` — использование служебных тегов
- `MagicNumber` — магические числа
- `MethodTooLong` — слишком длинный метод

**Каждая диагностика** — отдельный класс с аннотацией:
```java
@DiagnosticMetadata(
    type = DiagnosticType.CODE_SMELL,
    severity = DiagnosticSeverity.MINOR,
    minutesToFix = 1,
    tags = { DiagnosticTag.STANDARD }
)
public class EmptyCodeBlockDiagnostic extends AbstractVisitorDiagnostic {
    @Override
    public ParseTree visitCodeBlock(BSLParser.CodeBlockContext ctx) {
        if (ctx.statement().isEmpty()) {
            diagnosticStorage.addDiagnostic(ctx);
        }
        return super.visitCodeBlock(ctx);
    }
}
```

> **Идея для OES:** Когда будем делать web-designer (Monaco Editor), можно реализовать BSL Language Server Protocol для OES-скриптов. 200 готовых правил — референс для наших диагностик.

### 2.3 mdclasses (модель метаданных 1С)

**Репозиторий:** `github.com/1c-syntax/mdclasses`  
**Язык:** Java  
**Назначение:** Парсинг и представление метаданных конфигурации 1С в Java-объектах

**Иерархия метаданных:**

```java
// Корень
Configuration
  ├── CommonModules[]           // Общие модули
  ├── SessionParameters[]       // Параметры сессии
  ├── Roles[]                   // Роли
  ├── CommonAttributes[]        // Общие реквизиты
  ├── ExchangePlans[]           // Планы обмена
  ├── Constants[]               // Константы
  ├── Catalogs[]                // Справочники
  ├── Documents[]               // Документы
  ├── DocumentNumerators[]      // Нумераторы документов
  ├── Sequences[]               // Последовательности
  ├── ChartsOfCharacteristicTypes[]  // ПВХ
  ├── ChartsOfAccounts[]        // Планы счетов
  ├── ChartsOfCalculationTypes[]// Планы видов расчёта
  ├── InformationRegisters[]    // Регистры сведений
  ├── AccumulationRegisters[]   // Регистры накопления
  ├── AccountingRegisters[]     // Регистры бухгалтерии
  ├── CalculationRegisters[]    // Регистры расчёта
  ├── BusinessProcesses[]       // Бизнес-процессы
  ├── Tasks[]                   // Задачи
  ├── DataProcessors[]          // Обработки
  ├── Reports[]                 // Отчёты
  ├── Enums[]                   // Перечисления
  ├── WebServices[]             // Веб-сервисы
  ├── HTTPServices[]            // HTTP-сервисы
  ├── CommonForms[]             // Общие формы
  ├── CommonCommands[]          // Общие команды
  ├── CommonTemplates[]         // Общие макеты
  └── Subsystems[]              // Подсистемы
```

**Структура объекта метаданных (пример — Catalog):**
```java
public class Catalog implements MDObject {
    String name;                    // Имя
    String synonym;                 // Синоним (отображаемое имя)
    String comment;                 // Комментарий
    ObjectBelonging objectBelonging;// Принадлежность объекта
    
    // Реквизиты
    List<Attribute> attributes;
    // Табличные части
    List<TabularSection> tabularSections;
    // Формы
    List<Form> forms;
    // Макеты
    List<Template> templates;
    // Команды
    List<Command> commands;
    
    // Модули
    ModuleType objectModule;
    ModuleType managerModule;
    
    // Специфика справочника
    boolean hierarchical;
    HierarchyType hierarchyType;    // Hierarchy of groups / elements
    int codeLength;
    int descriptionLength;
    CodeType codeType;              // String / Number
    boolean autonumbering;
    
    // Владельцы
    List<MDObjectReference> owners;
    
    // Формы по умолчанию
    MDObjectReference defaultObjectForm;
    MDObjectReference defaultListForm;
    MDObjectReference defaultChoiceForm;
    
    // Предопределённые значения
    List<PredefinedItem> predefined;
}
```

**Attribute (Реквизит):**
```java
public class Attribute {
    String name;
    String synonym;
    String comment;
    TypeDescription type;  // ← описание типа
    boolean indexing;
    boolean fillChecking;
    UseMode useMode;
}
```

**TypeDescription (ОписаниеТипов) — композитный тип:**
```java
public class TypeDescription {
    List<TypeItem> types;  // Один или несколько типов
    
    // Квалификаторы для примитивов
    NumberQualifiers numberQualifiers;    // длина, точность
    StringQualifiers stringQualifiers;    // длина, допустимая длина
    DateQualifiers dateQualifiers;        // состав даты (дата/время/дата+время)
}

public class TypeItem {
    String typeName;  // "String", "Number", "Boolean", "Date",
                      // "CatalogRef.Контрагенты", 
                      // "DocumentRef.РеализацияТоваров",
                      // "EnumRef.ТипыЦен"
}
```

> **Прямой маппинг на OES:** Наши 11 типов метаобъектов — подмножество этой структуры. Важно: у 1С есть ещё `BusinessProcesses`, `Tasks`, `CalculationRegisters`, `ChartsOfCalculationTypes`, `Sequences`, `DocumentNumerators`, `WebServices`, `HTTPServices` — которых у нас нет.

### 2.4 Формат хранения конфигурации на диске

**Формат EDT (выгрузка в файлы):**
```
Configuration/
├── Configuration.mdo           # XML — корневой объект конфигурации
├── Catalogs/
│   └── Контрагенты/
│       ├── Контрагенты.mdo     # XML — описание справочника
│       ├── ObjectModule.bsl    # Модуль объекта
│       ├── ManagerModule.bsl   # Модуль менеджера
│       ├── Forms/
│       │   └── ФормаЭлемента/
│       │       ├── Module.bsl  # Модуль формы
│       │       └── Form.form   # XML — описание формы
│       └── Templates/
├── Documents/
│   └── РеализацияТоваров/
│       └── ...
├── InformationRegisters/
├── AccumulationRegisters/
└── ...
```

**Пример .mdo файла (Catalog):**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<mdclass:Catalog xmlns:mdclass="http://g5.1c.ru/v8/dt/metadata/mdclass"
                 uuid="a1b2c3d4-e5f6-..."
                 name="Контрагенты">
  <synonym>
    <v8:item lang="ru">Контрагенты</v8:item>
  </synonym>
  <useStandardCommands>true</useStandardCommands>
  <codeLength>9</codeLength>
  <descriptionLength>150</descriptionLength>
  <codeType>String</codeType>
  <hierarchical>true</hierarchical>
  <hierarchyType>HierarchyOfItems</hierarchyType>
  
  <attributes uuid="...">
    <name>ИНН</name>
    <type>
      <types>String</types>
      <stringQualifiers><length>12</length></stringQualifiers>
    </type>
  </attributes>
  
  <tabularSections uuid="...">
    <name>КонтактнаяИнформация</name>
    <attributes uuid="...">
      <name>Тип</name>
      <type>
        <types>EnumRef.ТипыКонтактнойИнформации</types>
      </type>
    </attributes>
  </tabularSections>
  
  <forms uuid="...">ФормаЭлемента</forms>
  <forms uuid="...">ФормаСписка</forms>
</mdclass:Catalog>
```

> **Для OES:** Наш XML/JSON экспорт (`metadataConfigurationXML.cpp`, `metadataConfigurationJSON.cpp`) выполняет ту же роль. Стоит сравнить структуры для обеспечения совместимости при импорте 1С-конфигураций.

### 2.5 Модель форм

**Управляемая форма 1С** (managed form) — декларативное описание:

```xml
<Form>
  <title>Контрагент</title>
  <attributes>
    <name>Object</name>
    <type>CatalogObject.Контрагенты</type>
    <mainAttribute>true</mainAttribute>
  </attributes>
  
  <items>
    <FormGroup type="UsualGroup" name="ГруппаШапка">
      <representation>WeakSeparation</representation>
      <items>
        <FormField name="Наименование" dataPath="Object.Description" 
                   type="InputField" />
        <FormField name="ИНН" dataPath="Object.ИНН" 
                   type="InputField" />
      </items>
    </FormGroup>
    
    <FormTable name="КонтактнаяИнформация" 
               dataPath="Object.КонтактнаяИнформация">
      <columns>
        <FormField name="Тип" dataPath="КонтактнаяИнформация.Тип" />
        <FormField name="Значение" dataPath="КонтактнаяИнформация.Значение" />
      </columns>
    </FormTable>
    
    <FormGroup type="CommandBar" name="ОсновнаяКоманднаяПанель">
      <items>
        <FormButton name="ЗаписатьИЗакрыть" commandName="WriteAndClose" />
        <FormButton name="Записать" commandName="Write" />
      </items>
    </FormGroup>
  </items>
</Form>
```

**Типы элементов формы:**
| Тип | Назначение | Аналог в Formily |
|-----|-----------|------------------|
| `InputField` | Поле ввода | `Input`, `DatePicker`, `NumberPicker` |
| `CheckBoxField` | Флажок | `Checkbox` |
| `RadioButtonField` | Переключатель | `Radio` |
| `TextDocumentField` | Текстовый редактор | `Textarea` |
| `SpreadsheetDocumentField` | Табличный документ | AG Grid? |
| `FormTable` | Таблица (табличная часть) | AG Grid |
| `FormGroup` | Группа элементов | `FormLayout` |
| `CommandBar` | Командная панель | Toolbar |
| `Button` | Кнопка | `Button` |
| `Decoration` | Декорация (надпись/картинка) | `Label`, `Image` |

> **Для OES web client:** Наш Formily JSON Schema рендерер должен уметь преобразовывать эту модель форм в Formily-схему. Это ключевой маппинг для миграции конфигураций с 1С.

---

## Часть 3: Сводная таблица — что взять для OES

### Критические инсайты (с верификацией 2026-04-14)

| # | Инсайт | Приоритет | Статус верификации |
|---|--------|-----------|-------------------|
| 1 | ~~decimal арифметика~~ | ~~КРИТИЧЕСКИЙ~~ | ✅ **НЕТ GAP** — `ttmath::Big<128,128>`, 128-bit точность |
| 2 | **Полный AST** — без AST невозможны: форматтер, статанализ, AI code completion | СТРАТЕГИЧЕСКИЙ | ⚠️ Подтверждено — single-pass, нет AST |
| 3 | ~~ТаблицаЗначений~~ | ~~ВЫСОКИЙ~~ | ✅ **НЕТ GAP** — `ibValueModelTable` существует (CLSID `VL_TABL`) |
| 4 | **Атрибутная регистрация функций** — макросы вместо ручного `ibSystemManager` | СРЕДНИЙ | ⚠️ Подтверждено — 92 функции, enum + switch |
| 5 | **Сериализация bytecode** — кеш компиляции для быстрого старта | СРЕДНИЙ | ⚠️ Подтверждено — ibByteCode in-memory only |
| 6 | **ANTLR4 грамматика BSL** — формальная спецификация для верификации совместимости | СПРАВОЧНЫЙ | Без изменений |
| 7 | **200 диагностик** — референс для нашего линтера/LSP | СПРАВОЧНЫЙ | Без изменений |
| 8 | **mdclasses структура** — полная карта метаданных 1С для gap-анализа | СПРАВОЧНЫЙ | Без изменений |
| 9 | **Маппинг форм → Formily** — ключ к миграции конфигураций | ВЫСОКИЙ | ⚠️ Phase 3 web client — в работе |
| 10 | **Plugin API** через `AutoContext<T>` / CRTP | СРЕДНИЙ | Без изменений |
| 11 | **Русские ключевые слова** — нет RU алиасов (Если/Процедура) | ВЫСОКИЙ | ❌ Только English в `translateCode.cpp` |

### Сравнение: OneScript vs OES Enterprise

| Аспект | OneScript (C#) | OES Enterprise (C++) | Кто лучше |
|--------|---------------|---------------------|-----------|
| Парсер | Recursive descent → полный AST | Single-pass, нет AST | OneScript |
| Кодогенерация | Visitor по AST | Inline при парсинге | OneScript |
| VM | Стек-машина, ~90 опкодов | Стек-машина, 66 опкодов | Паритет |
| Тип значения | `IValue` интерфейс | `ibValue` tagged union | OES (быстрее) |
| Числа | `decimal` (точная) | `ttmath::Big<128,128>` (128-bit) | **Паритет** ✅ |
| ТаблицаЗначений | `ValueTableImpl` | `ibValueModelTable` (VL_TABL) | **Паритет** ✅ |
| Составные типы | `TypeDescription` | `ibTypeDescription` (vector + qualifiers) | **Паритет** ✅ |
| Регистрация функций | Атрибуты + reflection | Ручная, 92 функции, switch | OneScript |
| Расширяемость | Plugin через .NET сборки | `simplePlugin.dll` | OneScript |
| Метаданные бизнес-объектов | Нет в ядре | 11 полноценных типов | **OES** |
| База данных | Нет абстракции | `ibDatabaseLayer` (5 драйверов) | **OES** |
| Bytecode cache | Да (ModuleImage → файл) | Нет (in-memory) | OneScript |
| Двуязычность RU/EN | Да (44 пары) | Только English | OneScript |
| Desktop GUI | Нет | wxWidgets (22 контрола) | **OES** |
| Web GUI | Через ASP.NET | React SPA (Refine + shadcn + Formily) | **OES** ✅ |
| HTTP server | Нет | cpp-httplib (встроен в daemon) | **OES** ✅ |
| REST API | Нет | JWT auth + CRUD + metadata endpoints | **OES** ✅ |

### Типы метаданных: 1С vs OES

| Тип 1С | Есть в OES | Статус |
|--------|-----------|--------|
| Справочник (Catalog) | ✅ | Реализован |
| Документ (Document) | ✅ | Реализован |
| Перечисление (Enum) | ✅ | Реализован |
| Константа (Constant) | ✅ | Реализован |
| Регистр сведений (InformationRegister) | ✅ | Реализован |
| Регистр накопления (AccumulationRegister) | ✅ | Реализован |
| Обработка (DataProcessor) | ✅ | Реализован |
| Отчёт (Report) | ✅ | Реализован |
| ПВХ (ChartOfCharacteristicTypes) | ✅ | Реализован |
| План счетов (ChartOfAccounts) | ✅ | Реализован |
| Регистр бухгалтерии (AccountingRegister) | ✅ | Реализован |
| Бизнес-процесс (BusinessProcess) | ❌ | Не реализован |
| Задача (Task) | ❌ | Не реализован |
| Регистр расчёта (CalculationRegister) | ❌ | Не реализован |
| План видов расчёта | ❌ | Не реализован |
| Нумератор документов | ❌ | Не реализован |
| Последовательность (Sequence) | ❌ | Не реализован |
| План обмена (ExchangePlan) | ❌ | Не реализован |
| Веб-сервис | ❌ | Не реализован |
| HTTP-сервис | ❌ | Не реализован |

---

## Часть 4: Конкретный план действий

### Выполнено (верифицировано 2026-04-14)

1. ~~Проверить тип числа в `ibValue`~~ — ✅ `ttmath::Big<128,128>`, gap нет
2. ~~Проверить `ibValueTable`~~ — ✅ `ibValueModelTable` существует, gap нет
3. ~~Проверить `ibTypeDescription`~~ — ✅ Композитные типы + квалификаторы, gap нет
4. ~~Web-клиент~~ — ✅ Phase 0-2 реализованы: cpp-httplib + React SPA + JWT auth + REST API + metadata endpoints

### Сейчас в работе

5. **Web client Phase 3** — Form Serializer (metadata → Formily JSON Schema)
6. **Web client Phase 4-9** — SPA layout, reports, embedding, designer, debugger, AI

### Среднесрочно

7. **Русские ключевые слова** — добавить 44 RU алиаса в `translateCode.cpp` (Если/Процедура/Функция...)
8. **Макросы саморегистрации** для `ibSystemManager` — 92 функции через switch → макросы
9. **Недостающие встроенные функции** — добавить ~25 функций для совместимости с 1С:
   - Математика (8): Sin, Cos, Tan, ASin, ACos, ATan, Exp, Pow
   - Строки (2): StrSplit, StrConcat
   - Локализация (1): NStr (локализованные строки)
   - XML (3): XMLWriter, XMLReader, XMLString
   - JSON (2): ReadJSON, WriteJSON
   - Типы (2): TypeDescription, FillPropertyValues
   - Транзакции (1): TransactionActive
   - Файлы (1): FindFiles
   - Прочее (5): Sleep, TempStorageAddress, PutToTempStorage, GetFromTempStorage, DeleteFromTempStorage
10. **Маппинг 1С-форм → Formily JSON Schema** — для импорта конфигураций

### Стратегически (v2.0)

11. **Полноценный AST** в компиляторе — открывает: LSP, форматтер, статанализ, AI
12. **Сериализация `ibByteCode`** — кеш компиляции
13. **Plugin API** через CRTP `AutoContext<T>`
14. **Бизнес-процессы, Задачи** — новые типы метаданных
15. **&НаКлиенте/&НаСервере** — разделение контекста выполнения для клиент-серверной архитектуры

---

## Ссылки на репозитории

| Проект | URL | Лицензия | Что смотреть |
|--------|-----|----------|-------------|
| OneScript | github.com/EvilBeaver/OneScript | MPL-2.0 | Компилятор, VM, типы |
| bsl-parser | github.com/1c-syntax/bsl-parser | LGPL-3.0 | ANTLR4 грамматика BSL |
| bsl-language-server | github.com/1c-syntax/bsl-language-server | LGPL-3.0 | LSP, диагностики |
| mdclasses | github.com/1c-syntax/mdclasses | LGPL-3.0 | Модель метаданных |
| Frappe Framework | github.com/frappe/frappe | MIT | DocType = метаобъект |
| iDempiere | github.com/idempiere/idempiere | GPL-2.0 | Application Dictionary, план счетов |
| GnuCash | github.com/Gnucash/gnucash | GPL-2.0 | Бухгалтерия на C++ |
| Wren | github.com/wren-lang/wren | MIT | Bytecode VM на C |
| MdInternals | github.com/elisy/MdInternals | GPL | Декомпилятор .cf бинарного формата |

---

## Часть 5: 1C:EDT — внутренняя архитектура (из исследования EDT API)

### 5.1 Стек технологий EDT

1C:EDT построен на **Eclipse Platform + EMF (Eclipse Modeling Framework)**. Каждый тип метаданных — `EClass` в Ecore-модели. Все объекты наследуют `EObject`.

**Корневые пакеты EDT API:**
| Пакет | Назначение |
|-------|-----------|
| `com._1c.g5.v8.dt.metadata.mdclass` | Интерфейсы всех типов метаданных |
| `com._1c.g5.v8.dt.metadata.mdtype` | Система типов (TypeDescription, квалификаторы) |
| `com._1c.g5.v8.dt.metadata.dbview` | Модель представлений БД |
| `com._1c.g5.v8.dt.bsl.model` | BSL языковая модель (Module, ModuleType) |
| `com._1c.g5.v8.bm.core` | Business Model core (объектная БД EDT) |

### 5.2 Полный список типов метаданных EDT

**Объекты данных:**
`Catalog`, `Document`, `InformationRegister`, `AccumulationRegister`, `AccountingRegister`, `CalculationRegister`, `ChartOfAccounts`, `ChartOfCharacteristicTypes`, `ChartOfCalculationTypes`, `Enumeration`, `Constant`, `ExchangePlan`, `Sequence`

**Бизнес-логика:**
`BusinessProcess`, `Task`

**Отчёты и обработки:**
`Report`, `DataProcessor`

**UI:**
`CommonForm`, `CommonTemplate`, `Style`, `PaletteColor`

**Код:**
`CommonModule`, `Role`, `ScheduledJob`, `EventSubscription`

**Интеграция:**
`HTTPService`, `WebService`, `WebSocketClient`, `IntegrationService`, `XDTOPackage`

**Прочее:**
`DefinedType`, `Language`, `Interface`

### 5.3 Детальная структура Catalog в mdclasses

```java
@Value @Builder
public class Catalog implements ReferenceObject, AccessRightsOwner {
    UUID uuid;
    String name;
    MdoReference mdoReference;
    ObjectBelonging objectBelonging;
    MultiLanguageString synonym;
    String comment;

    // Дочерние объекты
    List<ObjectCommand>    commands;
    List<Attribute>        attributes;      // реквизиты
    List<TabularSection>   tabularSections; // табличные части
    List<ObjectForm>       forms;
    List<ObjectTemplate>   templates;
    List<ObjectModule>     modules;         // модули (lazy)

    // Специфика справочника
    List<MdoReference>     owners;          // владельцы
    boolean                hierarchical;
    HierarchyType          hierarchyType;   // Groups / Items
    int                    codeLength;
    int                    descriptionLength;
    CodeType               codeType;        // String / Number
    CodeSeries             codeSeries;      // WHOLE_CATALOG / BY_OWNER
    boolean                checkUnique;
    boolean                autonumbering;
}
```

### 5.4 Дочерние элементы метаданных

| Класс | Назначение | Аналог в OES |
|-------|-----------|-------------|
| `ObjectAttribute` | Реквизит объекта | Есть |
| `StandardAttribute` | Стандартный реквизит (Код, Наименование) | Есть |
| `ObjectTabularSection` | Табличная часть | Есть |
| `ObjectForm` | Форма объекта | Есть |
| `ObjectModule` | Модуль объекта | Есть |
| `ObjectCommand` | Команда объекта | **Нет в OES** |
| `Dimension` | Измерение регистра | Есть |
| `Resource` | Ресурс регистра | Есть |
| `Recalculation` | Перерасчёт | **Нет** |
| `AccountingFlag` | Признак учёта | **Нет** |
| `ExtDimensionAccountingFlag` | Признак учёта субконто | **Нет** |
| `EnumValue` | Значение перечисления | Есть |

### 5.5 TypeDescription — составной тип (критический gap OES)

В 1С тип реквизита — это **объект** `TypeDescription`, а не просто enum:

```java
TypeDescription {
    List<TypeItem> types;              // НЕСКОЛЬКО допустимых типов одновременно
    NumberQualifiers numberQualifiers;  // длина, точность, знак
    StringQualifiers stringQualifiers;  // длина, фиксированная/переменная
    DateQualifiers dateQualifiers;      // дата/время/дата+время
}
```

**Пример:** реквизит "Контрагент" может принимать типы `CatalogRef.Организации` ИЛИ `CatalogRef.Контрагенты` — это один `TypeDescription` с двумя элементами в `types[]`.

**XML-представление:**
```xml
<type>
    <types>CatalogRef.Контрагенты</types>
    <types>CatalogRef.Организации</types>
</type>
```

> **Для OES:** Нужна структура `ibTypeDescription`:
> ```cpp
> struct ibTypeDescription {
>     std::vector<ibTypeID>                  types;
>     std::optional<ibNumberQualifiers>      numberQ;  // digits, fractionDigits, sign
>     std::optional<ibStringQualifiers>      stringQ;  // length, allowedLength
>     std::optional<ibDateQualifiers>        dateQ;    // dateFractions
> };
> ```

### 5.6 Автоматически порождаемые типы

При создании справочника `Warehouses` платформа 1С автоматически порождает набор типов:
| Тип | Назначение |
|-----|-----------|
| `CatalogRef.Warehouses` | Ссылка |
| `CatalogObject.Warehouses` | Объект (для записи) |
| `CatalogManager.Warehouses` | Менеджер (статические методы) |
| `CatalogSelection.Warehouses` | Выборка |
| `CatalogList.Warehouses` | Динамический список |

Аналогично для Document: `DocumentRef.*`, `DocumentObject.*`, `DocumentManager.*`, `DocumentSelection.*`

### 5.7 Модель управляемой формы (детально)

**`ManagedFormData`** — корень формы:
```java
ManagedFormData {
    title      : MultiLanguageString
    attributes : List<FormAttribute>   // данные формы
    items      : List<FormItem>        // иерархия UI-элементов
    handlers   : List<FormHandler>     // обработчики событий
}
```

**Полный список `FormElementType`:**

*Поля ввода:*
`InputField`, `CheckBoxField`, `RadioButtonField`

*Документные поля:*
`TextDocumentField`, `FormattedDocumentField`, `HTMLDocumentField`, `PDFDocumentField`, `SpreadsheetDocumentField`

*Специализированные:*
`CalendarField`, `PeriodField`, `PictureField`, `ProgressBarField`, `TrackBarField`, `ChartField`, `GanttChartField`, `GraphicalSchemaField`, `GeographicalSchemaField`, `PlannerField`

*Группировки:*
`FormGroup`, `UsualGroup`, `ColumnGroup`, `ButtonGroup`, `Pages`, `Page`

*Кнопки:*
`Button`, `UsualButton`, `CommandBarButton`, `CommandBarHyperlink`

*Таблицы:*
`Table`

*Декорации:*
`Decoration`, `LabelDecoration`, `PictureDecoration`

*Панели:*
`CommandBar`, `Popup`, `SearchControlAddition`

### 5.8 Типы модулей BSL

| Тип модуля | Где | Особенности |
|-----------|-----|-------------|
| `ManagedApplicationModule` | Конфигурация | Старт тонкого/веб-клиента |
| `CommonModule` | Общие модули | Server/Client/Both |
| `ObjectModule` | Справочники, Документы | BeforeWrite, OnWrite и т.д. |
| `ManagerModule` | Все объекты | Статические методы без экземпляра |
| `FormModule` | Каждая форма | &AtClient/&AtServer |
| `RecordSetModule` | Регистры | BeforeWrite для набора записей |
| `CommandModule` | Каждая команда | `CommandProcessing()` |
| `SessionModule` | Конфигурация | `SetSessionParameters()` при старте |

### 5.9 Компиляторные директивы (критический gap OES)

```
&AtClient                        — выполнение на клиенте
&AtServer                        — на сервере
&AtServerNoContext               — на сервере без контекста формы
&AtClientAtServer                — и на клиенте, и на сервере
&AtClientAtServerNoContext       — оба без контекста
```

**Грамматика:**
```antlr
compilerDirective : AMPERSAND ( AT_CLIENT | AT_SERVER | AT_SERVER_NO_CONTEXT
                    | AT_CLIENT_AT_SERVER | AT_CLIENT_AT_SERVER_NO_CONTEXT ) ;
```

> **Для OES web client:** Когда web-клиент станет основным, нужно реализовать разделение контекстов client/server в парсере и интерпретаторе. Это определяет, какой код выполняется в браузере (через WASM или JS), а какой — на сервере.

### 5.10 SDBL — встроенный язык запросов 1С

Отдельная ANTLR4 грамматика: `SDBLParser.g4` + `SDBLLexer.g4`

SQL-подобный язык, но с абстракцией над метаданными:
```sql
ВЫБРАТЬ
    Товары.Наименование,
    Товары.Артикул,
    Цены.Цена
ИЗ
    Справочник.Товары КАК Товары
    ЛЕВОЕ СОЕДИНЕНИЕ РегистрСведений.Цены.СрезПоследних КАК Цены
    ПО Цены.Товар = Товары.Ссылка
ГДЕ
    НЕ Товары.ПометкаУдаления
```

> **Для OES:** Сейчас используем `db_query->RunQuery()` с сырым SQL. Добавление абстрактного языка запросов (как SDBL) позволит:
> - Абстрагироваться от конкретной СУБД (Firebird/PostgreSQL/SQLite)
> - Использовать имена метаданных вместо имён таблиц
> - Автоматические JOIN-ы для связанных объектов
> - Виртуальные таблицы (срезы регистров)

### 5.11 Бинарный формат .cf

Контейнер с сигнатурой `0xFF 0xFF 0xFF 0x7F`, страничная организация. Декомпилятор: `github.com/elisy/MdInternals` (C#, GPL).

> **Для OES:** Если реализовать чтение .cf — можно будет импортировать готовые конфигурации 1С. Но GPL-лицензия MdInternals не совместима с нашей LGPL, нужна собственная реализация.

---

## Часть 6: Итоговые рекомендации (обновлённые)

### Топ-5 архитектурных решений для заимствования

| # | Решение | Откуда | Влияние на OES |
|---|---------|--------|---------------|
| 1 | **TypeDescription (составной тип с квалификаторами)** | mdclasses / EDT | Без этого невозможен полный импорт 1С-конфигураций |
| 2 | **Полный AST + Visitor pattern** | OneScript | Открывает LSP, диагностики, AI code completion |
| 3 | **SDBL-подобный язык запросов** | bsl-parser (SDBLParser.g4) | Абстракция над СУБД, виртуальные таблицы |
| 4 | **Директивы &AtClient/&AtServer** | EDT / bsl-parser | Критично для web-клиента |
| 5 | **ТаблицаЗначений как native тип** | OneScript | 80% конфигураций 1С используют |

### Топ-5 инструментальных решений

| # | Решение | Откуда | Применение |
|---|---------|--------|-----------|
| 1 | **ANTLR4 грамматика BSL** | bsl-parser | Верификация совместимости парсера |
| 2 | **200 диагностик** | bsl-language-server | Референс для OES LSP |
| 3 | **Атрибутная регистрация функций** | OneScript | Масштабирование ibSystemManager |
| 4 | **Сериализация bytecode** | OneScript | Кеш компиляции |
| 5 | **Маппинг FormItem → Formily** | mdclasses | Импорт форм 1С в web-клиент |

### Чего НЕ копировать

- **Отсутствие DB-абстракции** в OneScript — наш `ibDatabaseLayer` с 5 драйверами сильнее
- **EMF/Ecore зависимость** EDT — слишком тяжёлый фреймворк, нам не нужен
- **GPL-код** из MdInternals — несовместим с LGPL OES
