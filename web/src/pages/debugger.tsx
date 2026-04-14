/**
 * Phase 8 — OES Web Debugger Page
 *
 * Layout:
 *   [Header + DebugToolbar]
 *   [Left panels 260px | Center: CodeEditor | Right is Center]
 *   [Bottom: DebugConsole 200px]
 *
 * Left panel has two tabs: Variables | Call Stack
 *
 * Keyboard shortcuts (global):
 *   F5           Start / Continue
 *   Shift+F5     Stop
 *   F10          Step Over
 *   F11          Step Into
 *   Shift+F11    Step Out
 */

import { useCallback, useReducer, useRef, useState } from "react"
import { useNavigate } from "react-router-dom"
import { ArrowLeft } from "@phosphor-icons/react"

import { DebugToolbar } from "@/components/debugger/DebugToolbar"
import { VariablesPanel } from "@/components/debugger/VariablesPanel"
import { CallStackPanel } from "@/components/debugger/CallStackPanel"
import { DebugConsole } from "@/components/debugger/DebugConsole"
import { DebugCodeEditor } from "@/components/debugger/DebugCodeEditor"

import {
  useDebugStore,
  useDebuggerPolling,
  type StackFrame,
} from "@/hooks/useDebugger"
import type { EditorTab } from "@/components/designer/CodeEditor"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Editor state (minimal — only one file open at a time in debug mode)
// ---------------------------------------------------------------------------

interface DebugEditorState {
  tabs: EditorTab[]
  activeTabId: string | null
}

type DebugEditorAction =
  | { type: "OPEN_TAB"; tab: EditorTab }
  | { type: "SELECT_TAB"; moduleId: string }
  | { type: "CLOSE_TAB"; moduleId: string }
  | { type: "UPDATE_CODE"; moduleId: string; code: string }

function editorReducer(state: DebugEditorState, action: DebugEditorAction): DebugEditorState {
  switch (action.type) {
    case "OPEN_TAB": {
      const exists = state.tabs.find((t) => t.moduleId === action.tab.moduleId)
      return {
        ...state,
        tabs: exists ? state.tabs : [...state.tabs, action.tab],
        activeTabId: action.tab.moduleId,
      }
    }
    case "SELECT_TAB":
      return { ...state, activeTabId: action.moduleId }
    case "CLOSE_TAB": {
      const remaining = state.tabs.filter((t) => t.moduleId !== action.moduleId)
      const nextActive =
        state.activeTabId === action.moduleId
          ? (remaining.at(-1)?.moduleId ?? null)
          : state.activeTabId
      return { ...state, tabs: remaining, activeTabId: nextActive }
    }
    case "UPDATE_CODE": {
      return {
        ...state,
        tabs: state.tabs.map((t) =>
          t.moduleId === action.moduleId ? { ...t, code: action.code, dirty: true } : t,
        ),
      }
    }
    default:
      return state
  }
}

const INITIAL_EDITOR: DebugEditorState = { tabs: [], activeTabId: null }

// ---------------------------------------------------------------------------
// Left panel tab type
// ---------------------------------------------------------------------------

type LeftTab = "variables" | "callstack"

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

