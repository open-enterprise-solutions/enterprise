import { useEffect, useRef, useCallback, lazy, Suspense } from "react"
import type * as MonacoNS from "monaco-editor"
import { X, Circle } from "@phosphor-icons/react"
import type { CompileError } from "./CompileConsole"

// Lazy-load Monaco — ~2 MB bundle
const MonacoEditor = lazy(() => import("@monaco-editor/react"))

// ---------------------------------------------------------------------------
// Tab types
// ---------------------------------------------------------------------------

export interface EditorTab {
  moduleId: string
  title: string
  /** Object display name (used in tooltip) */
  objectName: string
  code: string
  /** Unsaved local changes */
  dirty: boolean
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

interface CodeEditorProps {
  tabs: EditorTab[]
  activeTabId: string | null
  onTabSelect: (id: string) => void
  onTabClose: (id: string) => void
  onChange: (moduleId: string, code: string) => void
  onSave: () => void
  onCompile: () => void
  compileErrors: CompileError[]
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function CodeEditor({
  tabs,
  activeTabId,
  onTabSelect,
  onTabClose,
  onChange,
  onSave,
  onCompile,
  compileErrors,
}: CodeEditorProps) {
  const monacoRef = useRef<typeof MonacoNS | null>(null)
  const editorRef = useRef<MonacoNS.editor.IStandaloneCodeEditor | null>(null)

  const activeTab = tabs.find((t) => t.moduleId === activeTabId)

  // Apply compile error markers whenever errors or active tab change
  useEffect(() => {
    const monaco = monacoRef.current
    const editor = editorRef.current
    if (!monaco || !editor) return

    const model = editor.getModel()
    if (!model) return

    const markers: MonacoNS.editor.IMarkerData[] = compileErrors.map((e) => ({
      severity:
        e.severity === "error"
          ? monaco.MarkerSeverity.Error
          : e.severity === "warning"
          ? monaco.MarkerSeverity.Warning
          : monaco.MarkerSeverity.Info,
      startLineNumber: e.line,
      endLineNumber: e.line,
      startColumn: e.column,
      endColumn: e.column + 1,
      message: e.message,
    }))

    monaco.editor.setModelMarkers(model, "oes-compile", markers)
  }, [compileErrors, activeTabId])

  // Handle editor mount
  const handleEditorDidMount = useCallback(
    (editor: MonacoNS.editor.IStandaloneCodeEditor, monaco: typeof MonacoNS) => {
      editorRef.current = editor
      monacoRef.current = monaco

      // Register OES language and theme
      import("@/lib/oes-language").then(({ registerOesLanguage, registerOesTheme }) => {
        registerOesLanguage(monaco)
        registerOesTheme(monaco)
        monaco.editor.setTheme("oes-dark")

        // Re-set model language after registration
        const model = editor.getModel()
        if (model) {
          monaco.editor.setModelLanguage(model, "oes-script")
        }
      })

      // Keyboard shortcuts
      editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => onSave())
      editor.addCommand(monaco.KeyCode.F7, () => onCompile())
    },
    [onSave, onCompile],
  )

  // When the active tab changes, update Monaco model content if needed
  useEffect(() => {
    const editor = editorRef.current
    if (!editor || !activeTab) return
    const current = editor.getValue()
    if (current !== activeTab.code) {
      editor.setValue(activeTab.code)
      // Clear undo stack for the new file
      editor.getModel()?.onDidChangeContent
    }
  }, [activeTab?.moduleId]) // Only when tab switches, not on every keystroke

  if (tabs.length === 0) {
    return (
      <div className="flex flex-1 items-center justify-center text-[12px] text-[hsl(var(--muted-foreground))]">
        Double-click a module in the tree to open it.
      </div>
    )
  }

  return (
    <div className="flex flex-1 flex-col min-h-0">
      {/* Tab bar */}
      <div className="flex h-8 shrink-0 items-end gap-0 overflow-x-auto border-b border-[hsl(var(--border))] bg-[hsl(var(--sidebar))]">
        {tabs.map((tab) => (
          <EditorTabButton
            key={tab.moduleId}
            tab={tab}
            active={tab.moduleId === activeTabId}
            onSelect={() => onTabSelect(tab.moduleId)}
            onClose={() => onTabClose(tab.moduleId)}
          />
        ))}
      </div>

      {/* Monaco editor */}
      <div className="flex-1 min-h-0">
        <Suspense
          fallback={
            <div className="flex h-full items-center justify-center text-[11px] text-[hsl(var(--muted-foreground))]">
              Loading editor...
            </div>
          }
        >
          <MonacoEditor
            height="100%"
            language="oes-script"
            theme="oes-dark"
            value={activeTab?.code ?? ""}
            onChange={(v) => {
              if (activeTabId) onChange(activeTabId, v ?? "")
            }}
            onMount={handleEditorDidMount}
            options={{
              fontSize: 13,
              fontFamily: "'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace",
              fontLigatures: true,
              lineNumbers: "on",
              minimap: { enabled: true },
              scrollBeyondLastLine: false,
              wordWrap: "off",
              tabSize: 2,
              insertSpaces: true,
              renderLineHighlight: "all",
              bracketPairColorization: { enabled: true },
              folding: true,
              showFoldingControls: "mouseover",
              glyphMargin: true,
              // Breakpoint gutter preparation
              lineNumbersMinChars: 4,
              renderWhitespace: "none",
              padding: { top: 8 },
            }}
          />
        </Suspense>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Tab button
// ---------------------------------------------------------------------------

interface EditorTabButtonProps {
  tab: EditorTab
  active: boolean
  onSelect: () => void
  onClose: () => void
}

function EditorTabButton({ tab, active, onSelect, onClose }: EditorTabButtonProps) {
  return (
    <div
      role="tab"
      aria-selected={active}
      title={tab.objectName}
      onClick={onSelect}
      className={[
        "group flex h-full cursor-pointer items-center gap-1.5 border-r border-[hsl(var(--border))] px-3 text-[11px] transition-colors select-none shrink-0",
        active
          ? "bg-[hsl(var(--background))] text-[hsl(var(--foreground))] border-t-2 border-t-[hsl(var(--primary))]"
          : "bg-[hsl(var(--sidebar))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
      ].join(" ")}
    >
      {tab.dirty && (
        <Circle size={6} weight="fill" className="text-[hsl(var(--primary))] shrink-0" />
      )}
      <span className="max-w-[160px] truncate">{tab.title}</span>
      <button
        type="button"
        onClick={(e) => {
          e.stopPropagation()
          onClose()
        }}
        className={[
          "flex h-4 w-4 items-center justify-center rounded-[2px] transition-colors",
          "opacity-0 group-hover:opacity-100",
          active ? "opacity-60 hover:opacity-100" : "",
          "hover:bg-[hsl(var(--secondary))]",
        ].join(" ")}
      >
        <X size={9} weight="bold" />
      </button>
    </div>
  )
}
