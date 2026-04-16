/**
 * VariablesPanel — shows local variables and watch expressions.
 *
 * Layout (two sections):
 *   ▼ Variables   — from current debug state
 *   ▼ Watch       — user-defined expressions, evaluated on demand
 */

import { useState, useCallback, useRef, useEffect } from "react"
import {
  CaretRight,
  CaretDown,
  Eye,
  Plus,
  X,
  PencilSimple,
} from "@phosphor-icons/react"
import type { Variable } from "@/hooks/useDebugger"

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

interface VariablesPanelProps {
  variables: Variable[]
  isPaused: boolean
  onEvaluate: (expression: string) => Promise<string>
}

// ---------------------------------------------------------------------------
// Watch expression record
// ---------------------------------------------------------------------------

interface WatchExpr {
  id: string
  expression: string
  result: string | null
  evaluating: boolean
}

let _watchCounter = 0

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function VariablesPanel({ variables, isPaused, onEvaluate }: VariablesPanelProps) {
  const [watchExprs, setWatchExprs] = useState<WatchExpr[]>([])
  const [newExpr, setNewExpr] = useState("")
  const [addingWatch, setAddingWatch] = useState(false)
  const inputRef = useRef<HTMLInputElement>(null)

  // Re-evaluate all watch expressions when paused
  useEffect(() => {
    if (!isPaused || watchExprs.length === 0) return

    watchExprs.forEach((w) => {
      setWatchExprs((prev) =>
        prev.map((e) => (e.id === w.id ? { ...e, evaluating: true } : e)),
      )
      onEvaluate(w.expression)
        .then((result) => {
          setWatchExprs((prev) =>
            prev.map((e) => (e.id === w.id ? { ...e, result, evaluating: false } : e)),
          )
        })
        .catch(() => {
          setWatchExprs((prev) =>
            prev.map((e) => (e.id === w.id ? { ...e, result: "<error>", evaluating: false } : e)),
          )
        })
    })
  }, [isPaused]) // intentionally omit watchExprs to avoid loop

  const addWatch = useCallback(async () => {
    const expr = newExpr.trim()
    if (!expr) return

    const id = `watch-${Date.now()}-${++_watchCounter}`
    const entry: WatchExpr = { id, expression: expr, result: null, evaluating: isPaused }
    setWatchExprs((prev) => [...prev, entry])
    setNewExpr("")
    setAddingWatch(false)

    if (isPaused) {
      const result = await onEvaluate(expr).catch(() => "<error>")
      setWatchExprs((prev) =>
        prev.map((e) => (e.id === id ? { ...e, result, evaluating: false } : e)),
      )
    }
  }, [newExpr, isPaused, onEvaluate])

  const removeWatch = useCallback((id: string) => {
    setWatchExprs((prev) => prev.filter((e) => e.id !== id))
  }, [])

  // Focus input when add panel opens
  useEffect(() => {
    if (addingWatch) {
      setTimeout(() => inputRef.current?.focus(), 30)
    }
  }, [addingWatch])

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <div className="flex flex-col h-full text-[11px] overflow-hidden">
      {/* Variables section */}
      <Section label="Variables" defaultOpen>
        {variables.length === 0 ? (
          <EmptyRow text="No variables in scope" />
        ) : (
          <div className="overflow-y-auto">
            {variables.map((v, i) => (
              <VariableRow key={`${v.name}-${i}`} variable={v} depth={0} />
            ))}
          </div>
        )}
      </Section>

      {/* Watch section */}
      <Section
        label="Watch"
        defaultOpen
        actions={
          <button
            type="button"
            title="Add watch expression"
            onClick={() => setAddingWatch(true)}
            className="flex h-4 w-4 items-center justify-center rounded-[2px] hover:bg-[hsl(var(--secondary))] transition-colors"
          >
            <Plus size={10} weight="bold" />
          </button>
        }
      >
        {addingWatch && (
          <div className="flex items-center gap-1 border-b border-[hsl(var(--border))] px-2 py-1">
            <Eye size={10} weight="duotone" className="text-[hsl(var(--muted-foreground))] shrink-0" />
            <input
              ref={inputRef}
              value={newExpr}
              onChange={(e) => setNewExpr(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") void addWatch()
                if (e.key === "Escape") { setAddingWatch(false); setNewExpr("") }
              }}
              placeholder="Expression..."
              className="flex-1 bg-transparent outline-none placeholder:text-[hsl(var(--muted-foreground))] text-[11px]"
            />
            <button
              type="button"
              onClick={() => void addWatch()}
              className="text-[hsl(var(--primary))] hover:underline"
            >
              Add
            </button>
            <button
              type="button"
              onClick={() => { setAddingWatch(false); setNewExpr("") }}
              className="text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]"
            >
              <X size={10} weight="bold" />
            </button>
          </div>
        )}

        {watchExprs.length === 0 && !addingWatch ? (
          <EmptyRow text="Click + to add a watch expression" />
        ) : (
          watchExprs.map((w) => (
            <WatchRow
              key={w.id}
              watch={w}
              onRemove={() => removeWatch(w.id)}
            />
          ))
        )}
      </Section>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------

interface SectionProps {
  label: string
  defaultOpen?: boolean
  actions?: React.ReactNode
  children: React.ReactNode
}

function Section({ label, defaultOpen = true, actions, children }: SectionProps) {
  const [open, setOpen] = useState(defaultOpen)

  return (
    <div className="flex flex-col border-b border-[hsl(var(--border))] last:border-b-0 min-h-0">
      <button
        type="button"
        onClick={() => setOpen((v) => !v)}
        className="flex h-6 shrink-0 items-center gap-1 px-2 font-semibold uppercase tracking-wide text-[10px] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] transition-colors select-none"
      >
        {open
          ? <CaretDown size={9} weight="bold" className="shrink-0" />
          : <CaretRight size={9} weight="bold" className="shrink-0" />
        }
        <span className="flex-1 text-left">{label}</span>
        {actions && (
          <span onClick={(e) => e.stopPropagation()} className="flex items-center">
            {actions}
          </span>
        )}
      </button>

      {open && (
        <div className="flex flex-col overflow-y-auto">
          {children}
        </div>
      )}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Variable row (recursive)
// ---------------------------------------------------------------------------

interface VariableRowProps {
  variable: Variable
  depth: number
}

function VariableRow({ variable, depth }: VariableRowProps) {
  const [expanded, setExpanded] = useState(false)
  const hasChildren = (variable.children && variable.children.length > 0) || variable.expandable

  const indent = depth * 12 + 8

  return (
    <>
      <div
        className="flex items-center gap-1 py-0.5 pr-2 hover:bg-[hsl(var(--secondary)/0.5)] cursor-default select-none"
        style={{ paddingLeft: indent }}
      >
        {/* Expand arrow */}
        <span className="w-3 shrink-0 flex items-center justify-center">
          {hasChildren ? (
            <button
              type="button"
              onClick={() => setExpanded((v) => !v)}
              className="flex items-center justify-center"
            >
              {expanded
                ? <CaretDown size={8} weight="bold" />
                : <CaretRight size={8} weight="bold" />
              }
            </button>
          ) : null}
        </span>

        {/* Name */}
        <span className="w-[100px] shrink-0 truncate text-[hsl(var(--foreground))]">
          {variable.name}
        </span>

        {/* Value */}
        <span className="flex-1 truncate text-amber-300">
          {variable.value}
        </span>

        {/* Type */}
        <span className="w-[60px] shrink-0 truncate text-right text-[hsl(var(--muted-foreground))]">
          {variable.type}
        </span>
      </div>

      {/* Children */}
      {expanded && variable.children?.map((child, i) => (
        <VariableRow key={`${child.name}-${i}`} variable={child} depth={depth + 1} />
      ))}
    </>
  )
}

// ---------------------------------------------------------------------------
// Watch row
// ---------------------------------------------------------------------------

interface WatchRowProps {
  watch: WatchExpr
  onRemove: () => void
}

function WatchRow({ watch, onRemove }: WatchRowProps) {
  return (
    <div className="group flex items-center gap-1 py-0.5 pr-2 pl-5 hover:bg-[hsl(var(--secondary)/0.5)] select-none">
      <PencilSimple size={9} weight="duotone" className="shrink-0 text-[hsl(var(--muted-foreground))]" />

      <span className="w-[100px] shrink-0 truncate text-[hsl(var(--foreground))]">
        {watch.expression}
      </span>

      <span className="flex-1 truncate text-amber-300 italic">
        {watch.evaluating
          ? "…"
          : watch.result ?? (
            <span className="text-[hsl(var(--muted-foreground))]">not evaluated</span>
          )
        }
      </span>

      <button
        type="button"
        onClick={onRemove}
        title="Remove watch"
        className="opacity-0 group-hover:opacity-100 flex h-4 w-4 items-center justify-center rounded-[2px] hover:bg-[hsl(var(--secondary))] transition-all"
      >
        <X size={9} weight="bold" />
      </button>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Empty row
// ---------------------------------------------------------------------------

function EmptyRow({ text }: { text: string }) {
  return (
    <div className="px-5 py-2 text-[hsl(var(--muted-foreground))] italic">
      {text}
    </div>
  )
}