export function DebuggerPage() {
  const navigate = useNavigate()

  // Debugger store
  const status = useDebugStore((s) => s.status)
  const currentModule = useDebugStore((s) => s.currentModule)
  const currentLine = useDebugStore((s) => s.currentLine)
  const variables = useDebugStore((s) => s.variables)
  const callStack = useDebugStore((s) => s.callStack)
  const breakpoints = useDebugStore((s) => s.breakpoints)
  const output = useDebugStore((s) => s.output)
  const _addOutput = useDebugStore((s) => s._addOutput)

  const start = useDebugStore((s) => s.start)
  const stop = useDebugStore((s) => s.stop)
  const continueExec = useDebugStore((s) => s.continue)
  const stepOver = useDebugStore((s) => s.stepOver)
  const stepInto = useDebugStore((s) => s.stepInto)
  const stepOut = useDebugStore((s) => s.stepOut)
  const evaluate = useDebugStore((s) => s.evaluate)
  const toggleBreakpoint = useDebugStore((s) => s.toggleBreakpoint)
  const clearOutput = useCallback(
    () => useDebugStore.setState({ output: [] }),
    [],
  )

  // Start polling when active
  useDebuggerPolling()

  // Local editor state
  const [editorState, dispatchEditor] = useReducer(editorReducer, INITIAL_EDITOR)
  const tabsRef = useRef(editorState.tabs)
  tabsRef.current = editorState.tabs

  const [leftTab, setLeftTab] = useState<LeftTab>("variables")
  const [activeFrameIndex, setActiveFrameIndex] = useState(0)

  const isPaused = status === "paused"

  // ---------------------------------------------------------------------------
  // Load module into editor
  // ---------------------------------------------------------------------------

  const loadModule = useCallback(async (moduleId: string) => {
    const existing = tabsRef.current.find((t) => t.moduleId === moduleId)
    if (existing) {
      dispatchEditor({ type: "SELECT_TAB", moduleId })
      return
    }
    try {
      const { data } = await api.get<{ code: string }>(`/designer/module/${moduleId}`)
      dispatchEditor({
        type: "OPEN_TAB",
        tab: {
          moduleId,
          title: moduleId.split("/").pop() ?? moduleId,
          objectName: moduleId,
          code: data.code,
          dirty: false,
        },
      })
    } catch {
      _addOutput({ kind: "system", text: `Could not load module: ${moduleId}` })
    }
  }, [_addOutput])

  // When debugger pauses, auto-load the current module
  const prevModuleRef = useRef<string | null>(null)
  if (isPaused && currentModule && currentModule !== prevModuleRef.current) {
    prevModuleRef.current = currentModule
    void loadModule(currentModule)
  }
  if (!isPaused) {
    prevModuleRef.current = null
  }

  // ---------------------------------------------------------------------------
  // Frame selection
  // ---------------------------------------------------------------------------

  const handleFrameSelect = useCallback(
    (frame: StackFrame) => {
      setActiveFrameIndex(frame.index)
      void loadModule(frame.module)
    },
    [loadModule],
  )

  // ---------------------------------------------------------------------------
  // Breakpoint toggle (from editor gutter)
  // ---------------------------------------------------------------------------

  const handleToggleBreakpoint = useCallback(
    (moduleId: string, line: number) => {
      void toggleBreakpoint(moduleId, line)
    },
    [toggleBreakpoint],
  )

  // ---------------------------------------------------------------------------
  // Pause stub — sent as command; backend needs to handle it
  // ---------------------------------------------------------------------------

  const handlePause = useCallback(async () => {
    try {
      await api.post("/debug/command", { action: "pause" })
    } catch {
      // Best effort
    }
  }, [])

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  const currentModuleBreakpoints = breakpoints.filter(
    (b) => b.module === editorState.activeTabId,
  )

  return (
    <div className="flex h-screen flex-col overflow-hidden bg-[hsl(var(--background))] density-compact">

      {/* ------------------------------------------------------------------ */}
      {/* Header                                                               */}
      {/* ------------------------------------------------------------------ */}
      <header className="flex h-10 shrink-0 items-center gap-3 border-b border-[hsl(var(--border))] bg-[hsl(var(--sidebar))] px-3">
        <button
          type="button"
          onClick={() => navigate("/designer")}
          title="Back to Designer"
          className="flex h-7 items-center gap-1.5 rounded-[2px] border border-transparent px-2 text-[11px] font-medium text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))] hover:border-[hsl(var(--border))] transition-colors"
        >
          <ArrowLeft size={13} weight="duotone" />
          Designer
        </button>

        <div className="h-4 w-px bg-[hsl(var(--border))]" />

        <span className="text-[12px] font-semibold text-[hsl(var(--foreground))]">
          OES Debugger
        </span>

        <div className="flex-1" />

        {currentModule && isPaused && (
          <span className="text-[11px] text-[hsl(var(--muted-foreground))]">
            {currentModule}
            {currentLine != null && (
              <span className="ml-1 text-amber-400">:{currentLine}</span>
            )}
          </span>
        )}
      </header>

      {/* ------------------------------------------------------------------ */}
      {/* Debug toolbar                                                        */}
      {/* ------------------------------------------------------------------ */}
      <DebugToolbar
        status={status}
        onStart={start}
        onStop={stop}
        onContinue={continueExec}
        onPause={handlePause}
        onStepOver={stepOver}
        onStepInto={stepInto}
        onStepOut={stepOut}
      />

      {/* ------------------------------------------------------------------ */}
      {/* Main layout: left panel | editor                                     */}
      {/* ------------------------------------------------------------------ */}
      <div className="flex flex-1 min-h-0 overflow-hidden">

        {/* Left panel */}
        <aside className="w-64 shrink-0 flex flex-col border-r border-[hsl(var(--border))] bg-[hsl(var(--sidebar))] overflow-hidden">

          {/* Left panel tab bar */}
          <div className="flex h-7 shrink-0 border-b border-[hsl(var(--border))]">
            <LeftTabBtn
              label="Variables"
              active={leftTab === "variables"}
              onClick={() => setLeftTab("variables")}
            />
            <LeftTabBtn
              label="Call Stack"
              active={leftTab === "callstack"}
              onClick={() => setLeftTab("callstack")}
            />
          </div>

          {/* Left panel content */}
          <div className="flex-1 min-h-0 overflow-hidden">
            {leftTab === "variables" ? (
              <VariablesPanel
                variables={variables}
                isPaused={isPaused}
                onEvaluate={evaluate}
              />
            ) : (
              <CallStackPanel
                frames={callStack}
                activeFrameIndex={activeFrameIndex}
                onFrameSelect={handleFrameSelect}
              />
            )}
          </div>
        </aside>

        {/* Center: editor + console */}
        <div className="flex flex-1 flex-col min-w-0 overflow-hidden">

          {/* Editor area */}
          <div className="flex-1 min-h-0 overflow-hidden">
            <DebugCodeEditor
              tabs={editorState.tabs}
              activeTabId={editorState.activeTabId}
              onTabSelect={(id) => dispatchEditor({ type: "SELECT_TAB", moduleId: id })}
              onTabClose={(id) => dispatchEditor({ type: "CLOSE_TAB", moduleId: id })}
              onChange={(moduleId, code) => dispatchEditor({ type: "UPDATE_CODE", moduleId, code })}
              compileErrors={[]}
              currentLine={
                editorState.activeTabId === currentModule ? (currentLine ?? null) : null
              }
              breakpoints={currentModuleBreakpoints}
              onToggleBreakpoint={(line) =>
                handleToggleBreakpoint(editorState.activeTabId ?? "", line)
              }
            />
          </div>

          {/* Bottom: debug console */}
          <div className="h-48 shrink-0 border-t border-[hsl(var(--border))] bg-[hsl(var(--background))]">
            <DebugConsole
              output={output}
              isPaused={isPaused}
              onEvaluate={evaluate}
              onClear={clearOutput}
            />
          </div>
        </div>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Left panel tab button
// ---------------------------------------------------------------------------

interface LeftTabBtnProps {
  label: string
  active: boolean
  onClick: () => void
}

function LeftTabBtn({ label, active, onClick }: LeftTabBtnProps) {
  return (
    <button
      type="button"
      onClick={onClick}
      className={[
        "flex-1 text-[10px] font-semibold uppercase tracking-wide transition-colors px-2",
        active
          ? "text-[hsl(var(--foreground))] border-b-2 border-[hsl(var(--primary))]"
          : "text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]",
      ].join(" ")}
    >
      {label}
    </button>
  )
}
