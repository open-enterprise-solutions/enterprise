/**
 * DebugConsole — evaluate expressions + show debug output.
 *
 * Features:
 * - Input with history navigation (ArrowUp/ArrowDown)
 * - Timestamped output entries colour-coded by kind
 * - Auto-scroll to bottom on new entries
 */

import { useState, useRef, useEffect, useCallback } from "react"
import { Terminal, ArrowRight, Trash } from "@phosphor-icons/react"
import type { DebugOutput } from "@/hooks/useDebugger"

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

interface DebugConsoleProps {
  output: DebugOutput[]
  isPaused: boolean
  onEvaluate: (expression: string) => Promise<string>
  onClear: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function DebugConsole({ output, isPaused, onEvaluate, onClear }: DebugConsoleProps) {
  const [inputValue, setInputValue] = useState("")
  const [evaluating, setEvaluating] = useState(false)
  const [history, setHistory] = useState<string[]>([])
  const [, setHistoryIndex] = useState(-1)

  const outputRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)

  // Auto-scroll to bottom when new output arrives
  useEffect(() => {
    const el = outputRef.current
    if (el) {
      el.scrollTop = el.scrollHeight
    }
  }, [output])

  const submit = useCallback(async () => {
    const expr = inputValue.trim()
    if (!expr || evaluating) return

    // Add to history (deduplicate at head)
    setHistory((prev) => {
      const without = prev.filter((h) => h !== expr)
      return [expr, ...without].slice(0, 100)
    })
    setHistoryIndex(-1)
    setInputValue("")

    setEvaluating(true)
    try {
      await onEvaluate(expr)
    } finally {
      setEvaluating(false)
      inputRef.current?.focus()
    }
  }, [inputValue, evaluating, onEvaluate])

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent<HTMLInputElement>) => {
      if (e.key === "Enter") {
        e.preventDefault()
        void submit()
        return
      }

      if (e.key === "ArrowUp") {
        e.preventDefault()
        setHistoryIndex((prev) => {
          const next = Math.min(prev + 1, history.length - 1)
          setInputValue(history[next] ?? "")
          return next
        })
        return
      }

      if (e.key === "ArrowDown") {
        e.preventDefault()
        setHistoryIndex((prev) => {
          const next = Math.max(prev - 1, -1)
          setInputValue(next === -1 ? "" : (history[next] ?? ""))
          return next
        })
        return
      }
    },
    [submit, history],
  )

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <div className="flex flex-col h-full text-[11px] overflow-hidden">
      {/* Header */}
      <div className="flex h-6 shrink-0 items-center gap-1 px-2 border-b border-[hsl(var(--border))] font-semibold uppercase tracking-wide text-[10px] text-[hsl(var(--muted-foreground))]">
        <Terminal size={10} weight="duotone" className="shrink-0" />
        <span className="flex-1">Debug Console</span>
        <button
          type="button"
          title="Clear console"
          onClick={onClear}
          className="flex h-4 w-4 items-center justify-center rounded-[2px] hover:bg-[hsl(var(--secondary))] transition-colors"
        >
          <Trash size={10} weight="duotone" />
        </button>
      </div>

      {/* Output area */}
      <div
        ref={outputRef}
        className="flex-1 overflow-y-auto font-mono text-[11px] leading-5 px-2 py-1"
      >
        {output.length === 0 && (
          <div className="text-[hsl(var(--muted-foreground))] italic pt-1">
            Use the input below to evaluate expressions while paused.
          </div>
        )}
        {output.map((entry) => (
          <OutputLine key={entry.id} entry={entry} />
        ))}
        {evaluating && (
          <div className="text-[hsl(var(--muted-foreground))] italic">evaluating…</div>
        )}
      </div>

      {/* Input */}
      <div className={[
        "flex shrink-0 items-center gap-1 border-t border-[hsl(var(--border))] px-2 py-1",
        !isPaused ? "opacity-50 pointer-events-none" : "",
      ].join(" ")}>
        <ArrowRight size={10} weight="bold" className="text-[hsl(var(--muted-foreground))] shrink-0" />
        <input
          ref={inputRef}
          value={inputValue}
          onChange={(e) => setInputValue(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={isPaused ? "Evaluate expression… (Enter)" : "Paused to evaluate"}
          disabled={!isPaused || evaluating}
          className="flex-1 bg-transparent font-mono outline-none placeholder:text-[hsl(var(--muted-foreground))] text-[11px]"
        />
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Output line
// ---------------------------------------------------------------------------

interface OutputLineProps {
  entry: DebugOutput
}

const KIND_STYLES: Record<string, string> = {
  "output":           "text-[hsl(var(--foreground))]",
  "evaluate-result":  "text-emerald-400",
  "evaluate-error":   "text-red-400",
  "system":           "text-[hsl(var(--muted-foreground))] italic",
}

function formatTime(ms: number): string {
  const d = new Date(ms)
  return d.toLocaleTimeString("en-US", { hour12: false, hour: "2-digit", minute: "2-digit", second: "2-digit" })
}

function OutputLine({ entry }: OutputLineProps) {
  const style = KIND_STYLES[entry.kind] ?? "text-[hsl(var(--foreground))]"
  const lines = entry.text.split("\n")

  return (
    <div className="flex gap-2 group">
      <span className="shrink-0 text-[9px] text-[hsl(var(--muted-foreground))] leading-5 opacity-0 group-hover:opacity-100 transition-opacity w-[54px] text-right">
        {formatTime(entry.at)}
      </span>
      <div className={`flex-1 whitespace-pre-wrap break-all ${style}`}>
        {lines.map((line, i) => (
          <div key={i}>{line || "\u00a0"}</div>
        ))}
      </div>
    </div>
  )
}
