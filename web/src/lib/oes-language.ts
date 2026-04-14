/**
 * OES scripting language definition for Monaco Editor.
 * Registers the language, tokenizer, completion provider, and a dark theme.
 */
import type * as MonacoNS from "monaco-editor"

// ---------------------------------------------------------------------------
// Keywords — 44 total (from codeDef.h KEY_* enumerators)
// ---------------------------------------------------------------------------
export const OES_KEYWORDS: string[] = [
  "If", "Then", "Else", "ElseIf", "EndIf",
  "For", "Each", "In", "To", "Do", "EndDo",
  "While", "EndWhile",
  "Procedure", "EndProcedure",
  "Function", "EndFunction",
  "Var", "Return", "Continue", "Break",
  "Try", "Except", "EndTry", "Raise",
  "New", "Execute", "Export",
  "And", "Or", "Not",
  "True", "False", "Undefined", "Null",
  "Goto", "Label",
  "AddHandler", "RemoveHandler",
  "Async", "Await",
  "Import", "Module",
]

// ---------------------------------------------------------------------------
// Built-in functions — 92 (from SystemManager.cpp)
// ---------------------------------------------------------------------------
export const OES_BUILTINS: string[] = [
  // Type constructors / converters
  "Boolean", "Number", "Date", "String",
  // Math
  "Round", "Int", "Log10", "Ln", "Max", "Min", "Sqrt", "Abs", "Pow", "Exp",
  "Sin", "Cos", "Tan", "ASin", "ACos", "ATan",
  // String
  "StrLen", "Left", "Right", "Mid", "Find", "Upper", "Lower",
  "TrimAll", "TrimLeft", "TrimRight", "StrReplace", "StrSplit", "StrConcat",
  "CharCode", "Char", "Format",
  // Date / Time
  "CurrentDate", "CurrentTime", "CurrentDateTime",
  "AddMonth", "AddDay", "AddHour", "AddMinute", "AddSecond",
  "BegOfMonth", "EndOfMonth", "BegOfYear", "EndOfYear",
  "BegOfWeek", "EndOfWeek", "BegOfDay", "EndOfDay",
  "GetYear", "GetMonth", "GetDay", "GetHour", "GetMinute", "GetSecond",
  "DiffDate",
  // UI / Interaction
  "Message", "Alert", "Question", "OpenForm", "CloseForm",
  // DB / Transactions
  "BeginTransaction", "CommitTransaction", "RollbackTransaction",
  "LockData", "UnlockData",
  // Type system
  "Type", "TypeOf", "IsNull", "IsRef", "IsObject",
  // Value helpers
  "ValueIsFilled", "FillAttribute",
  // Collection helpers
  "New", "Delete", "Count", "Add", "Get", "Set", "Clear",
  // Misc
  "Sleep", "Evaluate", "GetCommonModule",
]

