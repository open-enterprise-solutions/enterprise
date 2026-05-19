#!/usr/bin/env python3
"""
Populate keyword content (description, syntax_block, parameters,
return_descr, example, see_also) across the three locale corpora
in data/help/{en-US,ru-RU,uk-UA}/keywords.json from a single
master content table embedded below.

Designed to be idempotent: re-running overwrites only the fields
shipped in the master table and flips reviewed=True on matched ids.
Untouched fields and untouched entries stay as they were.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[1]
DATA = REPO / "data" / "help"

# -------------------------------------------------------------------- content

# id -> { locale -> { field -> value } }
# Field set: description, syntax_block, parameters, return_descr, example,
#            see_also, name_local (only for ru-RU/uk-UA when keyword differs).
# Availability is constant across all keywords — set once below.
AVAIL = "Designer, codeRunner, daemon, wenterprise-server"

# Russian/Ukrainian keyword spellings recognised by the OES tokenizer.
# (Internal note — these are the localised forms users actually type in the
# editor; the resolver matches against name_local in the active locale.)
LOCAL_NAME = {
    # id : (ru, uk)
    "kw.If":         ("Если",      "Якщо"),
    "kw.Then":       ("Тогда",     "Тоді"),
    "kw.Else":       ("Иначе",     "Інакше"),
    "kw.Elseif":     ("ИначеЕсли", "ІнакшеЯкщо"),
    "kw.Endif":      ("КонецЕсли", "КінецьЯкщо"),
    "kw.While":      ("Пока",      "Поки"),
    "kw.For":        ("Для",       "Для"),
    "kw.Foreach":    ("Каждого",   "Кожного"),
    "kw.In":         ("Из",        "З"),
    "kw.To":         ("По",        "По"),
    "kw.Do":         ("Цикл",      "Цикл"),
    "kw.EndDo":      ("КонецЦикла","КінецьЦиклу"),
    "kw.Break":      ("Прервать",  "Перервати"),
    "kw.Continue":   ("Продолжить","Продовжити"),
    "kw.Procedure":  ("Процедура", "Процедура"),
    "kw.EndProcedure":("КонецПроцедуры","КінецьПроцедури"),
    "kw.Function":   ("Функция",   "Функція"),
    "kw.EndFunction":("КонецФункции","КінецьФункції"),
    "kw.Return":     ("Возврат",   "Повернути"),
    "kw.Export":     ("Экспорт",   "Експорт"),
    "kw.Val":        ("Знач",      "Знач"),
    "kw.Var":        ("Перем",     "Змінна"),
    "kw.Try":        ("Попытка",   "Спроба"),
    "kw.Except":     ("Исключение","Виняток"),
    "kw.Endtry":     ("КонецПопытки","КінецьСпроби"),
    "kw.Raise":      ("ВызватьИсключение","ВикликатиВиняток"),
    "kw.New":        ("Новый",     "Новий"),
    "kw.Undefined":  ("Неопределено","Невизначено"),
    "kw.Null":       ("Null",      "Null"),
    "kw.True":       ("Истина",    "Істина"),
    "kw.False":      ("Ложь",      "Хибність"),
    "kw.And":        ("И",         "І"),
    "kw.Or":         ("Или",       "Або"),
    "kw.Not":        ("Не",        "Не"),
    "kw.GoTo":       ("Перейти",   "Перейти"),
}

# Per-id content. Each value is a dict with locale-specific text plus the
# language-agnostic see_also list. "en-US" key is the English form, "ru-RU"
# uses Russian keywords inside the example/syntax, "uk-UA" uses Ukrainian.

KW = {
    "kw.Procedure": {
        "en-US": {
            "description": "Declares a procedure — a callable unit that performs work but does not return a value. Use Function instead when the caller needs a result.",
            "syntax_block": "Procedure <Name>([Val] <Param1>, [Val] <Param2>, …) [Export]\n    // body\nEndProcedure",
            "parameters": "<Name> — identifier. Val — pass-by-value modifier. Export — make procedure callable from other modules.",
            "example": "Procedure LogMessage(Val Text) Export\n    Message(Text);\nEndProcedure",
            "see_also": ["kw.Function", "kw.EndProcedure", "kw.Return", "kw.Export", "kw.Val"],
        },
        "ru-RU": {
            "description": "Объявляет процедуру — вызываемый блок кода, который выполняет действия, но не возвращает значение. Используйте Функция, если вызывающему нужен результат.",
            "syntax_block": "Процедура <Имя>([Знач] <Парам1>, [Знач] <Парам2>, …) [Экспорт]\n    // тело\nКонецПроцедуры",
            "parameters": "<Имя> — идентификатор. Знач — параметр передаётся по значению. Экспорт — процедура доступна из других модулей.",
            "example": "Процедура ВывестиСообщение(Знач Текст) Экспорт\n    Сообщить(Текст);\nКонецПроцедуры",
            "see_also": ["kw.Function", "kw.EndProcedure", "kw.Return", "kw.Export", "kw.Val"],
        },
        "uk-UA": {
            "description": "Оголошує процедуру — викликаний блок коду, який виконує дії, але не повертає значення. Використовуйте Функція, якщо викликачу потрібен результат.",
            "syntax_block": "Процедура <Імʼя>([Знач] <Парам1>, [Знач] <Парам2>, …) [Експорт]\n    // тіло\nКінецьПроцедури",
            "parameters": "<Імʼя> — ідентифікатор. Знач — параметр передається за значенням. Експорт — процедура доступна з інших модулів.",
            "example": "Процедура ВивестиПовідомлення(Знач Текст) Експорт\n    Повідомити(Текст);\nКінецьПроцедури",
            "see_also": ["kw.Function", "kw.EndProcedure", "kw.Return", "kw.Export", "kw.Val"],
        },
    },
    "kw.EndProcedure": {
        "en-US": {
            "description": "Closes a procedure body. The reader exits the procedure when control reaches EndProcedure.",
            "syntax_block": "Procedure Demo()\n    // …\nEndProcedure",
            "see_also": ["kw.Procedure"],
        },
        "ru-RU": {
            "description": "Закрывает тело процедуры. Управление возвращается из процедуры по достижении КонецПроцедуры.",
            "syntax_block": "Процедура Пример()\n    // …\nКонецПроцедуры",
            "see_also": ["kw.Procedure"],
        },
        "uk-UA": {
            "description": "Закриває тіло процедури. Керування повертається з процедури при досягненні КінецьПроцедури.",
            "syntax_block": "Процедура Приклад()\n    // …\nКінецьПроцедури",
            "see_also": ["kw.Procedure"],
        },
    },
    "kw.Function": {
        "en-US": {
            "description": "Declares a function — a callable that returns a value via Return. Functions are interchangeable with procedures everywhere except as expression sources.",
            "syntax_block": "Function <Name>([Val] <Param1>, …) [Export]\n    // body\n    Return <Value>;\nEndFunction",
            "parameters": "<Name> — identifier. Val — pass-by-value modifier. Export — function callable from other modules.",
            "return_descr": "Whatever the Return statement produces.",
            "example": "Function Sum(Val A, Val B) Export\n    Return A + B;\nEndFunction",
            "see_also": ["kw.Procedure", "kw.Return", "kw.EndFunction", "kw.Export"],
        },
        "ru-RU": {
            "description": "Объявляет функцию — вызываемый блок кода, возвращающий значение через Возврат. Функции взаимозаменяемы с процедурами везде, кроме выражений-источников.",
            "syntax_block": "Функция <Имя>([Знач] <Парам1>, …) [Экспорт]\n    // тело\n    Возврат <Значение>;\nКонецФункции",
            "parameters": "<Имя> — идентификатор. Знач — передача параметра по значению. Экспорт — функция доступна из других модулей.",
            "return_descr": "Значение, переданное в оператор Возврат.",
            "example": "Функция Сумма(Знач А, Знач Б) Экспорт\n    Возврат А + Б;\nКонецФункции",
            "see_also": ["kw.Procedure", "kw.Return", "kw.EndFunction", "kw.Export"],
        },
        "uk-UA": {
            "description": "Оголошує функцію — викликаний блок коду, що повертає значення через Повернути. Функції взаємозамінні з процедурами скрізь, крім виразів-джерел.",
            "syntax_block": "Функція <Імʼя>([Знач] <Парам1>, …) [Експорт]\n    // тіло\n    Повернути <Значення>;\nКінецьФункції",
            "parameters": "<Імʼя> — ідентифікатор. Знач — передача параметра за значенням. Експорт — функція доступна з інших модулів.",
            "return_descr": "Значення, передане в оператор Повернути.",
            "example": "Функція Сума(Знач А, Знач Б) Експорт\n    Повернути А + Б;\nКінецьФункції",
            "see_also": ["kw.Procedure", "kw.Return", "kw.EndFunction", "kw.Export"],
        },
    },
    "kw.EndFunction": {
        "en-US": {
            "description": "Closes a function body. Reaching EndFunction without an explicit Return makes the function yield Undefined.",
            "syntax_block": "Function Demo()\n    Return 42;\nEndFunction",
            "see_also": ["kw.Function", "kw.Return", "kw.Undefined"],
        },
        "ru-RU": {
            "description": "Закрывает тело функции. Достижение КонецФункции без оператора Возврат приводит к возврату Неопределено.",
            "syntax_block": "Функция Пример()\n    Возврат 42;\nКонецФункции",
            "see_also": ["kw.Function", "kw.Return", "kw.Undefined"],
        },
        "uk-UA": {
            "description": "Закриває тіло функції. Досягнення КінецьФункції без оператора Повернути дає Невизначено.",
            "syntax_block": "Функція Приклад()\n    Повернути 42;\nКінецьФункції",
            "see_also": ["kw.Function", "kw.Return", "kw.Undefined"],
        },
    },
    "kw.Return": {
        "en-US": {
            "description": "Exits the current procedure or function. Inside a function may carry a result expression.",
            "syntax_block": "Return;            // exit procedure\nReturn <Expression>; // exit function with result",
            "example": "Function Sign(Val X)\n    If X < 0 Then Return -1; EndIf;\n    If X > 0 Then Return 1;  EndIf;\n    Return 0;\nEndFunction",
            "see_also": ["kw.Function", "kw.Procedure"],
        },
        "ru-RU": {
            "description": "Выходит из текущей процедуры или функции. Внутри функции может содержать возвращаемое выражение.",
            "syntax_block": "Возврат;             // выйти из процедуры\nВозврат <Выражение>; // выйти из функции со значением",
            "example": "Функция Знак(Знач Х)\n    Если Х < 0 Тогда Возврат -1; КонецЕсли;\n    Если Х > 0 Тогда Возврат 1;  КонецЕсли;\n    Возврат 0;\nКонецФункции",
            "see_also": ["kw.Function", "kw.Procedure"],
        },
        "uk-UA": {
            "description": "Виходить з поточної процедури або функції. Усередині функції може містити вираз-результат.",
            "syntax_block": "Повернути;             // вийти з процедури\nПовернути <Вираз>;     // вийти з функції зі значенням",
            "example": "Функція Знак(Знач Х)\n    Якщо Х < 0 Тоді Повернути -1; КінецьЯкщо;\n    Якщо Х > 0 Тоді Повернути 1;  КінецьЯкщо;\n    Повернути 0;\nКінецьФункції",
            "see_also": ["kw.Function", "kw.Procedure"],
        },
    },
    "kw.Export": {
        "en-US": {
            "description": "Marks a procedure, function, or module-level variable as accessible from other modules. Without Export the symbol is private to its defining module.",
            "syntax_block": "Procedure DoWork() Export\n    // body\nEndProcedure",
            "example": "Var Counter Export;  // module-public variable\n\nFunction Read() Export\n    Return Counter;\nEndFunction",
            "see_also": ["kw.Procedure", "kw.Function", "kw.Var"],
        },
        "ru-RU": {
            "description": "Помечает процедуру, функцию или модульную переменную как доступную из других модулей. Без Экспорт символ виден только в своём модуле.",
            "syntax_block": "Процедура Работать() Экспорт\n    // тело\nКонецПроцедуры",
            "example": "Перем Счётчик Экспорт;  // модульная публичная переменная\n\nФункция Прочесть() Экспорт\n    Возврат Счётчик;\nКонецФункции",
            "see_also": ["kw.Procedure", "kw.Function", "kw.Var"],
        },
        "uk-UA": {
            "description": "Позначає процедуру, функцію або модульну змінну доступною з інших модулів. Без Експорт символ видно лише у своєму модулі.",
            "syntax_block": "Процедура Працювати() Експорт\n    // тіло\nКінецьПроцедури",
            "example": "Змінна Лічильник Експорт;\n\nФункція Прочитати() Експорт\n    Повернути Лічильник;\nКінецьФункції",
            "see_also": ["kw.Procedure", "kw.Function", "kw.Var"],
        },
    },
    "kw.Val": {
        "en-US": {
            "description": "Pass-by-value parameter modifier. Without Val the caller's variable is bound by reference and assignments inside the callee mutate the caller's binding; with Val a local copy is taken.",
            "syntax_block": "Procedure Demo(Val ReadOnlyParam, MutableParam)\n    // ReadOnlyParam is the caller's value (a copy);\n    // MutableParam writes back into the caller's variable.\nEndProcedure",
            "see_also": ["kw.Procedure", "kw.Function"],
        },
        "ru-RU": {
            "description": "Модификатор параметра «по значению». Без Знач параметр привязан по ссылке, и присваивания внутри процедуры меняют переменную вызывающего; со Знач берётся локальная копия.",
            "syntax_block": "Процедура Пример(Знач ТолькоЧтение, Изменяемый)\n    // ТолькоЧтение — копия значения вызывающего;\n    // Изменяемый — присваивание попадёт в переменную вызывающего.\nКонецПроцедуры",
            "see_also": ["kw.Procedure", "kw.Function"],
        },
        "uk-UA": {
            "description": "Модифікатор параметра «за значенням». Без Знач параметр звʼязаний за посиланням; зі Знач береться локальна копія.",
            "syntax_block": "Процедура Приклад(Знач ЛишеЧитання, Мутабельний)\n    // …\nКінецьПроцедури",
            "see_also": ["kw.Procedure", "kw.Function"],
        },
    },
    "kw.Var": {
        "en-US": {
            "description": "Declares one or more local variables (inside a procedure / function body) or module-level variables (outside any callable). Initial value is Undefined.",
            "syntax_block": "Var <Name1>, <Name2>, …;\n\nProcedure Demo()\n    Var Counter, Sum;\n    Counter = 0;\nEndProcedure",
            "see_also": ["kw.Undefined", "kw.Export"],
        },
        "ru-RU": {
            "description": "Объявляет одну или несколько локальных переменных (внутри тела процедуры / функции) либо модульных переменных (вне любых блоков). Начальное значение — Неопределено.",
            "syntax_block": "Перем <Имя1>, <Имя2>, …;\n\nПроцедура Пример()\n    Перем Счётчик, Сумма;\n    Счётчик = 0;\nКонецПроцедуры",
            "see_also": ["kw.Undefined", "kw.Export"],
        },
        "uk-UA": {
            "description": "Оголошує одну або декілька локальних змінних. Початкове значення — Невизначено.",
            "syntax_block": "Змінна <Імʼя1>, <Імʼя2>, …;\n\nПроцедура Приклад()\n    Змінна Лічильник, Сума;\n    Лічильник = 0;\nКінецьПроцедури",
            "see_also": ["kw.Undefined", "kw.Export"],
        },
    },
    "kw.If": {
        # Already populated; leave alone but flip reviewed flag to keep the
        # entry consistent with the rest.
    },
    "kw.Then": {
        "en-US": {
            "description": "Terminates the condition expression of an If / ElsIf branch. The body that follows runs only when the corresponding expression is true.",
            "syntax_block": "If <Expr> Then\n    // body\nEndIf;",
            "see_also": ["kw.If", "kw.Else", "kw.Elseif", "kw.Endif"],
        },
        "ru-RU": {
            "description": "Завершает условие ветки Если / ИначеЕсли. Тело за Тогда выполняется только при истинности соответствующего выражения.",
            "syntax_block": "Если <Выражение> Тогда\n    // тело\nКонецЕсли;",
            "see_also": ["kw.If", "kw.Else", "kw.Elseif", "kw.Endif"],
        },
        "uk-UA": {
            "description": "Завершує умову гілки Якщо / ІнакшеЯкщо. Тіло за Тоді виконується лише за істинної умови.",
            "syntax_block": "Якщо <Вираз> Тоді\n    // тіло\nКінецьЯкщо;",
            "see_also": ["kw.If", "kw.Else", "kw.Elseif", "kw.Endif"],
        },
    },
    "kw.Else": {
        "en-US": {
            "description": "Branch executed when every preceding If / ElsIf condition was false.",
            "syntax_block": "If <Expr> Then\n    // matched\nElse\n    // fallback\nEndIf;",
            "see_also": ["kw.If", "kw.Elseif", "kw.Endif"],
        },
        "ru-RU": {
            "description": "Ветка, выполняющаяся, когда все условия Если / ИначеЕсли оказались ложными.",
            "syntax_block": "Если <Выр> Тогда\n    // совпало\nИначе\n    // запасной путь\nКонецЕсли;",
            "see_also": ["kw.If", "kw.Elseif", "kw.Endif"],
        },
        "uk-UA": {
            "description": "Гілка, що виконується, коли всі умови Якщо / ІнакшеЯкщо виявилися хибними.",
            "syntax_block": "Якщо <Вираз> Тоді\n    // збіг\nІнакше\n    // запасний шлях\nКінецьЯкщо;",
            "see_also": ["kw.If", "kw.Elseif", "kw.Endif"],
        },
    },
    "kw.Elseif": {
        "en-US": {
            "description": "Adds a chained condition to an If statement. Evaluated only if every earlier branch's condition was false.",
            "syntax_block": "If A Then\n    // …\nElsIf B Then\n    // …\nElsIf C Then\n    // …\nEndIf;",
            "see_also": ["kw.If", "kw.Else", "kw.Endif"],
        },
        "ru-RU": {
            "description": "Добавляет цепочечное условие к Если. Проверяется, только если каждая предыдущая ветка дала Ложь.",
            "syntax_block": "Если A Тогда\n    // …\nИначеЕсли B Тогда\n    // …\nИначеЕсли C Тогда\n    // …\nКонецЕсли;",
            "see_also": ["kw.If", "kw.Else", "kw.Endif"],
        },
        "uk-UA": {
            "description": "Додає ланцюгову умову до Якщо. Перевіряється, лише якщо кожна попередня гілка дала Хибність.",
            "syntax_block": "Якщо A Тоді\n    // …\nІнакшеЯкщо B Тоді\n    // …\nКінецьЯкщо;",
            "see_also": ["kw.If", "kw.Else", "kw.Endif"],
        },
    },
    "kw.Endif": {
        "en-US": {
            "description": "Closes an If construct.",
            "syntax_block": "If <Expr> Then\n    // body\nEndIf;",
            "see_also": ["kw.If"],
        },
        "ru-RU": {
            "description": "Закрывает конструкцию Если.",
            "syntax_block": "Если <Выражение> Тогда\n    // тело\nКонецЕсли;",
            "see_also": ["kw.If"],
        },
        "uk-UA": {
            "description": "Закриває конструкцію Якщо.",
            "syntax_block": "Якщо <Вираз> Тоді\n    // тіло\nКінецьЯкщо;",
            "see_also": ["kw.If"],
        },
    },
    "kw.While": {
        "en-US": {
            "description": "Pre-test loop. Evaluates the condition before each iteration; runs the body while the condition is true.",
            "syntax_block": "While <Condition> Do\n    // body\nEndDo;",
            "example": "Counter = 0;\nWhile Counter < 10 Do\n    Counter = Counter + 1;\nEndDo;",
            "see_also": ["kw.For", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
        },
        "ru-RU": {
            "description": "Цикл с предусловием. Перед каждой итерацией проверяет условие; выполняет тело, пока условие истинно.",
            "syntax_block": "Пока <Условие> Цикл\n    // тело\nКонецЦикла;",
            "example": "Счётчик = 0;\nПока Счётчик < 10 Цикл\n    Счётчик = Счётчик + 1;\nКонецЦикла;",
            "see_also": ["kw.For", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
        },
        "uk-UA": {
            "description": "Цикл з передумовою. Перед кожною ітерацією перевіряє умову; виконує тіло, поки умова істинна.",
            "syntax_block": "Поки <Умова> Цикл\n    // тіло\nКінецьЦиклу;",
            "example": "Лічильник = 0;\nПоки Лічильник < 10 Цикл\n    Лічильник = Лічильник + 1;\nКінецьЦиклу;",
            "see_also": ["kw.For", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
        },
    },
    "kw.For": {
        "en-US": {
            "description": "Counted or iterator-based loop. Two forms: indexed (For <i> = <from> To <to> Do) and for-each (For Each <item> In <collection> Do).",
            "syntax_block": "// indexed\nFor i = 1 To 10 Do\n    // body\nEndDo;\n\n// for-each\nFor Each Item In Collection Do\n    // body\nEndDo;",
            "example": "For Each Order In Customer.Orders Do\n    Total = Total + Order.Amount;\nEndDo;",
            "see_also": ["kw.While", "kw.Foreach", "kw.In", "kw.To", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
        },
        "ru-RU": {
            "description": "Счётный или итераторный цикл. Две формы: индексная (Для <i> = <от> По <до> Цикл) и поэлементная (Для Каждого <Элемент> Из <Коллекция> Цикл).",
            "syntax_block": "// счётная форма\nДля i = 1 По 10 Цикл\n    // тело\nКонецЦикла;\n\n// поэлементная\nДля Каждого Элемент Из Коллекция Цикл\n    // тело\nКонецЦикла;",
            "example": "Для Каждого Заказ Из Клиент.Заказы Цикл\n    Итог = Итог + Заказ.Сумма;\nКонецЦикла;",
            "see_also": ["kw.While", "kw.Foreach", "kw.In", "kw.To", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
        },
        "uk-UA": {
            "description": "Лічильниковий або ітераторний цикл. Дві форми: індексна (Для <i> = <від> По <до> Цикл) та поелементна (Для Кожного <Елемент> З <Колекція> Цикл).",
            "syntax_block": "// індексна\nДля i = 1 По 10 Цикл\n    // тіло\nКінецьЦиклу;\n\n// поелементна\nДля Кожного Елемент З Колекція Цикл\n    // тіло\nКінецьЦиклу;",
            "example": "Для Кожного Замовлення З Клієнт.Замовлення Цикл\n    Підсумок = Підсумок + Замовлення.Сума;\nКінецьЦиклу;",
            "see_also": ["kw.While", "kw.Foreach", "kw.In", "kw.To", "kw.Do", "kw.EndDo", "kw.Break", "kw.Continue"],
        },
    },
    "kw.Foreach": {
        "en-US": {
            "description": "Loop modifier introducing for-each iteration over a collection.",
            "syntax_block": "For Each <Item> In <Collection> Do\n    // body\nEndDo;",
            "see_also": ["kw.For", "kw.In", "kw.Do"],
        },
        "ru-RU": {
            "description": "Модификатор цикла, вводящий поэлементную итерацию по коллекции.",
            "syntax_block": "Для Каждого <Элемент> Из <Коллекция> Цикл\n    // тело\nКонецЦикла;",
            "see_also": ["kw.For", "kw.In", "kw.Do"],
        },
        "uk-UA": {
            "description": "Модифікатор циклу, що вводить поелементну ітерацію по колекції.",
            "syntax_block": "Для Кожного <Елемент> З <Колекція> Цикл\n    // тіло\nКінецьЦиклу;",
            "see_also": ["kw.For", "kw.In", "kw.Do"],
        },
    },
    "kw.In": {
        "en-US": {
            "description": "Marks the collection expression in a for-each loop.",
            "syntax_block": "For Each <Item> In <Collection> Do … EndDo;",
            "see_also": ["kw.For", "kw.Foreach"],
        },
        "ru-RU": {
            "description": "Указывает коллекцию-источник в цикле Для Каждого.",
            "syntax_block": "Для Каждого <Элемент> Из <Коллекция> Цикл … КонецЦикла;",
            "see_also": ["kw.For", "kw.Foreach"],
        },
        "uk-UA": {
            "description": "Позначає колекцію-джерело в циклі Для Кожного.",
            "syntax_block": "Для Кожного <Елемент> З <Колекція> Цикл … КінецьЦиклу;",
            "see_also": ["kw.For", "kw.Foreach"],
        },
    },
    "kw.To": {
        "en-US": {
            "description": "Upper bound (inclusive) of the indexed For loop.",
            "syntax_block": "For i = 1 To 10 Do … EndDo;",
            "see_also": ["kw.For"],
        },
        "ru-RU": {
            "description": "Верхняя граница (включительно) счётного цикла Для.",
            "syntax_block": "Для i = 1 По 10 Цикл … КонецЦикла;",
            "see_also": ["kw.For"],
        },
        "uk-UA": {
            "description": "Верхня межа (включно) лічильникового циклу Для.",
            "syntax_block": "Для i = 1 По 10 Цикл … КінецьЦиклу;",
            "see_also": ["kw.For"],
        },
    },
    "kw.Do": {
        "en-US": {
            "description": "Opens the body of a While or For loop.",
            "syntax_block": "While <Cond> Do … EndDo;",
            "see_also": ["kw.While", "kw.For", "kw.EndDo"],
        },
        "ru-RU": {
            "description": "Открывает тело цикла Пока или Для.",
            "syntax_block": "Пока <Условие> Цикл … КонецЦикла;",
            "see_also": ["kw.While", "kw.For", "kw.EndDo"],
        },
        "uk-UA": {
            "description": "Відкриває тіло циклу Поки або Для.",
            "syntax_block": "Поки <Умова> Цикл … КінецьЦиклу;",
            "see_also": ["kw.While", "kw.For", "kw.EndDo"],
        },
    },
    "kw.EndDo": {
        "en-US": {
            "description": "Closes the body of a While or For loop.",
            "syntax_block": "While X Do\n    // …\nEndDo;",
            "see_also": ["kw.While", "kw.For", "kw.Do"],
        },
        "ru-RU": {
            "description": "Закрывает тело цикла Пока или Для.",
            "syntax_block": "Пока X Цикл\n    // …\nКонецЦикла;",
            "see_also": ["kw.While", "kw.For", "kw.Do"],
        },
        "uk-UA": {
            "description": "Закриває тіло циклу Поки або Для.",
            "syntax_block": "Поки X Цикл\n    // …\nКінецьЦиклу;",
            "see_also": ["kw.While", "kw.For", "kw.Do"],
        },
    },
    "kw.Break": {
        "en-US": {
            "description": "Aborts the innermost enclosing loop and continues after EndDo.",
            "syntax_block": "While Cond Do\n    If StopCondition Then Break; EndIf;\n    // …\nEndDo;",
            "see_also": ["kw.While", "kw.For", "kw.Continue"],
        },
        "ru-RU": {
            "description": "Прерывает ближайший охватывающий цикл; управление переходит за КонецЦикла.",
            "syntax_block": "Пока Условие Цикл\n    Если СтопУсловие Тогда Прервать; КонецЕсли;\nКонецЦикла;",
            "see_also": ["kw.While", "kw.For", "kw.Continue"],
        },
        "uk-UA": {
            "description": "Перериває найближчий охоплюючий цикл; керування переходить за КінецьЦиклу.",
            "syntax_block": "Поки Умова Цикл\n    Якщо СтопУмова Тоді Перервати; КінецьЯкщо;\nКінецьЦиклу;",
            "see_also": ["kw.While", "kw.For", "kw.Continue"],
        },
    },
    "kw.Continue": {
        "en-US": {
            "description": "Skips the rest of the current loop iteration; execution resumes at the loop header.",
            "syntax_block": "For Each Row In Table Do\n    If Row.IsHeader Then Continue; EndIf;\n    Process(Row);\nEndDo;",
            "see_also": ["kw.For", "kw.While", "kw.Break"],
        },
        "ru-RU": {
            "description": "Пропускает остаток текущей итерации цикла; управление переходит к заголовку цикла.",
            "syntax_block": "Для Каждого Строка Из Таблица Цикл\n    Если Строка.Заголовок Тогда Продолжить; КонецЕсли;\n    Обработать(Строка);\nКонецЦикла;",
            "see_also": ["kw.For", "kw.While", "kw.Break"],
        },
        "uk-UA": {
            "description": "Пропускає решту поточної ітерації; керування переходить на заголовок циклу.",
            "syntax_block": "Для Кожного Рядок З Таблиця Цикл\n    Якщо Рядок.Заголовок Тоді Продовжити; КінецьЯкщо;\n    Обробити(Рядок);\nКінецьЦиклу;",
            "see_also": ["kw.For", "kw.While", "kw.Break"],
        },
    },
    "kw.Try": {
        "en-US": {
            "description": "Begins an exception-handling block. Code inside is monitored; a raised exception transfers control to the Except branch.",
            "syntax_block": "Try\n    // protected body\nExcept\n    // handler — runs on any raised exception\nEndTry;",
            "example": "Try\n    Result = ParseNumber(UserInput);\nExcept\n    Message(\"Not a valid number: \" + ErrorDescription());\nEndTry;",
            "see_also": ["kw.Except", "kw.Endtry", "kw.Raise"],
        },
        "ru-RU": {
            "description": "Начинает блок обработки исключений. Код внутри отслеживается; возникшее исключение передаёт управление в ветку Исключение.",
            "syntax_block": "Попытка\n    // защищённый блок\nИсключение\n    // обработчик — запускается при любом исключении\nКонецПопытки;",
            "example": "Попытка\n    Результат = РазобратьЧисло(ВводПользователя);\nИсключение\n    Сообщить(\"Не число: \" + ОписаниеОшибки());\nКонецПопытки;",
            "see_also": ["kw.Except", "kw.Endtry", "kw.Raise"],
        },
        "uk-UA": {
            "description": "Починає блок обробки виключень. Код всередині відстежується; виникле виключення передає керування в гілку Виняток.",
            "syntax_block": "Спроба\n    // захищений блок\nВиняток\n    // обробник\nКінецьСпроби;",
            "example": "Спроба\n    Результат = РозібратиЧисло(Ввід);\nВиняток\n    Повідомити(\"Не число: \" + ОписПомилки());\nКінецьСпроби;",
            "see_also": ["kw.Except", "kw.Endtry", "kw.Raise"],
        },
    },
    "kw.Except": {
        "en-US": {
            "description": "Opens the handler branch of a Try / Except construct. Runs when the protected body raises an exception.",
            "syntax_block": "Try\n    // …\nExcept\n    // recovery\nEndTry;",
            "see_also": ["kw.Try", "kw.Endtry", "kw.Raise"],
        },
        "ru-RU": {
            "description": "Открывает ветку обработчика конструкции Попытка / Исключение. Выполняется, если защищённый блок породил исключение.",
            "syntax_block": "Попытка\n    // …\nИсключение\n    // восстановление\nКонецПопытки;",
            "see_also": ["kw.Try", "kw.Endtry", "kw.Raise"],
        },
        "uk-UA": {
            "description": "Відкриває гілку обробника конструкції Спроба / Виняток.",
            "syntax_block": "Спроба\n    // …\nВиняток\n    // відновлення\nКінецьСпроби;",
            "see_also": ["kw.Try", "kw.Endtry", "kw.Raise"],
        },
    },
    "kw.Endtry": {
        "en-US": {
            "description": "Closes a Try / Except construct.",
            "syntax_block": "Try … Except … EndTry;",
            "see_also": ["kw.Try", "kw.Except"],
        },
        "ru-RU": {
            "description": "Закрывает конструкцию Попытка / Исключение.",
            "syntax_block": "Попытка … Исключение … КонецПопытки;",
            "see_also": ["kw.Try", "kw.Except"],
        },
        "uk-UA": {
            "description": "Закриває конструкцію Спроба / Виняток.",
            "syntax_block": "Спроба … Виняток … КінецьСпроби;",
            "see_also": ["kw.Try", "kw.Except"],
        },
    },
    "kw.Raise": {
        "en-US": {
            "description": "Raises an exception. Bare form re-throws the current exception (only valid inside Except). With an expression — raises a new exception carrying that text.",
            "syntax_block": "Raise;                       // rethrow current exception\nRaise <ExpressionString>;    // raise a new exception",
            "example": "If Amount < 0 Then\n    Raise \"Negative amount is not allowed\";\nEndIf;",
            "see_also": ["kw.Try", "kw.Except"],
        },
        "ru-RU": {
            "description": "Возбуждает исключение. Без аргумента — перевозбуждает текущее (допустимо только внутри Исключение). С выражением — создаёт новое исключение с этим текстом.",
            "syntax_block": "ВызватьИсключение;             // перевозбудить текущее\nВызватьИсключение <ВыражениеСтрока>;  // новое исключение",
            "example": "Если Сумма < 0 Тогда\n    ВызватьИсключение \"Отрицательная сумма недопустима\";\nКонецЕсли;",
            "see_also": ["kw.Try", "kw.Except"],
        },
        "uk-UA": {
            "description": "Викликає виняток. Без аргументу — перепороджує поточне (лише всередині Виняток).",
            "syntax_block": "ВикликатиВиняток;\nВикликатиВиняток <Рядок>;",
            "example": "Якщо Сума < 0 Тоді\n    ВикликатиВиняток \"Відʼємна сума недопустима\";\nКінецьЯкщо;",
            "see_also": ["kw.Try", "kw.Except"],
        },
    },
    "kw.New": {
        "en-US": {
            "description": "Constructs a value of a built-in type (collections, primitive wrappers, query/stream readers). Type name follows New; constructor arguments in parentheses if required.",
            "syntax_block": "<Var> = New <TypeName>(<Args>…);",
            "example": "Items = New Array;\nItems.Add(\"first\");\nItems.Add(\"second\");\n\nQuery = New Query();\nQuery.Text = \"SELECT * FROM Catalog.Items\";",
            "see_also": ["kw.Undefined"],
        },
        "ru-RU": {
            "description": "Создаёт значение встроенного типа (коллекции, обёртки примитивов, читатели запросов / потоков). Имя типа идёт после Новый; аргументы конструктора в скобках, если нужны.",
            "syntax_block": "<Перем> = Новый <ИмяТипа>(<Аргументы>…);",
            "example": "Элементы = Новый Массив;\nЭлементы.Добавить(\"первый\");\nЭлементы.Добавить(\"второй\");\n\nЗапрос = Новый Запрос();\nЗапрос.Текст = \"ВЫБРАТЬ * ИЗ Справочник.Товары\";",
            "see_also": ["kw.Undefined"],
        },
        "uk-UA": {
            "description": "Створює значення вбудованого типу (колекції, обгортки примітивів, читачі запитів / потоків).",
            "syntax_block": "<Змін> = Новий <ІмʼяТипу>(<Аргументи>…);",
            "example": "Елементи = Новий Масив;\nЕлементи.Додати(\"перший\");\nЕлементи.Додати(\"другий\");",
            "see_also": ["kw.Undefined"],
        },
    },
    "kw.Undefined": {
        "en-US": {
            "description": "Singleton value indicating \"no value here yet\". Initial value of every declared but unassigned variable; also the natural result of methods that have nothing meaningful to return.",
            "example": "Var X;\nIf X = Undefined Then\n    X = ComputeDefault();\nEndIf;",
            "see_also": ["kw.Null", "kw.New", "kw.Var"],
        },
        "ru-RU": {
            "description": "Сингл-значение «значения ещё нет». Начальное значение любой объявленной, но не инициализированной переменной; естественный результат методов, которым нечего вернуть.",
            "example": "Перем Х;\nЕсли Х = Неопределено Тогда\n    Х = ВычислитьПоУмолчанию();\nКонецЕсли;",
            "see_also": ["kw.Null", "kw.New", "kw.Var"],
        },
        "uk-UA": {
            "description": "Сингл-значення «ще немає значення».",
            "example": "Змінна Х;\nЯкщо Х = Невизначено Тоді\n    Х = ОбчислитиЗаЗамовчуванням();\nКінецьЯкщо;",
            "see_also": ["kw.Null", "kw.New", "kw.Var"],
        },
    },
    "kw.Null": {
        "en-US": {
            "description": "Database-flavoured \"no value\" used by query result fields where a SQL NULL is returned. Distinct from Undefined; comparisons with regular values are always false.",
            "example": "If Selection.Manager = Null Then\n    ManagerName = \"<unassigned>\";\nEndIf;",
            "see_also": ["kw.Undefined"],
        },
        "ru-RU": {
            "description": "Значение «нет значения» из базы данных — то, что возвращают поля результата запроса при SQL NULL. Отличается от Неопределено; сравнения с обычными значениями всегда дают Ложь.",
            "example": "Если Выборка.Менеджер = Null Тогда\n    ИмяМенеджера = \"<не назначен>\";\nКонецЕсли;",
            "see_also": ["kw.Undefined"],
        },
        "uk-UA": {
            "description": "Значення «немає» з результатів запиту до БД (SQL NULL).",
            "example": "Якщо Вибірка.Менеджер = Null Тоді\n    ІмʼяМенеджера = \"<не призначений>\";\nКінецьЯкщо;",
            "see_also": ["kw.Undefined"],
        },
    },
    "kw.True": {
        "en-US": {
            "description": "Boolean literal — the true value.",
            "syntax_block": "Flag = True;",
            "see_also": ["kw.False", "kw.And", "kw.Or", "kw.Not"],
        },
        "ru-RU": {
            "description": "Логический литерал — значение «истина».",
            "syntax_block": "Флаг = Истина;",
            "see_also": ["kw.False", "kw.And", "kw.Or", "kw.Not"],
        },
        "uk-UA": {
            "description": "Логічний літерал — істина.",
            "syntax_block": "Прапорець = Істина;",
            "see_also": ["kw.False", "kw.And", "kw.Or", "kw.Not"],
        },
    },
    "kw.False": {
        "en-US": {
            "description": "Boolean literal — the false value.",
            "syntax_block": "Flag = False;",
            "see_also": ["kw.True", "kw.And", "kw.Or", "kw.Not"],
        },
        "ru-RU": {
            "description": "Логический литерал — значение «ложь».",
            "syntax_block": "Флаг = Ложь;",
            "see_also": ["kw.True", "kw.And", "kw.Or", "kw.Not"],
        },
        "uk-UA": {
            "description": "Логічний літерал — хибність.",
            "syntax_block": "Прапорець = Хибність;",
            "see_also": ["kw.True", "kw.And", "kw.Or", "kw.Not"],
        },
    },
    "kw.And": {
        "en-US": {
            "description": "Logical conjunction. Short-circuits: the right operand is not evaluated when the left is false.",
            "syntax_block": "<LeftExpr> And <RightExpr>",
            "example": "If User.IsLoggedIn And User.Role = \"admin\" Then\n    ShowAdminPanel();\nEndIf;",
            "see_also": ["kw.Or", "kw.Not"],
        },
        "ru-RU": {
            "description": "Логическое И. Короткое замыкание: правый операнд не вычисляется, если левый — Ложь.",
            "syntax_block": "<ЛевоеВыражение> И <ПравоеВыражение>",
            "example": "Если Пользователь.ВошёлВСистему И Пользователь.Роль = \"админ\" Тогда\n    ПоказатьПанельАдмина();\nКонецЕсли;",
            "see_also": ["kw.Or", "kw.Not"],
        },
        "uk-UA": {
            "description": "Логічне І. Коротке замикання.",
            "syntax_block": "<ЛівийВираз> І <ПравийВираз>",
            "see_also": ["kw.Or", "kw.Not"],
        },
    },
    "kw.Or": {
        "en-US": {
            "description": "Logical disjunction. Short-circuits: the right operand is not evaluated when the left is true.",
            "syntax_block": "<LeftExpr> Or <RightExpr>",
            "see_also": ["kw.And", "kw.Not"],
        },
        "ru-RU": {
            "description": "Логическое ИЛИ. Короткое замыкание: правый операнд не вычисляется, если левый — Истина.",
            "syntax_block": "<ЛевоеВыражение> Или <ПравоеВыражение>",
            "see_also": ["kw.And", "kw.Not"],
        },
        "uk-UA": {
            "description": "Логічне АБО. Коротке замикання.",
            "syntax_block": "<ЛівийВираз> Або <ПравийВираз>",
            "see_also": ["kw.And", "kw.Not"],
        },
    },
    "kw.Not": {
        "en-US": {
            "description": "Logical negation.",
            "syntax_block": "Not <Expression>",
            "example": "If Not User.IsLoggedIn Then\n    ShowLoginForm();\nEndIf;",
            "see_also": ["kw.And", "kw.Or"],
        },
        "ru-RU": {
            "description": "Логическое отрицание.",
            "syntax_block": "Не <Выражение>",
            "example": "Если Не Пользователь.ВошёлВСистему Тогда\n    ПоказатьФормуВхода();\nКонецЕсли;",
            "see_also": ["kw.And", "kw.Or"],
        },
        "uk-UA": {
            "description": "Логічне заперечення.",
            "syntax_block": "Не <Вираз>",
            "see_also": ["kw.And", "kw.Or"],
        },
    },
    "kw.GoTo": {
        "en-US": {
            "description": "Unconditional jump to a labelled statement within the same scope. Use sparingly — almost always a loop / function refactor is clearer.",
            "syntax_block": "GoTo ~<Label>;\n…\n~<Label>:",
            "see_also": [],
        },
        "ru-RU": {
            "description": "Безусловный переход к помеченному оператору в пределах того же блока. Применяйте умеренно — почти всегда чище переписать через цикл или функцию.",
            "syntax_block": "Перейти ~<Метка>;\n…\n~<Метка>:",
            "see_also": [],
        },
        "uk-UA": {
            "description": "Безумовний перехід до позначеного оператора в межах того ж блоку.",
            "syntax_block": "Перейти ~<Мітка>;\n…\n~<Мітка>:",
            "see_also": [],
        },
    },
}

# -------------------------------------------------------------------- helpers


def merge_entry(entry: dict[str, Any], content: dict[str, Any]) -> bool:
    """Merge content fields into entry; return True if anything changed."""
    changed = False
    for key in ("description", "syntax_block", "parameters", "return_descr",
                "example", "see_also", "name_local"):
        if key in content and entry.get(key) != content[key]:
            entry[key] = content[key]
            changed = True
    # Constant fields.
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
        if eid not in KW:
            continue
        seen += 1
        content = dict(KW[eid].get(locale, {}))
        # Auto-populate name_local from LOCAL_NAME when locale is non-English
        # and the entry didn't ship an explicit name_local override.
        if locale != "en-US" and eid in LOCAL_NAME and "name_local" not in content:
            ru, uk = LOCAL_NAME[eid]
            content["name_local"] = ru if locale == "ru-RU" else uk
        if not content:
            continue
        if merge_entry(entry, content):
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
