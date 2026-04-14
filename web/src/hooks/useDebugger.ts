/**
 * useDebugger — Zustand store + polling hook for OES Web Debugger.
 *
 * Architecture:
 *   Frontend ←HTTP polling (200ms)→ ibWebServer ←TCP:1650→ ibDebuggerServer
 *
 * API contract:
 *   POST /api/debug/start
 *   POST /api/debug/stop
 *   POST /api/debug/command     { action, ...params }
 *   GET  /api/debug/state       → DebugStateResponse
 *   POST /api/debug/breakpoint  { module, line, enabled }
 *   GET  /api/debug/breakpoints → Breakpoint[]
 */

import { useEffect, useRef } from "react"
import { create } from "zustand"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type DebugStatus = "idle" | "running" | "paused" | "stopped"

export interface Variable {
  name: string
  value: string
  type: string
  /** For objects/arrays — child variables */
  children?: Variable[]
  /** Whether this node can be expanded (has children not yet loaded) */
  expandable?: boolean
}

export interface StackFrame {
  index: number
  module: string
  procedure: string
  line: number
}

export interface Breakpoint {
  id: string
  module: string
  line: number
  enabled: boolean
}

export type DebugOutputKind = "output" | "evaluate-result" | "evaluate-error" | "system"

export interface DebugOutput {
  id: string
  kind: DebugOutputKind
  text: string
  at: number
}

export interface DebugState {
  status: DebugStatus
  currentModule: string | null
  currentLine: number | null
  variables: Variable[]
  callStack: StackFrame[]
  breakpoints: Breakpoint[]
  output: DebugOutput[]
}

interface DebugStore extends DebugState {
  // Actions
  _setState: (patch: Partial<DebugState>) => void
  _addOutput: (entry: Omit<DebugOutput, "id" | "at">) => void

  start: () => Promise<void>
  stop: () => Promise<void>
  sendCommand: (action: string, params?: Record<string, unknown>) => Promise<unknown>
  continue: () => Promise<void>
  stepOver: () => Promise<void>
  stepInto: () => Promise<void>
  stepOut: () => Promise<void>
  evaluate: (expression: string) => Promise<string>
  toggleBreakpoint: (module: string, line: number) => Promise<void>
  setBreakpointEnabled: (id: string, enabled: boolean) => Promise<void>
  removeBreakpoint: (id: string) => Promise<void>
  syncBreakpoints: () => Promise<void>
  pollState: () => Promise<void>
}

// ---------------------------------------------------------------------------
// API response shapes
// ---------------------------------------------------------------------------

