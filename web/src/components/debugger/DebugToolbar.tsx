/**
 * DebugToolbar — debug session controls.
 *
 * Shortcuts:
 *   F5           Continue
 *   F10          Step Over
 *   F11          Step Into
 *   Shift+F11    Step Out
 *   Shift+F5     Stop
 */

import { useEffect } from "react"
import {
  Play,
  Pause,
  ArrowDown,
  ArrowDownLeft,
  ArrowUp,
  StopCircle,
  Bug,
} from "@phosphor-icons/react"
import type { DebugStatus } from "@/hooks/useDebugger"

interface DebugToolbarProps {
  status: DebugStatus
  onStart: () => void
  onStop: () => void
  onContinue: () => void
  onPause: () => void
  onStepOver: () => void
  onStepInto: () => void
  onStepOut: () => void
}

export function DebugToolbar({
  status,
  onStart,
  onStop,
  onContinue,
  onPause,
  onStepOver,
  onStepInto,
  onStepOut,
}: DebugToolbarProps) {
  const isIdle = status === "idle" || status === "stopped"
  const isRunning = status === "running"
  const isPaused = status === "paused"
  const isActive = isRunning || isPaused

  // ---------------------------------------------------------------------------
  // Global keyboard shortcuts
  // ---------------------------------------------------------------------------

  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      // Prevent default browser behaviour for function keys
      if (["F5", "F10", "F11"].includes(e.key)) {
        e.preventDefault()
      }

      if (e.key === "F5" && e.shiftKey) {
        if (isActive) onStop()
        return
      }

      switch (e.key) {
        case "F5":
          if (isIdle) onStart()
          else if (isPaused) onContinue()
          break
        case "F10":
          if (isPaused) onStepOver()
          break
        case "F11":
          if (e.shiftKey) {
            if (isPaused) onStepOut()
          } else {
            if (isPaused) onStepInto()
          }
          break
      }
    }

    window.addEventListener("keydown", handler)
    return () => window.removeEventListener("keydown", handler)
  }, [isIdle, isRunning, isPaused, isActive, onStart, onStop, onContinue, onStepOver, onStepInto, onStepOut])

  // ---------------------------------------------------------------------------
  // Status badge
  // ---------------------------------------------------------------------------

  const statusConfig = {
    idle:    { label: "Idle",    dot: "bg-[hsl(var(--muted-foreground))]" },
    running: { label: "Running", dot: "bg-[hsl(var(--success,142_71%_45%))]" },
    paused:  { label: "Paused",  dot: "bg-amber-400" },
    stopped: { label: "Stopped", dot: "bg-red-500" },
  } as const

  const { label, dot } = statusConfig[status]

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <div className="flex h-9 shrink-0 items-center gap-1 border-b border-[hsl(var(--border))] bg-[hsl(var(--sidebar))] px-2">
      {/* Bug icon — branding */}
      <Bug size={16} weight="duotone" className="text-[hsl(var(--muted-foreground))] mr-1 shrink-0" />

      {/* Start / Continue */}
      {isIdle ? (
        <ToolbarBtn
          label="Start Debugging (F5)"
          onClick={onStart}
          icon={<Play size={14} weight="duotone" className="text-emerald-400" />}
        />
      ) : (
        <ToolbarBtn
          label={isPaused ? "Continue (F5)" : "Continue (F5)"}
          onClick={onContinue}
          disabled={isRunning}
          icon={<Play size={14} weight="duotone" className={isPaused ? "text-emerald-400" : "text-[hsl(var(--muted-foreground))]"} />}
        />
      )}

      {/* Pause */}
      <ToolbarBtn
        label="Pause"
        onClick={onPause}
        disabled={!isRunning}
        icon={<Pause size={14} weight="duotone" className={isRunning ? "text-amber-400" : "text-[hsl(var(--muted-foreground))]"} />}
      />

      <div className="mx-0.5 h-4 w-px bg-[hsl(var(--border))]" />

      {/* Step Over */}
      <ToolbarBtn
        label="Step Over (F10)"
        onClick={onStepOver}
        disabled={!isPaused}
        icon={<ArrowDown size={14} weight="duotone" className={isPaused ? "text-sky-400" : "text-[hsl(var(--muted-foreground))]"} />}
      />

      {/* Step Into */}
      <ToolbarBtn
        label="Step Into (F11)"
        onClick={onStepInto}
        disabled={!isPaused}
        icon={<ArrowDownLeft size={14} weight="duotone" className={isPaused ? "text-sky-400" : "text-[hsl(var(--muted-foreground))]"} />}
      />

      {/* Step Out */}
      <ToolbarBtn
        label="Step Out (Shift+F11)"
        onClick={onStepOut}
        disabled={!isPaused}
        icon={<ArrowUp size={14} weight="duotone" className={isPaused ? "text-sky-400" : "text-[hsl(var(--muted-foreground))]"} />}
      />

      <div className="mx-0.5 h-4 w-px bg-[hsl(var(--border))]" />

      {/* Stop */}
      <ToolbarBtn
        label="Stop (Shift+F5)"
        onClick={onStop}
        disabled={isIdle}
        icon={<StopCircle size={14} weight="duotone" className={isActive ? "text-red-500" : "text-[hsl(var(--muted-foreground))]"} />}
      />

      {/* Spacer */}
      <div className="flex-1" />

      {/* Status indicator */}
      <div className="flex items-center gap-1.5 pr-1">
        <span className={`h-1.5 w-1.5 rounded-full shrink-0 ${dot}`} />
        <span className="text-[11px] text-[hsl(var(--muted-foreground))]">{label}</span>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Toolbar button
// ---------------------------------------------------------------------------

interface ToolbarBtnProps {
  label: string
  icon: React.ReactNode
  onClick: () => void
  disabled?: boolean
}

function ToolbarBtn({ label, icon, onClick, disabled = false }: ToolbarBtnProps) {
  return (
    <button
      type="button"
      title={label}
      aria-label={label}
      disabled={disabled}
      onClick={onClick}
      className={[
        "flex h-6 w-6 items-center justify-center rounded-[2px] transition-colors",
        disabled
          ? "cursor-not-allowed opacity-40"
          : "hover:bg-[hsl(var(--secondary))] active:bg-[hsl(var(--secondary))]",
      ].join(" ")}
    >
      {icon}
    </button>
  )
}
