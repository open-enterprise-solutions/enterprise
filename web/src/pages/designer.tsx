/**
 * Phase 7 — OES Web Designer (browser-based configurator, replaces designer.exe)
 *
 * Layout:
 *   [Left: MetadataTree 260px] | [Center: CodeEditor or PropertyGrid] | [Bottom: CompileConsole]
 *
 * Keyboard shortcuts:
 *   Ctrl+S        → save active module
 *   F7            → compile active module
 *   Ctrl+Shift+E  → export configuration JSON
 */

import { useEffect, useCallback, useReducer, useRef } from "react"
import { useNavigate } from "react-router-dom"
import { ArrowLeft } from "@phosphor-icons/react"
import { api } from "@/lib/axios"
import { MetadataTree, type DesignerMetaNode } from "@/components/designer/MetadataTree"
import { CodeEditor, type EditorTab } from "@/components/designer/CodeEditor"
import { PropertyGrid, type PropGroup, type PropValues } from "@/components/designer/PropertyGrid"
import { CompileConsole, type CompileError } from "@/components/designer/CompileConsole"
import { DesignerToolbar } from "@/components/designer/DesignerToolbar"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

interface DesignerState {
  tabs: EditorTab[]
  activeTabId: string | null
  compileErrors: CompileError[]
  consoleOpen: boolean
  compiling: boolean
  saving: boolean
  /** Currently selected node for PropertyGrid */
  selectedNode: DesignerMetaNode | null
  /** Whether central pane shows editor or property grid */
  centerPane: "editor" | "properties"
  propValues: PropValues
}

type DesignerAction =
  | { type: "OPEN_TAB"; tab: EditorTab }
  | { type: "CLOSE_TAB"; moduleId: string }
  | { type: "SELECT_TAB"; moduleId: string }
  | { type: "UPDATE_CODE"; moduleId: string; code: string }
  | { type: "MARK_SAVED"; moduleId: string; code: string }
  | { type: "SET_COMPILE_ERRORS"; errors: CompileError[] }
  | { type: "SET_COMPILING"; value: boolean }
  | { type: "SET_SAVING"; value: boolean }
  | { type: "TOGGLE_CONSOLE" }
  | { type: "SELECT_NODE"; node: DesignerMetaNode }
  | { type: "SHOW_PROPERTIES"; node: DesignerMetaNode; values: PropValues }
  | { type: "UPDATE_PROP"; key: string; value: PropValues[string] }
  | { type: "SHOW_EDITOR" }

function reducer(state: DesignerState, action: DesignerAction): DesignerState {
  switch (action.type) {
    case "OPEN_TAB": {
      const exists = state.tabs.find((t) => t.moduleId === action.tab.moduleId)
      return {
        ...state,
        tabs: exists ? state.tabs : [...state.tabs, action.tab],
        activeTabId: action.tab.moduleId,
        centerPane: "editor",
      }
    }
    case "CLOSE_TAB": {
      const remaining = state.tabs.filter((t) => t.moduleId !== action.moduleId)
      const nextActive =
        state.activeTabId === action.moduleId
          ? (remaining.at(-1)?.moduleId ?? null)
          : state.activeTabId
      return {
        ...state,
        tabs: remaining,
        activeTabId: nextActive,
        centerPane: remaining.length === 0 ? "properties" : state.centerPane,
      }
    }
    case "SELECT_TAB":
      return { ...state, activeTabId: action.moduleId, centerPane: "editor" }
    case "UPDATE_CODE": {
      const tabs = state.tabs.map((t) =>
        t.moduleId === action.moduleId ? { ...t, code: action.code, dirty: true } : t,
      )
      return { ...state, tabs }
    }
    case "MARK_SAVED": {
      const tabs = state.tabs.map((t) =>
        t.moduleId === action.moduleId ? { ...t, code: action.code, dirty: false } : t,
      )
      return { ...state, tabs, saving: false }
    }
    case "SET_COMPILE_ERRORS":
      return { ...state, compileErrors: action.errors, compiling: false, consoleOpen: true }
    case "SET_COMPILING":
      return { ...state, compiling: action.value }
    case "SET_SAVING":
      return { ...state, saving: action.value }
    case "TOGGLE_CONSOLE":
      return { ...state, consoleOpen: !state.consoleOpen }
    case "SELECT_NODE":
      return { ...state, selectedNode: action.node }
    case "SHOW_PROPERTIES":
      return { ...state, selectedNode: action.node, propValues: action.values, centerPane: "properties" }
    case "UPDATE_PROP":
      return { ...state, propValues: { ...state.propValues, [action.key]: action.value } }
    case "SHOW_EDITOR":
      return { ...state, centerPane: "editor" }
    default:
      return state
  }
}

