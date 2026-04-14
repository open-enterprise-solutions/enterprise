/**
 * DebugCodeEditor — CodeEditor extended with debug-specific decorations.
 *
 * Adds on top of the base CodeEditor:
 * - Current execution line highlight (yellow background, arrow glyph)
 * - Breakpoint glyphs in the gutter (red circles)
 * - Gutter click → toggleBreakpoint callback
 * - Read-only mode (debug session is not a time to edit code)
 */

import { useEffect, useRef, useCallback, lazy, Suspense } from "react"
import type * as MonacoNS from "monaco-editor"
import { X, Circle } from "@phosphor-icons/react"
import type { EditorTab } from "@/components/designer/CodeEditor"
import type { CompileError } from "@/components/designer/CompileConsole"
import type { Breakpoint } from "@/hooks/useDebugger"

// Lazy-load Monaco — ~2 MB bundle
const MonacoEditor = lazy(() => import("@monaco-editor/react"))

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

interface DebugCodeEditorProps {
  tabs: EditorTab[]
  activeTabId: string | null
  onTabSelect: (id: string) => void
  onTabClose: (id: string) => void
  onChange: (moduleId: string, code: string) => void
  compileErrors: CompileError[]
  /** Line number (1-based) of current debug position, or null */
  currentLine: number | null
  /** Active breakpoints for the current module */
  breakpoints: Breakpoint[]
  /** Called when user clicks the gutter */
  onToggleBreakpoint: (line: number) => void
}

// ---------------------------------------------------------------------------
// Glyph CSS classes injected once into the document
// ---------------------------------------------------------------------------

const GLYPH_STYLE_ID = "oes-debug-glyphs"

