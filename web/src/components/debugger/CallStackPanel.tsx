/**
 * CallStackPanel — shows the current call stack frames.
 * Clicking a frame navigates the code editor to that module/line.
 */

import { Stack } from "@phosphor-icons/react"
import type { StackFrame } from "@/hooks/useDebugger"

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

interface CallStackPanelProps {
  frames: StackFrame[]
  /** Index of the currently active frame (topmost = 0) */
  activeFrameIndex: number
  onFrameSelect: (frame: StackFrame) => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function CallStackPanel({ frames, activeFrameIndex, onFrameSelect }: CallStackPanelProps) {
  return (
    <div className="flex flex-col h-full text-[11px] overflow-hidden">
      {/* Header */}
      <div className="flex h-6 shrink-0 items-center gap-1 px-2 border-b border-[hsl(var(--border))] font-semibold uppercase tracking-wide text-[10px] text-[hsl(var(--muted-foreground))]">
        <Stack size={10} weight="duotone" className="shrink-0" />
        <span>Call Stack</span>
      </div>

      {/* Frames */}
      <div className="flex-1 overflow-y-auto">
        {frames.length === 0 ? (
          <div className="px-3 py-2 text-[hsl(var(--muted-foreground))] italic">
            No call stack
          </div>
        ) : (
          frames.map((frame, i) => (
            <FrameRow
              key={frame.index}
              frame={frame}
              isActive={i === activeFrameIndex}
              onClick={() => onFrameSelect(frame)}
            />
          ))
        )}
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Frame row
// ---------------------------------------------------------------------------

interface FrameRowProps {
  frame: StackFrame
  isActive: boolean
  onClick: () => void
}

function FrameRow({ frame, isActive, onClick }: FrameRowProps) {
  return (
    <button
      type="button"
      onClick={onClick}
      title={`${frame.module} — line ${frame.line}`}
      className={[
        "w-full flex items-start gap-2 px-3 py-1 text-left transition-colors cursor-pointer",
        isActive
          ? "bg-[hsl(var(--primary)/0.15)] border-l-2 border-[hsl(var(--primary))]"
          : "hover:bg-[hsl(var(--secondary)/0.5)] border-l-2 border-transparent",
      ].join(" ")}
    >
      {/* Frame index */}
      <span className="w-4 shrink-0 text-[hsl(var(--muted-foreground))] text-right leading-4">
        {frame.index}
      </span>

      <div className="flex flex-col min-w-0">
        {/* Procedure name */}
        <span
          className={[
            "truncate font-medium leading-4",
            isActive ? "text-[hsl(var(--foreground))]" : "text-[hsl(var(--foreground)/0.8)]",
          ].join(" ")}
        >
          {frame.procedure || "<anonymous>"}
        </span>

        {/* Module + line */}
        <span className="truncate text-[10px] text-[hsl(var(--muted-foreground))] leading-4">
          {frame.module}:{frame.line}
        </span>
      </div>
    </button>
  )
}