interface DebugStateResponse {
  status: DebugStatus
  currentModule?: string
  currentLine?: number
  variables?: Variable[]
  callStack?: StackFrame[]
  output?: { text: string; kind?: DebugOutputKind }[]
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

let _outputCounter = 0
function makeOutputId(): string {
  return `dbg-out-${Date.now()}-${++_outputCounter}`
}

export const useDebugStore = create<DebugStore>((set, get) => ({
  // Initial state
  status: "idle",
  currentModule: null,
  currentLine: null,
  variables: [],
  callStack: [],
  breakpoints: [],
  output: [],

  // Internal helpers
  _setState: (patch) => set((s) => ({ ...s, ...patch })),

  _addOutput: (entry) =>
    set((s) => ({
      output: [
        ...s.output,
        { ...entry, id: makeOutputId(), at: Date.now() },
      ].slice(-500), // cap at 500 entries
    })),

  // ---------------------------------------------------------------------------
  // Session control
  // ---------------------------------------------------------------------------

  start: async () => {
    try {
      await api.post("/debug/start")
      set({ status: "running", currentModule: null, currentLine: null, variables: [], callStack: [], output: [] })
      get()._addOutput({ kind: "system", text: "Debug session started." })
    } catch (err) {
      get()._addOutput({
        kind: "system",
        text: `Failed to start debug session: ${err instanceof Error ? err.message : String(err)}`,
      })
    }
  },

  stop: async () => {
    try {
      await api.post("/debug/stop")
    } catch {
      // Best effort
    }
    set({ status: "stopped", currentModule: null, currentLine: null })
    get()._addOutput({ kind: "system", text: "Debug session stopped." })
  },

  // ---------------------------------------------------------------------------
  // Commands
  // ---------------------------------------------------------------------------

  sendCommand: async (action, params = {}) => {
    const { data } = await api.post<{ result?: unknown }>("/debug/command", { action, ...params })
    return data.result
  },

  continue: async () => {
    await get().sendCommand("continue")
    set({ status: "running" })
  },

  stepOver: async () => {
    await get().sendCommand("stepOver")
    set({ status: "running" })
  },

  stepInto: async () => {
    await get().sendCommand("stepInto")
    set({ status: "running" })
  },

  stepOut: async () => {
    await get().sendCommand("stepOut")
    set({ status: "running" })
  },

  evaluate: async (expression: string) => {
    try {
      const result = await get().sendCommand("evaluate", { expression })
      const text = String(result ?? "undefined")
      get()._addOutput({ kind: "evaluate-result", text: `> ${expression}\n${text}` })
      return text
    } catch (err) {
      const text = err instanceof Error ? err.message : String(err)
      get()._addOutput({ kind: "evaluate-error", text: `> ${expression}\nError: ${text}` })
      return `Error: ${text}`
    }
  },

  // ---------------------------------------------------------------------------
  // Breakpoints
  // ---------------------------------------------------------------------------

  toggleBreakpoint: async (module: string, line: number) => {
    const existing = get().breakpoints.find((b) => b.module === module && b.line === line)
    if (existing) {
      // Remove it
      try {
        await api.post("/debug/breakpoint", { module, line, enabled: false, remove: true })
      } catch {
        // Best effort
      }
      set((s) => ({ breakpoints: s.breakpoints.filter((b) => b.id !== existing.id) }))
    } else {
      // Add it
      const id = `${module}:${line}`
      try {
        await api.post("/debug/breakpoint", { module, line, enabled: true })
      } catch {
        // Optimistic — add locally even if request fails
      }
      set((s) => ({
        breakpoints: [...s.breakpoints, { id, module, line, enabled: true }],
      }))
    }
  },

  setBreakpointEnabled: async (id: string, enabled: boolean) => {
    const bp = get().breakpoints.find((b) => b.id === id)
    if (!bp) return
    try {
      await api.post("/debug/breakpoint", { module: bp.module, line: bp.line, enabled })
    } catch {
      // Best effort
    }
    set((s) => ({
      breakpoints: s.breakpoints.map((b) => (b.id === id ? { ...b, enabled } : b)),
    }))
  },

  removeBreakpoint: async (id: string) => {
    const bp = get().breakpoints.find((b) => b.id === id)
    if (!bp) return
    try {
      await api.post("/debug/breakpoint", { module: bp.module, line: bp.line, enabled: false, remove: true })
    } catch {
      // Best effort
    }
    set((s) => ({ breakpoints: s.breakpoints.filter((b) => b.id !== id) }))
  },

  syncBreakpoints: async () => {
    try {
      const { data } = await api.get<Breakpoint[]>("/debug/breakpoints")
      set({ breakpoints: data ?? [] })
    } catch {
      // Ignore
    }
  },

  // ---------------------------------------------------------------------------
  // Polling
  // ---------------------------------------------------------------------------

  pollState: async () => {
    try {
      const { data } = await api.get<DebugStateResponse>("/debug/state")
      const prev = get()

      const patch: Partial<DebugState> = {
        status: data.status ?? prev.status,
        currentModule: data.currentModule ?? null,
        currentLine: data.currentLine ?? null,
        variables: data.variables ?? prev.variables,
        callStack: data.callStack ?? prev.callStack,
      }

      // Append any server-side output lines
      if (data.output && data.output.length > 0) {
        const newEntries: DebugOutput[] = data.output.map((o) => ({
          id: makeOutputId(),
          kind: o.kind ?? "output",
          text: o.text,
          at: Date.now(),
        }))
        patch.output = [...prev.output, ...newEntries].slice(-500)
      }

      set(patch)
    } catch {
      // Network blip — ignore
    }
  },
}))

// ---------------------------------------------------------------------------
// Polling hook — call this once in DebuggerPage
// ---------------------------------------------------------------------------

const POLL_INTERVAL_MS = 200

export function useDebuggerPolling() {
  const status = useDebugStore((s) => s.status)
  const pollState = useDebugStore((s) => s.pollState)
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null)

  useEffect(() => {
    const active = status === "running" || status === "paused"

    if (active) {
      timerRef.current = setInterval(() => {
        void pollState()
      }, POLL_INTERVAL_MS)
    } else {
      if (timerRef.current !== null) {
        clearInterval(timerRef.current)
        timerRef.current = null
      }
    }

    return () => {
      if (timerRef.current !== null) {
        clearInterval(timerRef.current)
        timerRef.current = null
      }
    }
  }, [status, pollState])
}
