import { useEffect, type ReactNode } from "react"
import {
  FloppyDisk,
  Trash,
  Copy,
  CheckCircle,
  XCircle,
  X,
  DotsThreeOutline,
} from "@phosphor-icons/react"
import type { OesCommand } from "@/hooks/useFormSchema"

interface CommandBarAction {
  command: OesCommand
  label: string
  icon: ReactNode
  variant?: "default" | "primary" | "danger"
  disabled?: boolean
  shortcut?: string
}

interface CommandBarProps {
  commands: OesCommand[]
  onSave?: () => void
  onDelete?: () => void
  onCopy?: () => void
  onPost?: () => void
  onUnpost?: () => void
  onClose?: () => void
  isSaving?: boolean
  isPosting?: boolean
  disabled?: boolean
  /** Document status — determines which of Post/Unpost to show when both present in commands */
  docStatus?: "draft" | "posted" | "cancelled" | string
}

function cn(...classes: (string | undefined | false | null)[]): string {
  return classes.filter(Boolean).join(" ")
}

const variantStyles: Record<string, string> = {
  default:
    "border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))]",
  primary:
    "border border-[hsl(var(--primary))] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] hover:bg-[hsl(var(--primary)/0.88)]",
  danger:
    "border border-[hsl(var(--destructive)/0.4)] bg-[hsl(var(--background))] text-[hsl(var(--destructive))] hover:bg-[hsl(var(--destructive)/0.08)]",
}

function BarButton({
  icon,
  label,
  variant = "default",
  disabled,
  shortcut,
  onClick,
}: {
  icon: ReactNode
  label: string
  variant?: "default" | "primary" | "danger"
  disabled?: boolean
  shortcut?: string
  onClick?: () => void
}) {
  return (
    <button
      type="button"
      disabled={disabled}
      onClick={onClick}
      title={shortcut ? `${label} (${shortcut})` : label}
      className={cn(
        "flex items-center gap-1.5 h-8 px-3 rounded-[var(--radius)] text-[12px] font-medium transition-colors disabled:opacity-50 disabled:cursor-not-allowed",
        variantStyles[variant],
      )}
    >
      {icon}
      <span>{label}</span>
      {shortcut && (
        <kbd className="hidden sm:inline-flex items-center rounded border border-current/20 px-1 text-[10px] opacity-60">
          {shortcut}
        </kbd>
      )}
    </button>
  )
}

function Separator() {
  return <div className="w-px h-5 bg-[hsl(var(--border))]" />
}

export function CommandBar({
  commands,
  onSave,
  onDelete,
  onCopy,
  onPost,
  onUnpost,
  onClose,
  isSaving = false,
  isPosting = false,
  disabled = false,
  docStatus,
}: CommandBarProps) {
  const has = (cmd: OesCommand) => commands.includes(cmd)

  // Keyboard shortcuts
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (disabled) return
      if ((e.ctrlKey || e.metaKey) && e.key === "s") {
        e.preventDefault()
        onSave?.()
      }
      if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
        e.preventDefault()
        if (docStatus === "posted") onUnpost?.()
        else onPost?.()
      }
      if (e.key === "Escape") {
        e.preventDefault()
        onClose?.()
      }
    }
    window.addEventListener("keydown", handler)
    return () => window.removeEventListener("keydown", handler)
  }, [disabled, docStatus, onSave, onPost, onUnpost, onClose])

  // Build the visible action list from the commands the schema declared
  const actions: Array<CommandBarAction | "separator"> = []

  if (has("save")) {
    actions.push({
      command: "save",
      label: isSaving ? "Saving..." : "Save",
      icon: <FloppyDisk size={15} weight="duotone" />,
      variant: "primary",
      disabled: isSaving || disabled,
      shortcut: "Ctrl+S",
    })
  }

  if (has("post") || has("unpost")) {
    if (actions.length > 0) actions.push("separator")

    // If both commands are present, show the appropriate one based on docStatus.
    const showPost = has("post") && docStatus !== "posted"
    const showUnpost = has("unpost") && docStatus === "posted"

    if (showPost) {
      actions.push({
        command: "post",
        label: isPosting ? "Posting..." : "Post",
        icon: <CheckCircle size={15} weight="duotone" />,
        disabled: isPosting || disabled,
        shortcut: "Ctrl+Enter",
      })
    }
    if (showUnpost) {
      actions.push({
        command: "unpost",
        label: isPosting ? "Unposting..." : "Unpost",
        icon: <XCircle size={15} weight="duotone" />,
        disabled: isPosting || disabled,
        shortcut: "Ctrl+Enter",
      })
    }
  }

  if (has("copy")) {
    if (actions.length > 0 && actions[actions.length - 1] !== "separator") {
      actions.push("separator")
    }
    actions.push({
      command: "copy",
      label: "Copy",
      icon: <Copy size={15} weight="duotone" />,
      disabled,
    })
  }

  if (has("delete")) {
    if (actions.length > 0 && actions[actions.length - 1] !== "separator") {
      actions.push("separator")
    }
    actions.push({
      command: "delete",
      label: "Delete",
      icon: <Trash size={15} weight="duotone" />,
      variant: "danger",
      disabled,
    })
  }

  const handlerFor = (cmd: OesCommand) => {
    switch (cmd) {
      case "save": return onSave
      case "delete": return onDelete
      case "copy": return onCopy
      case "post": return onPost
      case "unpost": return onUnpost
      case "close": return onClose
      default: return undefined
    }
  }

  return (
    <div className="flex items-center gap-1.5 px-4 py-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.4)]">
      {actions.map((action, idx) => {
        if (action === "separator") {
          return <Separator key={`sep-${idx}`} />
        }
        return (
          <BarButton
            key={action.command}
            icon={action.icon}
            label={action.label}
            variant={action.variant}
            disabled={action.disabled}
            shortcut={action.shortcut}
            onClick={handlerFor(action.command)}
          />
        )
      })}

      {/* Spacer */}
      <div className="flex-1" />

      {/* More button — placeholder for extension */}
      <button
        type="button"
        className="flex items-center justify-center h-8 w-8 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
        title="More actions"
      >
        <DotsThreeOutline size={15} weight="duotone" />
      </button>

      {/* Close */}
      {(has("close") || onClose) && (
        <button
          type="button"
          onClick={onClose}
          className="flex items-center justify-center h-8 w-8 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
          title="Close (Esc)"
        >
          <X size={15} weight="bold" />
        </button>
      )}
    </div>
  )
}
