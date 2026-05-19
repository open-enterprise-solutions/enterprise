#!/usr/bin/env python3
"""
Populate keyword content across data/help/{en-US,ru-RU,uk-UA}/keywords.json.

Configuration design rule: the working language of every OES configuration
is English. Keywords have a single canonical spelling ("Procedure", "If",
"Try"), so syntax blocks and worked examples are English-only across all
locales. Localised entries differ only in prose fields — description,
parameters, return_descr — which is what a Russian / Ukrainian reader
actually needs translated.

The tool is idempotent: re-running overwrites only the fields shipped in
the master content table and flips reviewed=true on matched ids.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[1]
DATA = REPO / "data" / "help"

# Constant across every keyword entry: all four runtime targets run the
# same parser.
AVAIL = "Designer, codeRunner, daemon, wenterprise-server"

# Shared English-only syntax/example bodies. Same string lands in every
# locale corpus so the visible code in the help pane matches what a
# developer types into a module — which is always English.
SHARED = {
    "kw.Procedure": {
        "syntax_block": "Procedure <Name>([Val] <Param1>, [Val] <Param2>, …) [Export]\n    // body\nEndProcedure",
        "example": "Procedure LogMessage(Val Text) Export\n    Message(Text);\nEndProcedure",
        "see_also": ["kw.Function", "kw.EndProcedure", "kw.Return", "kw.Export", "kw.Val"],
    },
    "kw.EndProcedure": {
        "syntax_block": "Procedure Demo()\n    // …\nEndProcedure",
        "see_also": ["kw.Procedure"],
    },
    "kw.Function": {
        "syntax_block": "Function <Name>([Val] <Param1>, …) [Export]\n    // body\n    Return <Value>;\nEndFunction",
        "example": "Function Sum(Val A, Val B) Export\n    Return A + B;\nEndFunction",
        "see_also": ["kw.Procedure", "kw.Return", "kw.EndFunction", "kw.Export"],
    },
    "kw.EndFunction": {
        "syntax_block": "Function Demo()\n    Return 42;\nEndFunction",
        "see_also": ["kw.Function", "kw.Return", "kw.Undefined"],
    },
    "kw.Return": {
        "syntax_block": "Return;                 // exit procedure\nReturn <Expression>;    // exit function with result",
        "example": "Function Sign(Val X)\n    If X < 0 Then Return -1; EndIf;\n    If X > 0 Then Return 1;  EndIf;\n    Return 0;\nEndFunction",
        "see_also": ["kw.Function", "kw.Procedure"],
    },
    "kw.Export": {
        "syntax_block": "Procedure DoWork() Export\n    // body\nEndProcedure",
        "example": "Var Counter Export;  // module-public variable\n\nFunction Read() Export\n    Return Counter;\nEndFunction",
        "see_also": ["kw.Procedure", "kw.Function", "kw.Var"],
    },
    "kw.Val": {
        "syntax_block": "Procedure Demo(Val ReadOnlyParam, MutableParam)\n    // ReadOnlyParam is a copy of the caller's value.\n    // MutableParam writes back into the caller's variable.\nEndProcedure",
        "see_also": ["kw.Procedure", "kw.Function"],
    },
    "kw.Var": {
        "syntax_block": "Var <Name1>, <Name2>, …;\n\nProcedure Demo()\n    Var Counter, Sum;\n    Counter = 0;\nEndProcedure",
        "see_also": ["kw.Undefined", "kw.Export"],
    },
    "kw.If": {
        "syntax_block": "If <Expression1> Then\n    // statements\nElsIf <Expression2> Then\n    // statements\nElse\n    // statements\nEndIf",
        "example": "If userInfo.IsAdmin Then\n    EnableAdminMenu();\nElse\n    HideAdminMenu();\nEndIf",
        "see_also": ["kw.Then", "kw.Else", "kw.Elseif", "kw.Endif", "kw.While"],
    },
    "kw.Then": {
        "syntax_block": "If <Expr> Then\n    // body\nEndIf;",
        "see_also": ["kw.If", "kw.Else", "kw.Elseif", "kw.Endif"],
    },
    "kw.Else": {
        "syntax_block": "If <Expr> Then\n    // matched\nElse\n    // fallback\nEndIf;",
        "see_also": ["kw.If", "kw.Elseif", "kw.Endif"],
    },
    "kw.Elseif": {
        "syntax_block": "If A Then\n    // …\nElsIf B Then\n    // …\nElsIf C Then\n    // …\nEndIf;",
        "see_also": ["kw.If", "kw.Else", "kw.Endif"],
    },
    "kw.Endif": {
        "syntax_block": "If <Expr> Then\n    // body\nEndIf;",
        "see_also": ["kw.If"],
    },
    "kw.While": {
        "syntax_block": "While <Condition> Do\n    // body\nEndDo;",
        "example": "Counter = 0;\nWhile Counter < 10 Do\n    Counter = Counter + 1;\nEndDo;",
        "see_also": ["kw.For", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
    },
    "kw.For": {
        "syntax_block": "// indexed\nFor i = 1 To 10 Do\n    // body\nEndDo;\n\n// for-each\nFor Each Item In Collection Do\n    // body\nEndDo;",
        "example": "For Each Order In Customer.Orders Do\n    Total = Total + Order.Amount;\nEndDo;",
        "see_also": ["kw.While", "kw.Foreach", "kw.In", "kw.To", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
    },
    "kw.Foreach": {
        "syntax_block": "For Each <Item> In <Collection> Do\n    // body\nEndDo;",
        "see_also": ["kw.For", "kw.In", "kw.Do"],
    },
    "kw.In": {
        "syntax_block": "For Each <Item> In <Collection> Do … EndDo;",
        "see_also": ["kw.For", "kw.Foreach"],
    },
    "kw.To": {
        "syntax_block": "For i = 1 To 10 Do … EndDo;",
        "see_also": ["kw.For"],
    },
    "kw.Do": {
        "syntax_block": "While <Cond> Do … EndDo;",
        "see_also": ["kw.While", "kw.For", "kw.EndDo"],
    },
    "kw.EndDo": {
        "syntax_block": "While X Do\n    // …\nEndDo;",
        "see_also": ["kw.While", "kw.For", "kw.Do"],
    },
    "kw.Break": {
        "syntax_block": "While Cond Do\n    If StopCondition Then Break; EndIf;\n    // …\nEndDo;",
        "see_also": ["kw.While", "kw.For", "kw.Continue"],
    },
    "kw.Continue": {
        "syntax_block": "For Each Row In Table Do\n    If Row.IsHeader Then Continue; EndIf;\n    Process(Row);\nEndDo;",
        "see_also": ["kw.For", "kw.While", "kw.Break"],
    },
    "kw.Try": {
        "syntax_block": "Try\n    // protected body\nExcept\n    // handler — runs on any raised exception\nEndTry;",
        "example": "Try\n    Result = ParseNumber(UserInput);\nExcept\n    Message(\"Not a valid number: \" + ErrorDescription());\nEndTry;",
        "see_also": ["kw.Except", "kw.Endtry", "kw.Raise"],
    },
    "kw.Except": {
        "syntax_block": "Try\n    // …\nExcept\n    // recovery\nEndTry;",
        "see_also": ["kw.Try", "kw.Endtry", "kw.Raise"],
    },
    "kw.Endtry": {
        "syntax_block": "Try … Except … EndTry;",
        "see_also": ["kw.Try", "kw.Except"],
    },
    "kw.Raise": {
        "syntax_block": "Raise;                       // re-throw current exception\nRaise <ExpressionString>;    // raise a new exception",
        "example": "If Amount < 0 Then\n    Raise \"Negative amount is not allowed\";\nEndIf;",
        "see_also": ["kw.Try", "kw.Except"],
    },
    "kw.New": {
        "syntax_block": "<Var> = New <TypeName>(<Args>…);",
        "example": "Items = New Array;\nItems.Add(\"first\");\nItems.Add(\"second\");\n\nQuery = New Query();\nQuery.Text = \"SELECT * FROM Catalog.Items\";",
        "see_also": ["kw.Undefined"],
    },
    "kw.Undefined": {
        "example": "Var X;\nIf X = Undefined Then\n    X = ComputeDefault();\nEndIf;",
        "see_also": ["kw.Null", "kw.New", "kw.Var"],
    },
    "kw.Null": {
        "example": "If Selection.Manager = Null Then\n    ManagerName = \"<unassigned>\";\nEndIf;",
        "see_also": ["kw.Undefined"],
    },
    "kw.True": {
        "syntax_block": "Flag = True;",
        "see_also": ["kw.False", "kw.And", "kw.Or", "kw.Not"],
    },
    "kw.False": {
        "syntax_block": "Flag = False;",
        "see_also": ["kw.True", "kw.And", "kw.Or", "kw.Not"],
    },
    "kw.And": {
        "syntax_block": "<LeftExpr> And <RightExpr>",
        "example": "If User.IsLoggedIn And User.Role = \"admin\" Then\n    ShowAdminPanel();\nEndIf;",
        "see_also": ["kw.Or", "kw.Not"],
    },
    "kw.Or": {
        "syntax_block": "<LeftExpr> Or <RightExpr>",
        "see_also": ["kw.And", "kw.Not"],
    },
    "kw.Not": {
        "syntax_block": "Not <Expression>",
        "example": "If Not User.IsLoggedIn Then\n    ShowLoginForm();\nEndIf;",
        "see_also": ["kw.And", "kw.Or"],
    },
    "kw.GoTo": {
        "syntax_block": "GoTo ~<Label>;\n…\n~<Label>:",
    },
}

# Locale-specific prose. Only description / parameters / return_descr
# differ between locales — keywords, syntax blocks, and examples are
# English-only by design.
PROSE = {
    "kw.Procedure": {
        "en-US": {
            "description": "Declares a procedure — a callable unit that performs work but does not return a value. Use Function instead when the caller needs a result.",
            "parameters": "<Name> — identifier. Val — pass-by-value modifier. Export — make the procedure callable from other modules.",
        },
        "ru-RU": {
            "description": "Объявляет процедуру — вызываемый блок кода, который выполняет действия, но не возвращает значение. Когда вызывающему нужен результат — используйте Function.",
            "parameters": "<Name> — идентификатор. Val — модификатор «по значению». Export — делает процедуру доступной из других модулей.",
        },
        "uk-UA": {
            "description": "Оголошує процедуру — викликаний блок коду, що виконує дії, але не повертає значення. Якщо викликачу потрібен результат — використовуйте Function.",
            "parameters": "<Name> — ідентифікатор. Val — модифікатор «за значенням». Export — робить процедуру доступною з інших модулів.",
        },
    },
    "kw.EndProcedure": {
        "en-US": {"description": "Closes a procedure body. Execution returns to the caller when control reaches EndProcedure."},
        "ru-RU": {"description": "Закрывает тело процедуры. Управление возвращается вызывающему коду при достижении EndProcedure."},
        "uk-UA": {"description": "Закриває тіло процедури. Керування повертається викликачу при досягненні EndProcedure."},
    },
    "kw.Function": {
        "en-US": {
            "description": "Declares a function — a callable that returns a value via Return. Functions are interchangeable with procedures everywhere except as expression sources.",
            "parameters": "<Name> — identifier. Val — pass-by-value modifier. Export — make the function callable from other modules.",
            "return_descr": "Whatever the Return statement produces.",
        },
        "ru-RU": {
            "description": "Объявляет функцию — вызываемый блок кода, возвращающий значение через Return. Функции взаимозаменяемы с процедурами везде, кроме использования в качестве источника выражения.",
            "parameters": "<Name> — идентификатор. Val — модификатор «по значению». Export — делает функцию доступной из других модулей.",
            "return_descr": "Значение, переданное оператору Return.",
        },
        "uk-UA": {
            "description": "Оголошує функцію — викликаний блок коду, що повертає значення через Return. Функції взаємозамінні з процедурами скрізь, окрім використання як джерело виразу.",
            "parameters": "<Name> — ідентифікатор. Val — модифікатор «за значенням». Export — робить функцію доступною з інших модулів.",
            "return_descr": "Значення, передане оператору Return.",
        },
    },
    "kw.EndFunction": {
        "en-US": {"description": "Closes a function body. Reaching EndFunction without an explicit Return makes the function yield Undefined."},
        "ru-RU": {"description": "Закрывает тело функции. Достижение EndFunction без оператора Return приводит к возврату Undefined."},
        "uk-UA": {"description": "Закриває тіло функції. Досягнення EndFunction без оператора Return дає Undefined."},
    },
    "kw.Return": {
        "en-US": {"description": "Exits the current procedure or function. Inside a function may carry a result expression."},
        "ru-RU": {"description": "Завершает текущую процедуру или функцию. Внутри функции может содержать возвращаемое выражение."},
        "uk-UA": {"description": "Виходить з поточної процедури або функції. Усередині функції може містити вираз-результат."},
    },
    "kw.Export": {
        "en-US": {"description": "Marks a procedure, function, or module-level variable as accessible from other modules. Without Export the symbol is private to its defining module."},
        "ru-RU": {"description": "Помечает процедуру, функцию или модульную переменную как доступную из других модулей. Без Export символ виден только в своём модуле."},
        "uk-UA": {"description": "Позначає процедуру, функцію або модульну змінну доступною з інших модулів. Без Export символ видно лише у своєму модулі."},
    },
    "kw.Val": {
        "en-US": {"description": "Pass-by-value parameter modifier. Without Val the parameter is bound by reference and assignments inside the callee mutate the caller's binding; with Val a local copy is taken."},
        "ru-RU": {"description": "Модификатор параметра «по значению». Без Val параметр связан по ссылке, и присваивания внутри вызываемого кода меняют переменную вызывающего; со Val берётся локальная копия."},
        "uk-UA": {"description": "Модифікатор параметра «за значенням». Без Val параметр звʼязаний за посиланням, і присвоєння всередині викликаного коду змінюють змінну викликача; зі Val береться локальна копія."},
    },
    "kw.Var": {
        "en-US": {"description": "Declares one or more local variables (inside a procedure / function body) or module-level variables (outside any callable). Initial value is Undefined."},
        "ru-RU": {"description": "Объявляет одну или несколько локальных переменных (внутри тела процедуры / функции) либо модульных переменных (вне любых блоков). Начальное значение — Undefined."},
        "uk-UA": {"description": "Оголошує одну або декілька локальних змінних (усередині тіла процедури / функції) або модульних змінних. Початкове значення — Undefined."},
    },
    "kw.If": {
        "en-US": {
            "description": "Conditional execution. Evaluates the boolean expression; runs the Then branch when true, otherwise an optional ElsIf branch with its own expression, otherwise an optional Else branch.",
            "parameters": "<Expression1>, <Expression2> — boolean expressions.",
        },
        "ru-RU": {
            "description": "Условное выполнение. Вычисляет логическое выражение; при значении Истина выполняет ветку Then, иначе — необязательную ветку ElsIf с собственным выражением, иначе — необязательную ветку Else.",
            "parameters": "<Expression1>, <Expression2> — логические выражения.",
        },
        "uk-UA": {
            "description": "Умовне виконання. Обчислює логічний вираз; за значення True виконує гілку Then, інакше — необовʼязкову гілку ElsIf з власним виразом, інакше — необовʼязкову гілку Else.",
            "parameters": "<Expression1>, <Expression2> — логічні вирази.",
        },
    },
    "kw.Then": {
        "en-US": {"description": "Terminates the condition expression of an If / ElsIf branch. The body that follows runs only when the corresponding expression is true."},
        "ru-RU": {"description": "Завершает условие ветки If / ElsIf. Тело после Then выполняется только при истинности соответствующего выражения."},
        "uk-UA": {"description": "Завершує умову гілки If / ElsIf. Тіло після Then виконується лише за істинної умови."},
    },
    "kw.Else": {
        "en-US": {"description": "Branch executed when every preceding If / ElsIf condition was false."},
        "ru-RU": {"description": "Ветка, выполняющаяся, когда все условия If / ElsIf оказались ложными."},
        "uk-UA": {"description": "Гілка, що виконується, коли всі умови If / ElsIf виявилися хибними."},
    },
    "kw.Elseif": {
        "en-US": {"description": "Adds a chained condition to an If statement. Evaluated only if every earlier branch's condition was false."},
        "ru-RU": {"description": "Добавляет цепочечное условие к If. Проверяется, только если каждая предыдущая ветка дала Ложь."},
        "uk-UA": {"description": "Додає ланцюгову умову до If. Перевіряється, лише якщо кожна попередня гілка дала False."},
    },
    "kw.Endif": {
        "en-US": {"description": "Closes an If construct."},
        "ru-RU": {"description": "Закрывает конструкцию If."},
        "uk-UA": {"description": "Закриває конструкцію If."},
    },
    "kw.While": {
        "en-US": {"description": "Pre-test loop. Evaluates the condition before each iteration; runs the body while the condition is true."},
        "ru-RU": {"description": "Цикл с предусловием. Перед каждой итерацией проверяет условие; выполняет тело, пока условие истинно."},
        "uk-UA": {"description": "Цикл з передумовою. Перед кожною ітерацією перевіряє умову; виконує тіло, поки умова істинна."},
    },
    "kw.For": {
        "en-US": {"description": "Counted or iterator-based loop. Two forms: indexed (For <i> = <from> To <to> Do) and for-each (For Each <item> In <collection> Do)."},
        "ru-RU": {"description": "Счётный или итераторный цикл. Две формы: индексная (For <i> = <от> To <до> Do) и поэлементная (For Each <Элемент> In <Коллекция> Do)."},
        "uk-UA": {"description": "Лічильниковий або ітераторний цикл. Дві форми: індексна (For <i> = <від> To <до> Do) і поелементна (For Each <Елемент> In <Колекція> Do)."},
    },
    "kw.Foreach": {
        "en-US": {"description": "Loop modifier introducing for-each iteration over a collection."},
        "ru-RU": {"description": "Модификатор цикла, вводящий поэлементную итерацию по коллекции."},
        "uk-UA": {"description": "Модифікатор циклу, що вводить поелементну ітерацію по колекції."},
    },
    "kw.In": {
        "en-US": {"description": "Marks the collection expression in a for-each loop."},
        "ru-RU": {"description": "Указывает коллекцию-источник в цикле For Each."},
        "uk-UA": {"description": "Позначає колекцію-джерело в циклі For Each."},
    },
    "kw.To": {
        "en-US": {"description": "Upper bound (inclusive) of the indexed For loop."},
        "ru-RU": {"description": "Верхняя граница (включительно) счётного цикла For."},
        "uk-UA": {"description": "Верхня межа (включно) лічильникового циклу For."},
    },
    "kw.Do": {
        "en-US": {"description": "Opens the body of a While or For loop."},
        "ru-RU": {"description": "Открывает тело цикла While или For."},
        "uk-UA": {"description": "Відкриває тіло циклу While або For."},
    },
    "kw.EndDo": {
        "en-US": {"description": "Closes the body of a While or For loop."},
        "ru-RU": {"description": "Закрывает тело цикла While или For."},
        "uk-UA": {"description": "Закриває тіло циклу While або For."},
    },
    "kw.Break": {
        "en-US": {"description": "Aborts the innermost enclosing loop and continues after EndDo."},
        "ru-RU": {"description": "Прерывает ближайший охватывающий цикл; управление переходит за EndDo."},
        "uk-UA": {"description": "Перериває найближчий охоплюючий цикл; керування переходить за EndDo."},
    },
    "kw.Continue": {
        "en-US": {"description": "Skips the rest of the current loop iteration; execution resumes at the loop header."},
        "ru-RU": {"description": "Пропускает остаток текущей итерации цикла; управление переходит к заголовку цикла."},
        "uk-UA": {"description": "Пропускає решту поточної ітерації; керування переходить на заголовок циклу."},
    },
    "kw.Try": {
        "en-US": {"description": "Begins an exception-handling block. Code inside is monitored; a raised exception transfers control to the Except branch."},
        "ru-RU": {"description": "Начинает блок обработки исключений. Код внутри отслеживается; возникшее исключение передаёт управление в ветку Except."},
        "uk-UA": {"description": "Починає блок обробки виключень. Код всередині відстежується; виникле виключення передає керування в гілку Except."},
    },
    "kw.Except": {
        "en-US": {"description": "Opens the handler branch of a Try / Except construct. Runs when the protected body raises an exception."},
        "ru-RU": {"description": "Открывает ветку обработчика конструкции Try / Except. Выполняется, если защищённый блок породил исключение."},
        "uk-UA": {"description": "Відкриває гілку обробника конструкції Try / Except. Виконується, якщо захищений блок породив виключення."},
    },
    "kw.Endtry": {
        "en-US": {"description": "Closes a Try / Except construct."},
        "ru-RU": {"description": "Закрывает конструкцию Try / Except."},
        "uk-UA": {"description": "Закриває конструкцію Try / Except."},
    },
    "kw.Raise": {
        "en-US": {"description": "Raises an exception. Bare form re-throws the current exception (only valid inside Except). With an expression — raises a new exception carrying that text."},
        "ru-RU": {"description": "Возбуждает исключение. Без аргумента — перевозбуждает текущее (допустимо только внутри Except). С выражением — создаёт новое исключение с этим текстом."},
        "uk-UA": {"description": "Викликає виключення. Без аргументу — перепороджує поточне (лише всередині Except). З виразом — створює нове виключення з цим текстом."},
    },
    "kw.New": {
        "en-US": {"description": "Constructs a value of a built-in type (collections, primitive wrappers, query / stream readers). Type name follows New; constructor arguments in parentheses if required."},
        "ru-RU": {"description": "Создаёт значение встроенного типа (коллекции, обёртки примитивов, читатели запросов / потоков). Имя типа идёт после New; аргументы конструктора в скобках, если нужны."},
        "uk-UA": {"description": "Створює значення вбудованого типу (колекції, обгортки примітивів, читачі запитів / потоків). Імʼя типу йде після New; аргументи конструктора в дужках, якщо потрібні."},
    },
    "kw.Undefined": {
        "en-US": {"description": "Singleton value indicating \"no value here yet\". Initial value of every declared but unassigned variable; also the natural result of methods that have nothing meaningful to return."},
        "ru-RU": {"description": "Сингл-значение «значения ещё нет». Начальное значение любой объявленной, но не инициализированной переменной; естественный результат методов, которым нечего вернуть."},
        "uk-UA": {"description": "Сингл-значення «ще немає значення». Початкове значення будь-якої оголошеної, але не ініціалізованої змінної; природний результат методів, яким немає що повернути."},
    },
    "kw.Null": {
        "en-US": {"description": "Database-flavoured \"no value\" used by query result fields where a SQL NULL is returned. Distinct from Undefined; comparisons with regular values are always false."},
        "ru-RU": {"description": "Значение «нет значения» из баз данных — то, что возвращают поля результата запроса при SQL NULL. Отличается от Undefined; сравнения с обычными значениями всегда дают False."},
        "uk-UA": {"description": "Значення «немає» з результатів запиту до БД (SQL NULL). Відрізняється від Undefined; порівняння зі звичайними значеннями завжди дають False."},
    },
    "kw.True":  {"en-US": {"description": "Boolean literal — the true value."},
                  "ru-RU": {"description": "Логический литерал — значение «истина»."},
                  "uk-UA": {"description": "Логічний літерал — істина."}},
    "kw.False": {"en-US": {"description": "Boolean literal — the false value."},
                  "ru-RU": {"description": "Логический литерал — значение «ложь»."},
                  "uk-UA": {"description": "Логічний літерал — хибність."}},
    "kw.And":   {"en-US": {"description": "Logical conjunction. Short-circuits: the right operand is not evaluated when the left is false."},
                  "ru-RU": {"description": "Логическое И. Короткое замыкание: правый операнд не вычисляется, если левый — False."},
                  "uk-UA": {"description": "Логічне І. Коротке замикання: правий операнд не обчислюється, якщо лівий — False."}},
    "kw.Or":    {"en-US": {"description": "Logical disjunction. Short-circuits: the right operand is not evaluated when the left is true."},
                  "ru-RU": {"description": "Логическое ИЛИ. Короткое замыкание: правый операнд не вычисляется, если левый — True."},
                  "uk-UA": {"description": "Логічне АБО. Коротке замикання: правий операнд не обчислюється, якщо лівий — True."}},
    "kw.Not":   {"en-US": {"description": "Logical negation."},
                  "ru-RU": {"description": "Логическое отрицание."},
                  "uk-UA": {"description": "Логічне заперечення."}},
    "kw.GoTo":  {"en-US": {"description": "Unconditional jump to a labelled statement within the same scope. Use sparingly — a loop / function refactor is almost always clearer."},
                  "ru-RU": {"description": "Безусловный переход к помеченному оператору в пределах того же блока. Применяйте умеренно — почти всегда чище переписать через цикл или функцию."},
                  "uk-UA": {"description": "Безумовний перехід до позначеного оператора в межах того ж блоку. Використовуйте помірно — майже завжди чистіше переписати через цикл або функцію."}},
}

# Keywords whose canonical spelling is the same in all locales (which is
# all of them — working language is English by design). The previous
# version of the corpus shipped localised name_local values; this run
# resets them to match name_en so the editor + resolver agree on a
# single canonical form.
NAME_RESET = list(SHARED.keys())


def merge(entry: dict[str, Any], shared: dict[str, Any],
          prose: dict[str, Any], reset_name: bool) -> bool:
    changed = False
    # Shared (English-only) fields.
    for key in ("syntax_block", "example", "see_also"):
        if key in shared and entry.get(key) != shared[key]:
            entry[key] = shared[key]
            changed = True
    # Per-locale prose.
    for key in ("description", "parameters", "return_descr"):
        if key in prose and entry.get(key) != prose[key]:
            entry[key] = prose[key]
            changed = True
    # Reset name_local to match name_en for working-language-English design.
    if reset_name:
        name_en = entry.get("name_en", "")
        if name_en and entry.get("name_local") != name_en:
            entry["name_local"] = name_en
            changed = True
    if entry.get("availability") != AVAIL:
        entry["availability"] = AVAIL
        changed = True
    if not entry.get("reviewed"):
        entry["reviewed"] = True
        changed = True
    return changed


def apply(locale: str) -> tuple[int, int]:
    file = DATA / locale / "keywords.json"
    doc = json.loads(file.read_text(encoding="utf-8"))
    touched = 0
    seen = 0
    for entry in doc.get("entries", []):
        eid = entry.get("id", "")
        shared = SHARED.get(eid, {})
        prose = PROSE.get(eid, {}).get(locale, {})
        if not shared and not prose:
            continue
        seen += 1
        if merge(entry, shared, prose, reset_name=eid in NAME_RESET):
            touched += 1
    file.write_text(
        json.dumps(doc, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return seen, touched


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--locale", action="append", default=None,
                   help="restrict to one or more locales (repeatable)")
    args = p.parse_args()
    locales = args.locale or ["en-US", "ru-RU", "uk-UA"]
    for loc in locales:
        seen, touched = apply(loc)
        print(f"{loc}: matched {seen} / updated {touched}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