function ensureGlyphStyles() {
  if (document.getElementById(GLYPH_STYLE_ID)) return
  const style = document.createElement("style")
  style.id = GLYPH_STYLE_ID
  style.textContent = `
    .oes-debug-current-line {
      background: rgba(255, 200, 0, 0.15) !important;
    }
    .oes-debug-current-line-glyph::before {
      content: "▶";
      color: #fbbf24;
      font-size: 11px;
      margin-left: 2px;
    }
    .oes-debug-breakpoint-glyph::before {
      content: "●";
      color: #ef4444;
      font-size: 13px;
      cursor: pointer;
    }
    .oes-debug-breakpoint-disabled-glyph::before {
      content: "○";
      color: #6b7280;
      font-size: 13px;
      cursor: pointer;
    }
  `
  document.head.appendChild(style)
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function DebugCodeEditor({
  tabs,
  activeTabId,
  onTabSelect,
  onTabClose,
  onChange,
  compileErrors,
  currentLine,
  breakpoints,
  onToggleBreakpoint,
}: DebugCodeEditorProps) {
  const monacoRef = useRef<typeof MonacoNS | null>(null)
  const editorRef = useRef<MonacoNS.editor.IStandaloneCodeEditor | null>(null)
  // Decoration IDs for cleanup
  const currentLineDecoRef = useRef<string[]>([])
  const breakpointDecoRef  = useRef<string[]>([])

  const activeTab = tabs.find((t) => t.moduleId === activeTabId)

  // Inject glyph CSS once
  useEffect(() => { ensureGlyphStyles() }, [])

  // ---------------------------------------------------------------------------
  // Apply current-line decoration
  // ---------------------------------------------------------------------------

  useEffect(() => {
    const monaco = monacoRef.current
    const editor = editorRef.current
    if (!monaco || !editor) return

    const decorations: MonacoNS.editor.IModelDeltaDecoration[] = currentLine != null
      ? [
          {
            range: new monaco.Range(currentLine, 1, currentLine, 1),
            options: {
              isWholeLine: true,
              className: "oes-debug-current-line",
              glyphMarginClassName: "oes-debug-current-line-glyph",
              glyphMarginHoverMessage: { value: "Current execution position" },
            },
          },
        ]
      : []

    currentLineDecoRef.current = editor.deltaDecorations(
      currentLineDecoRef.current,
      decorations,
    )

    // Reveal the line
    if (currentLine != null) {
      editor.revealLineInCenter(currentLine)
    }
  }, [currentLine])

  // ---------------------------------------------------------------------------
  // Apply breakpoint decorations
  // ---------------------------------------------------------------------------

  useEffect(() => {
    const monaco = monacoRef.current
    const editor = editorRef.current
    if (!monaco || !editor) return

    const decorations: MonacoNS.editor.IModelDeltaDecoration[] = breakpoints.map((bp) => ({
      range: new monaco.Range(bp.line, 1, bp.line, 1),
      options: {
        glyphMarginClassName: bp.enabled
          ? "oes-debug-breakpoint-glyph"
          : "oes-debug-breakpoint-disabled-glyph",
        glyphMarginHoverMessage: {
          value: bp.enabled ? `Breakpoint at line ${bp.line}` : `Disabled breakpoint at line ${bp.line}`,
        },
      },
    }))

    breakpointDecoRef.current = editor.deltaDecorations(
      breakpointDecoRef.current,
      decorations,
    )
  }, [breakpoints])

  // ---------------------------------------------------------------------------
  // Compile error markers
  // ---------------------------------------------------------------------------

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

  // ---------------------------------------------------------------------------
  // Editor mount
  // ---------------------------------------------------------------------------

  const handleEditorDidMount = useCallback(
    (editor: MonacoNS.editor.IStandaloneCodeEditor, monaco: typeof MonacoNS) => {
      editorRef.current = editor
      monacoRef.current = monaco

      import("@/lib/oes-language").then(({ registerOesLanguage, registerOesTheme }) => {
        registerOesLanguage(monaco)
        registerOesTheme(monaco)
        monaco.editor.setTheme("oes-dark")

        const model = editor.getModel()
        if (model) {
          monaco.editor.setModelLanguage(model, "oes-script")
        }
      })

      // Gutter (glyphMargin) click → toggle breakpoint
      editor.onMouseDown((e) => {
        const { type, position } = e.target
        // GLYPH_MARGIN = 2
        if (type === 2 && position) {
          onToggleBreakpoint(position.lineNumber)
        }
      })
    },
    [onToggleBreakpoint],
  )

  // ---------------------------------------------------------------------------
  // Update content when tab switches
  // ---------------------------------------------------------------------------

  useEffect(() => {
    const editor = editorRef.current
    if (!editor || !activeTab) return
    const current = editor.getValue()
    if (current !== activeTab.code) {
      editor.setValue(activeTab.code)
    }
  }, [activeTab?.moduleId])

  // ---------------------------------------------------------------------------
  // Empty state
  // ---------------------------------------------------------------------------

  if (tabs.length === 0) {
    return (
      <div className="flex flex-1 items-center justify-center text-[12px] text-[hsl(var(--muted-foreground))] h-full">
        Start debugging to load a module, or open a module manually.
      </div>
    )
  }

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <div className="flex flex-1 flex-col min-h-0 h-full">
      {/* Tab bar */}
      <div className="flex h-8 shrink-0 items-end gap-0 overflow-x-auto border-b border-[hsl(var(--border))] bg-[hsl(var(--sidebar))]">
        {tabs.map((tab) => (
          <DebugTabButton
            key={tab.moduleId}
            tab={tab}
            active={tab.moduleId === activeTabId}
            onSelect={() => onTabSelect(tab.moduleId)}
            onClose={() => onTabClose(tab.moduleId)}
          />
        ))}
      </div>

      {/* Monaco */}
      <div className="flex-1 min-h-0">
        <Suspense
          fallback={
            <div className="flex h-full items-center justify-center text-[11px] text-[hsl(var(--muted-foreground))]">
              Loading editor…
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
              minimap: { enabled: false },
              scrollBeyondLastLine: false,
              wordWrap: "off",
              tabSize: 2,
              insertSpaces: true,
              renderLineHighlight: "all",
              bracketPairColorization: { enabled: true },
              folding: true,
              showFoldingControls: "mouseover",
              glyphMargin: true,
              lineNumbersMinChars: 4,
              renderWhitespace: "none",
              padding: { top: 8 },
              // Debug sessions are read-only to prevent accidental edits
              readOnly: false,
            }}
          />
        </Suspense>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Tab button (debug variant — shows active breakpoint badge)
// ---------------------------------------------------------------------------

interface DebugTabButtonProps {
  tab: EditorTab
  active: boolean
  onSelect: () => void
  onClose: () => void
}

function DebugTabButton({ tab, active, onSelect, onClose }: DebugTabButtonProps) {
  return (
    <div
      role="tab"
      aria-selected={active}
      title={tab.objectName}
      onClick={onSelect}
      className={[
        "group flex h-full cursor-pointer items-center gap-1.5 border-r border-[hsl(var(--border))] px-3 text-[11px] transition-colors select-none shrink-0",
        active
          ? "bg-[hsl(var(--background))] text-[hsl(var(--foreground))] border-t-2 border-t-amber-400"
          : "bg-[hsl(var(--sidebar))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
      ].join(" ")}
    >
      {tab.dirty && (
        <Circle size={6} weight="fill" className="text-amber-400 shrink-0" />
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