// ---------------------------------------------------------------------------
// Monaco language registration
// ---------------------------------------------------------------------------
export function registerOesLanguage(monaco: typeof MonacoNS) {
  const LANG_ID = "oes-script"

  // Only register once
  const existingLangs = monaco.languages.getLanguages()
  if (existingLangs.some((l) => l.id === LANG_ID)) return LANG_ID

  monaco.languages.register({ id: LANG_ID, extensions: [".oes"], aliases: ["OES Script"] })

  // Tokenizer
  monaco.languages.setMonarchTokensProvider(LANG_ID, {
    ignoreCase: true,
    keywords: OES_KEYWORDS,
    builtins: OES_BUILTINS,
    operators: ["=", "<>", "<", ">", "<=", ">=", "+", "-", "*", "/", "%", "^"],
    tokenizer: {
      root: [
        // Line comments
        [/\/\/.*$/, "comment"],
        // Block comments
        [/\/\*/, "comment", "@comment"],
        // Strings
        [/"([^"\\]|\\.)*$/, "string.invalid"],
        [/"/, "string", "@string_double"],
        // Numbers
        [/\d+(\.\d+)?([eE][+-]?\d+)?/, "number"],
        // Identifiers / keywords / builtins
        [/[a-zA-Z_]\w*/, {
          cases: {
            "@keywords": "keyword",
            "@builtins": "support.function",
            "@default": "identifier",
          },
        }],
        // Operators
        [/[=<>!+\-*/%^]+/, "operator"],
        // Delimiters
        [/[();,.]/, "delimiter"],
      ],
      comment: [
        [/[^/*]+/, "comment"],
        [/\*\//, "comment", "@pop"],
        [/[/*]/, "comment"],
      ],
      string_double: [
        [/[^\\"]+/, "string"],
        [/\\./, "string.escape"],
        [/"/, "string", "@pop"],
      ],
    },
  } as MonacoNS.languages.IMonarchLanguage)

  // Language configuration (brackets, auto-closing, etc.)
  monaco.languages.setLanguageConfiguration(LANG_ID, {
    brackets: [
      ["(", ")"],
      ["[", "]"],
    ],
    autoClosingPairs: [
      { open: "(", close: ")" },
      { open: "[", close: "]" },
      { open: '"', close: '"' },
    ],
    surroundingPairs: [
      { open: "(", close: ")" },
      { open: '"', close: '"' },
    ],
    comments: {
      lineComment: "//",
      blockComment: ["/*", "*/"],
    },
    folding: {
      markers: {
        start: /^\s*(Procedure|Function|If|For|While|Try)\b/i,
        end: /^\s*(EndProcedure|EndFunction|EndIf|EndDo|EndWhile|EndTry)\b/i,
      },
    },
  })

  // Completion provider
  monaco.languages.registerCompletionItemProvider(LANG_ID, {
    provideCompletionItems(model, position) {
      const word = model.getWordUntilPosition(position)
      const range: MonacoNS.IRange = {
        startLineNumber: position.lineNumber,
        endLineNumber: position.lineNumber,
        startColumn: word.startColumn,
        endColumn: word.endColumn,
      }

      const keywordItems: MonacoNS.languages.CompletionItem[] = OES_KEYWORDS.map((kw) => ({
        label: kw,
        kind: monaco.languages.CompletionItemKind.Keyword,
        insertText: kw,
        range,
      }))

      const builtinItems: MonacoNS.languages.CompletionItem[] = OES_BUILTINS.map((fn) => ({
        label: fn,
        kind: monaco.languages.CompletionItemKind.Function,
        insertText: fn + "(${1})",
        insertTextRules: monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet,
        detail: "Built-in function",
        range,
      }))

      return { suggestions: [...keywordItems, ...builtinItems] }
    },
  })

  return LANG_ID
}

// ---------------------------------------------------------------------------
// OES dark theme for Monaco
// ---------------------------------------------------------------------------
export function registerOesTheme(monaco: typeof MonacoNS) {
  monaco.editor.defineTheme("oes-dark", {
    base: "vs-dark",
    inherit: true,
    rules: [
      { token: "keyword",          foreground: "569cd6", fontStyle: "bold" },
      { token: "support.function", foreground: "dcdcaa" },
      { token: "identifier",       foreground: "9cdcfe" },
      { token: "string",           foreground: "ce9178" },
      { token: "string.escape",    foreground: "d7ba7d" },
      { token: "number",           foreground: "b5cea8" },
      { token: "comment",          foreground: "6a9955", fontStyle: "italic" },
      { token: "operator",         foreground: "d4d4d4" },
      { token: "delimiter",        foreground: "d4d4d4" },
    ],
    colors: {
      "editor.background":           "#0f1117",
      "editor.foreground":           "#d4d4d4",
      "editor.lineHighlightBackground": "#1a1d27",
      "editorGutter.background":     "#0f1117",
      "editorLineNumber.foreground": "#3c4052",
      "editorLineNumber.activeForeground": "#858999",
      "editor.selectionBackground":  "#264f78",
      "editorIndentGuide.background1": "#1e2130",
    },
  })
}

export const OES_LANG_ID = "oes-script"
