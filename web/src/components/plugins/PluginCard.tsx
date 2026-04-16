import { useState } from "react"
import { clsx } from "clsx"
import {
  PuzzlePiece,
  Gear,
  Trash,
  Warning,
  User,
  Tag,
} from "@phosphor-icons/react"
import { type Plugin, usePlugins } from "@/hooks/usePlugins"

// ---------------------------------------------------------------------------
// Toggle switch
// ---------------------------------------------------------------------------

interface ToggleSwitchProps {
  checked: boolean
  onChange: () => void
  disabled?: boolean
  label: string
}

function ToggleSwitch({ checked, onChange, disabled, label }: ToggleSwitchProps) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      disabled={disabled}
      onClick={onChange}
      className={clsx(
        "relative inline-flex h-5 w-9 shrink-0 cursor-pointer rounded-full border-2 border-transparent",
        "transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-[hsl(var(--ring))]",
        "disabled:cursor-not-allowed disabled:opacity-50",
        checked ? "bg-[hsl(var(--primary))]" : "bg-[hsl(var(--muted))]",
      )}
    >
      <span
        className={clsx(
          "pointer-events-none block h-4 w-4 rounded-full bg-white shadow-sm",
          "transform transition-transform",
          checked ? "translate-x-4" : "translate-x-0",
        )}
      />
    </button>
  )
}

// ---------------------------------------------------------------------------
// Confirm uninstall dialog
// ---------------------------------------------------------------------------

interface ConfirmUninstallProps {
  plugin: Plugin
  onConfirm: () => void
  onCancel: () => void
}

function ConfirmUninstall({ plugin, onConfirm, onCancel }: ConfirmUninstallProps) {
  return (
    <div className="mt-3 p-3 rounded-[var(--radius)] bg-[hsl(var(--destructive)/0.06)] border border-[hsl(var(--destructive)/0.2)]">
      <div className="flex gap-2 items-start">
        <Warning size={15} weight="duotone" className="text-[hsl(var(--destructive))] shrink-0 mt-0.5" />
        <div className="flex-1">
          <p className="text-[11px] font-medium text-[hsl(var(--foreground))]">
            Uninstall "{plugin.name}"?
          </p>
          <p className="text-[10px] text-[hsl(var(--muted-foreground))] mt-0.5">
            This action cannot be undone.
          </p>
          <div className="flex gap-2 mt-2">
            <button
              type="button"
              onClick={onConfirm}
              className="px-2.5 py-1 text-[10px] font-medium rounded-[var(--radius)] bg-[hsl(var(--destructive))] text-[hsl(var(--destructive-foreground))] hover:opacity-90 transition-opacity"
            >
              Uninstall
            </button>
            <button
              type="button"
              onClick={onCancel}
              className="px-2.5 py-1 text-[10px] font-medium rounded-[var(--radius)] border border-[hsl(var(--border))] hover:bg-[hsl(var(--secondary))] transition-colors"
            >
              Cancel
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Main PluginCard
// ---------------------------------------------------------------------------

interface PluginCardProps {
  plugin: Plugin
  onSettingsClick?: (plugin: Plugin) => void
}

export function PluginCard({ plugin, onSettingsClick }: PluginCardProps) {
  const { togglePlugin, uninstallPlugin } = usePlugins()
  const [confirmingUninstall, setConfirmingUninstall] = useState(false)
  const [isLoading, setIsLoading] = useState(false)

  const handleToggle = async () => {
    setIsLoading(true)
    try {
      await togglePlugin(plugin.id)
    } finally {
      setIsLoading(false)
    }
  }

  const handleUninstall = async () => {
    setIsLoading(true)
    try {
      await uninstallPlugin(plugin.id)
    } finally {
      setIsLoading(false)
      setConfirmingUninstall(false)
    }
  }

  return (
    <div
      className={clsx(
        "rounded-[var(--radius)] border border-[hsl(var(--border))]",
        "bg-[hsl(var(--card))] p-4 transition-colors",
        !plugin.enabled && "opacity-70",
      )}
    >
      {/* Header row */}
      <div className="flex items-start gap-3">
        {/* Icon */}
        <div className="w-10 h-10 rounded-[var(--radius)] bg-[hsl(var(--muted))] flex items-center justify-center shrink-0">
          <PuzzlePiece size={20} weight="duotone" className="text-[hsl(var(--primary))]" />
        </div>

        {/* Info */}
        <div className="flex-1 min-w-0">
          <div className="flex items-center justify-between gap-2">
            <h3 className="text-[13px] font-semibold text-[hsl(var(--foreground))] truncate">
              {plugin.name}
            </h3>
            <ToggleSwitch
              checked={plugin.enabled}
              onChange={() => void handleToggle()}
              disabled={isLoading}
              label={`${plugin.enabled ? "Disable" : "Enable"} ${plugin.name}`}
            />
          </div>

          {/* Meta row */}
          <div className="flex items-center gap-3 mt-0.5">
            <span className="flex items-center gap-1 text-[10px] text-[hsl(var(--muted-foreground))]">
              <Tag size={10} weight="duotone" />
              v{plugin.version}
            </span>
            <span className="flex items-center gap-1 text-[10px] text-[hsl(var(--muted-foreground))]">
              <User size={10} weight="duotone" />
              {plugin.author}
            </span>
          </div>

          {/* Status badge */}
          <span
            className={clsx(
              "inline-block mt-1 text-[9px] px-1.5 py-0.5 rounded-[var(--radius)] uppercase tracking-wide font-medium",
              plugin.enabled
                ? "bg-[hsl(var(--accent)/0.15)] text-[hsl(var(--accent))]"
                : "bg-[hsl(var(--muted))] text-[hsl(var(--muted-foreground))]",
            )}
          >
            {plugin.enabled ? "Active" : "Disabled"}
          </span>
        </div>
      </div>

      {/* Description */}
      <p className="mt-3 text-[11px] text-[hsl(var(--muted-foreground))] leading-relaxed">
        {plugin.description}
      </p>

      {/* Actions */}
      <div className="flex items-center justify-between mt-3 pt-3 border-t border-[hsl(var(--border))]">
        <button
          type="button"
          onClick={() => onSettingsClick?.(plugin)}
          className={clsx(
            "flex items-center gap-1.5 px-2.5 py-1.5 text-[11px]",
            "rounded-[var(--radius)] border border-[hsl(var(--border))]",
            "hover:bg-[hsl(var(--secondary))] transition-colors",
            "text-[hsl(var(--muted-foreground))]",
          )}
        >
          <Gear size={12} weight="duotone" />
          Settings
        </button>

        <button
          type="button"
          onClick={() => setConfirmingUninstall(true)}
          disabled={isLoading}
          className={clsx(
            "flex items-center gap-1.5 px-2.5 py-1.5 text-[11px]",
            "rounded-[var(--radius)] border border-transparent",
            "hover:border-[hsl(var(--destructive)/0.3)] hover:bg-[hsl(var(--destructive)/0.06)]",
            "transition-colors text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--destructive))]",
            "disabled:opacity-50 disabled:cursor-not-allowed",
          )}
        >
          <Trash size={12} weight="duotone" />
          Uninstall
        </button>
      </div>

      {/* Confirm uninstall */}
      {confirmingUninstall && (
        <ConfirmUninstall
          plugin={plugin}
          onConfirm={() => void handleUninstall()}
          onCancel={() => setConfirmingUninstall(false)}
        />
      )}
    </div>
  )
}
