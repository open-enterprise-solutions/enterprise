import { X, Warning, Info, CheckCircle, XCircle } from "@phosphor-icons/react"
import { format } from "date-fns"

export interface CompileError {
  line: number
  column: number
  message: string
  severity: "error" | "warning" | "info"
  timestamp?: number
}

interface CompileConsoleProps {
  errors: CompileError[]
  onErrorClick: (error: CompileError) => void
  onClear: () => void
  open: boolean
  onToggle: () => void
}

const SEVERITY_ICON = {
  error:   <XCircle   size={13} weight="duotone" className="text-[hsl(var(--destructive))] shrink-0" />,
  warning: <Warning   size={13} weight="duotone" className="text-[hsl(220,80%,65%)] shrink-0" />,
  info:    <Info      size={13} weight="duotone" className="text-[hsl(var(--muted-foreground))] shrink-0" />,
}

const SEVERITY_ROW = {
  error:   "hover:bg-[hsl(var(--destructive)/0.08)]",
  warning: "hover:bg-[hsl(220,80%,65%,0.08)]",
  info:    "hover:bg-[hsl(var(--secondary))]",
}

export function CompileConsole({
  errors,
  onErrorClick,
  onClear,
  open,
  onToggle,
}: CompileConsoleProps) {
  const errorCount   = errors.filter((e) => e.severity === "error").length
  const warningCount = errors.filter((e) => e.severity === "warning").length

  return (
    <div
      className={[
        "flex flex-col border-t border-[hsl(var(--border))] bg-[hsl(var(--background))]",
        "transition-[height] duration-150",
        open ? "h-48" : "h-8",
      ].join(" ")}
    >
      {/* Header */}
      <div className="flex h-8 shrink-0 items-center gap-2 border-b border-[hsl(var(--border))] px-3">
        <button
          type="button"
          onClick={onToggle}
          className="flex flex-1 items-center gap-2 text-left"
        >
          <span className="text-[11px] font-semibold uppercase tracking-wide text-[hsl(var(--muted-foreground))]">
            Output
          </span>
          {errorCount > 0 && (
            <span className="flex items-center gap-1 rounded-[2px] bg-[hsl(var(--destructive)/0.15)] px-1.5 py-0.5 text-[10px] font-medium text-[hsl(var(--destructive))]">
              <XCircle size={10} weight="fill" />
              {errorCount}
            </span>
          )}
          {warningCount > 0 && (
            <span className="flex items-center gap-1 rounded-[2px] bg-[hsl(220,80%,65%,0.15)] px-1.5 py-0.5 text-[10px] font-medium text-[hsl(220,80%,65%)]">
              <Warning size={10} weight="fill" />
              {warningCount}
            </span>
          )}
          {errors.length === 0 && (
            <span className="flex items-center gap-1 text-[10px] text-[hsl(var(--muted-foreground))]">
              <CheckCircle size={10} weight="duotone" />
              No errors
            </span>
          )}
        </button>

        {errors.length > 0 && (
          <button
            type="button"
            onClick={onClear}
            title="Clear output"
            className="flex h-5 w-5 items-center justify-center rounded-[2px] hover:bg-[hsl(var(--secondary))] text-[hsl(var(--muted-foreground))] transition-colors"
          >
            <X size={11} weight="bold" />
          </button>
        )}
      </div>

      {/* Error list */}
      {open && (
        <div className="flex-1 overflow-y-auto font-mono">
          {errors.length === 0 ? (
            <div className="flex items-center gap-2 px-3 py-2 text-[11px] text-[hsl(var(--muted-foreground))]">
              Ready.
            </div>
          ) : (
            errors.map((err, i) => (
              <button
                key={i}
                type="button"
                onClick={() => onErrorClick(err)}
                className={[
                  "flex w-full items-start gap-2 px-3 py-1.5 text-left text-[11px] transition-colors",
                  SEVERITY_ROW[err.severity],
                ].join(" ")}
              >
                {SEVERITY_ICON[err.severity]}
                <span className="text-[hsl(var(--muted-foreground))] shrink-0 tabular-nums w-16">
                  {err.timestamp
                    ? format(new Date(err.timestamp), "HH:mm:ss")
                    : ""}
                </span>
                <span className="text-[hsl(var(--muted-foreground))] shrink-0 w-20 tabular-nums">
                  Ln {err.line}, Col {err.column}
                </span>
                <span className="flex-1 text-[hsl(var(--foreground))]">{err.message}</span>
              </button>
            ))
          )}
        </div>
      )}
    </div>
  )
}