const INITIAL_STATE: DesignerState = {
  tabs: [],
  activeTabId: null,
  compileErrors: [],
  consoleOpen: true,
  compiling: false,
  saving: false,
  selectedNode: null,
  centerPane: "properties",
  propValues: {},
}

// ---------------------------------------------------------------------------
// Page component
// ---------------------------------------------------------------------------

export function DesignerPage() {
  const navigate = useNavigate()
  const [state, dispatch] = useReducer(reducer, INITIAL_STATE)
  // Keep latest code in a ref for save/compile without stale closure
  const tabsRef = useRef(state.tabs)
  tabsRef.current = state.tabs

  // ---------------------------------------------------------------------------
  // Module loading
  // ---------------------------------------------------------------------------

  const handleOpenModule = useCallback(
    async (moduleId: string, title: string, objectName: string) => {
      // If already open, just switch
      const existing = tabsRef.current.find((t) => t.moduleId === moduleId)
      if (existing) {
        dispatch({ type: "SELECT_TAB", moduleId })
        return
      }

      try {
        const { data } = await api.get<{ code: string }>(`/designer/module/${moduleId}`)
        dispatch({
          type: "OPEN_TAB",
          tab: { moduleId, title, objectName, code: data.code, dirty: false },
        })
      } catch {
        dispatch({
          type: "SET_COMPILE_ERRORS",
          errors: [{
            line: 0,
            column: 0,
            message: `Failed to load module: ${moduleId}`,
            severity: "error",
            timestamp: Date.now(),
          }],
        })
      }
    },
    [],
  )

  // ---------------------------------------------------------------------------
  // Save
  // ---------------------------------------------------------------------------

  const handleSave = useCallback(async () => {
    const activeId = state.activeTabId
    const tab = tabsRef.current.find((t) => t.moduleId === activeId)
    if (!tab) return

    dispatch({ type: "SET_SAVING", value: true })
    try {
      await api.put(`/designer/module/${tab.moduleId}`, { code: tab.code })
      dispatch({ type: "MARK_SAVED", moduleId: tab.moduleId, code: tab.code })
    } catch (err) {
      dispatch({ type: "SET_SAVING", value: false })
      dispatch({
        type: "SET_COMPILE_ERRORS",
        errors: [{
          line: 0,
          column: 0,
          message: `Save failed: ${err instanceof Error ? err.message : "unknown error"}`,
          severity: "error",
          timestamp: Date.now(),
        }],
      })
    }
  }, [state.activeTabId])

  // ---------------------------------------------------------------------------
  // Compile
  // ---------------------------------------------------------------------------

  const handleCompile = useCallback(async () => {
    const activeId = state.activeTabId
    const tab = tabsRef.current.find((t) => t.moduleId === activeId)
    if (!tab) return

    dispatch({ type: "SET_COMPILING", value: true })

    // Auto-save before compile
    try {
      await api.put(`/designer/module/${tab.moduleId}`, { code: tab.code })
      dispatch({ type: "MARK_SAVED", moduleId: tab.moduleId, code: tab.code })
    } catch {
      // Proceed with compile even if save fails
    }

    try {
      const { data } = await api.post<{ success: boolean; errors: CompileError[] }>(
        `/designer/compile/${tab.moduleId}`,
      )
      dispatch({
        type: "SET_COMPILE_ERRORS",
        errors: (data.errors ?? []).map((e) => ({ ...e, timestamp: Date.now() })),
      })
    } catch (err) {
      dispatch({
        type: "SET_COMPILE_ERRORS",
        errors: [{
          line: 0,
          column: 0,
          message: `Compile request failed: ${err instanceof Error ? err.message : "unknown error"}`,
          severity: "error",
          timestamp: Date.now(),
        }],
      })
    }
  }, [state.activeTabId])

  // ---------------------------------------------------------------------------
  // Update DB
  // ---------------------------------------------------------------------------

  const handleUpdateDb = useCallback(async () => {
    if (!confirm("Update database structure? This will apply all metadata changes to the database.")) return
    try {
      await api.post("/designer/update-db")
      dispatch({
        type: "SET_COMPILE_ERRORS",
        errors: [{
          line: 0,
          column: 0,
          message: "Database structure updated successfully.",
          severity: "info",
          timestamp: Date.now(),
        }],
      })
    } catch (err) {
      dispatch({
        type: "SET_COMPILE_ERRORS",
        errors: [{
          line: 0,
          column: 0,
          message: `Update DB failed: ${err instanceof Error ? err.message : "unknown error"}`,
          severity: "error",
          timestamp: Date.now(),
        }],
      })
    }
  }, [])

  // ---------------------------------------------------------------------------
  // Node selection → PropertyGrid
  // ---------------------------------------------------------------------------

  const handleSelectNode = useCallback(async (node: DesignerMetaNode) => {
    dispatch({ type: "SELECT_NODE", node })

    // If it is an object node, load its full metadata for the property grid
    if (node.type === "object" && node.id) {
      try {
        const { data } = await api.get<Record<string, unknown>>(`/designer/metadata/${node.id}`)
        const values: PropValues = {}
        for (const [k, v] of Object.entries(data)) {
          if (typeof v === "string" || typeof v === "number" || typeof v === "boolean") {
            values[k] = v
          }
        }
        dispatch({ type: "SHOW_PROPERTIES", node, values })
      } catch {
        // Ignore
      }
    }
  }, [])

  // ---------------------------------------------------------------------------
  // Keyboard shortcuts (global)
  // ---------------------------------------------------------------------------

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const meta = e.ctrlKey || e.metaKey
      if (meta && e.key === "s") {
        e.preventDefault()
        handleSave()
      }
      if (e.key === "F7") {
        e.preventDefault()
        handleCompile()
      }
      if (meta && e.shiftKey && e.key === "E") {
        e.preventDefault()
        // Trigger export — handled by toolbar
        document.getElementById("oes-export-btn")?.click()
      }
    }
    window.addEventListener("keydown", handler)
    return () => window.removeEventListener("keydown", handler)
  }, [handleSave, handleCompile])

  // ---------------------------------------------------------------------------
  // Jump to error line in Monaco
  // ---------------------------------------------------------------------------

  function handleErrorClick(err: CompileError) {
    // Monaco editor exposes a command to reveal a line — we use a custom event
    window.dispatchEvent(
      new CustomEvent("oes-goto-line", { detail: { line: err.line, column: err.column } }),
    )
  }

  // ---------------------------------------------------------------------------
  // Property groups for selected node
  // ---------------------------------------------------------------------------

  function buildPropertyGroups(node: DesignerMetaNode | null): PropGroup[] {
    if (!node) return []

    if (node.type === "object") {
      return [
        {
          label: "General",
          fields: [
            { key: "name",    label: "Name",    type: "text" },
            { key: "synonym", label: "Synonym", type: "text" },
            { key: "comment", label: "Comment", type: "text" },
          ],
        },
        {
          label: "Behavior",
          fields: [
            { key: "writeMode",    label: "Write mode",   type: "select", options: ["Independent", "Subordinate"] },
            { key: "quickChoice",  label: "Quick choice", type: "boolean" },
          ],
        },
      ]
    }

    if (node.type === "attribute") {
      return [
        {
          label: "Attribute",
          fields: [
            { key: "name",          label: "Name",          type: "text" },
            { key: "synonym",       label: "Synonym",       type: "text" },
            { key: "typeDesc",      label: "Type",          type: "type-description" },
            { key: "fillCheck",     label: "Fill check",    type: "boolean" },
            { key: "indexing",      label: "Indexing",      type: "select", options: ["NoIndex", "Index", "IndexWithAdditionalOrder"] },
          ],
        },
      ]
    }

    if (node.type === "section") {
      return [
        {
          label: "Section",
          fields: [
            { key: "name",  label: "Name",  type: "text", readOnly: true },
            { key: "count", label: "Count", type: "number", readOnly: true },
          ],
        },
      ]
    }

    return []
  }

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  const propertyGroups = buildPropertyGroups(state.selectedNode)

  return (
    <div className="flex h-screen flex-col overflow-hidden bg-[hsl(var(--background))] density-compact">
      {/* ------------------------------------------------------------------ */}
      {/* Header                                                               */}
      {/* ------------------------------------------------------------------ */}
      <header className="flex h-10 shrink-0 items-center gap-3 border-b border-[hsl(var(--border))] bg-[hsl(var(--sidebar))] px-3">
        <button
          type="button"
          onClick={() => navigate("/")}
          title="Back to application"
          className="flex h-7 items-center gap-1.5 rounded-[2px] border border-transparent px-2 text-[11px] font-medium text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))] hover:border-[hsl(var(--border))] transition-colors"
        >
          <ArrowLeft size={13} weight="duotone" />
          Back
        </button>

        <div className="h-4 w-px bg-[hsl(var(--border))]" />

        <span className="text-[12px] font-semibold text-[hsl(var(--foreground))]">
          OES Designer
        </span>

        <div className="flex-1" />

        {/* Active module indicator */}
        {state.activeTabId && (
          <span className="text-[11px] text-[hsl(var(--muted-foreground))]">
            {state.tabs.find((t) => t.moduleId === state.activeTabId)?.title}
            {state.tabs.find((t) => t.moduleId === state.activeTabId)?.dirty && (
              <span className="ml-1 text-[hsl(var(--primary))]">●</span>
            )}
          </span>
        )}
      </header>

      {/* ------------------------------------------------------------------ */}
      {/* Toolbar                                                              */}
      {/* ------------------------------------------------------------------ */}
      <DesignerToolbar
        activeModuleId={state.activeTabId}
        compiling={state.compiling}
        saving={state.saving}
        onSave={handleSave}
        onCompile={handleCompile}
        onUpdateDb={handleUpdateDb}
        onImportSuccess={() => {
          dispatch({
            type: "SET_COMPILE_ERRORS",
            errors: [{
              line: 0,
              column: 0,
              message: "Configuration imported successfully. Reload the tree.",
              severity: "info",
              timestamp: Date.now(),
            }],
          })
        }}
      />

      {/* ------------------------------------------------------------------ */}
      {/* Main 2-column layout                                                 */}
      {/* ------------------------------------------------------------------ */}
      <div className="flex flex-1 min-h-0 overflow-hidden">

        {/* Left panel — Metadata tree */}
        <aside className="w-64 shrink-0 flex flex-col border-r border-[hsl(var(--border))] bg-[hsl(var(--sidebar))]">
          <MetadataTree
            onOpenModule={handleOpenModule}
            onSelectNode={handleSelectNode}
          />
        </aside>

        {/* Center + Bottom panel */}
        <div className="flex flex-1 flex-col min-w-0 overflow-hidden">

          {/* Center pane */}
          <div className="flex flex-1 min-h-0 overflow-hidden">
            {state.centerPane === "editor" || state.tabs.length > 0 ? (
              <CodeEditor
                tabs={state.tabs}
                activeTabId={state.activeTabId}
                onTabSelect={(id) => dispatch({ type: "SELECT_TAB", moduleId: id })}
                onTabClose={(id) => dispatch({ type: "CLOSE_TAB", moduleId: id })}
                onChange={(moduleId, code) => dispatch({ type: "UPDATE_CODE", moduleId, code })}
                onSave={handleSave}
                onCompile={handleCompile}
                compileErrors={state.compileErrors}
              />
            ) : (
              /* Empty state or PropertyGrid when no editor tabs */
              <div className="flex flex-1 overflow-y-auto">
                {state.selectedNode && propertyGroups.length > 0 ? (
                  <div className="flex-1">
                    <div className="border-b border-[hsl(var(--border))] px-3 py-2">
                      <span className="text-[11px] font-semibold text-[hsl(var(--foreground))]">
                        {state.selectedNode.name}
                      </span>
                      <span className="ml-2 text-[10px] text-[hsl(var(--muted-foreground))]">
                        {state.selectedNode.type}
                      </span>
                    </div>
                    <PropertyGrid
                      groups={propertyGroups}
                      values={state.propValues}
                      onChange={(key, value) => dispatch({ type: "UPDATE_PROP", key, value })}
                    />
                  </div>
                ) : (
                  <div className="flex flex-1 items-center justify-center text-[12px] text-[hsl(var(--muted-foreground))]">
                    Select an object in the tree to view its properties,
                    or double-click a module to open it in the editor.
                  </div>
                )}
              </div>
            )}

            {/* Properties side panel — shown alongside editor when a node is selected */}
            {state.centerPane === "editor" && state.selectedNode && propertyGroups.length > 0 && (
              <aside className="w-56 shrink-0 border-l border-[hsl(var(--border))] overflow-y-auto">
                <div className="border-b border-[hsl(var(--border))] px-3 py-2">
                  <span className="text-[11px] font-semibold text-[hsl(var(--foreground))]">
                    {state.selectedNode.name}
                  </span>
                  <span className="ml-2 text-[10px] text-[hsl(var(--muted-foreground))]">
                    {state.selectedNode.type}
                  </span>
                </div>
                <PropertyGrid
                  groups={propertyGroups}
                  values={state.propValues}
                  onChange={(key, value) => dispatch({ type: "UPDATE_PROP", key, value })}
                />
              </aside>
            )}
          </div>

          {/* Bottom — Compile console */}
          <CompileConsole
            errors={state.compileErrors}
            onErrorClick={handleErrorClick}
            onClear={() => dispatch({ type: "SET_COMPILE_ERRORS", errors: [] })}
            open={state.consoleOpen}
            onToggle={() => dispatch({ type: "TOGGLE_CONSOLE" })}
          />
        </div>
      </div>
    </div>
  )
}
