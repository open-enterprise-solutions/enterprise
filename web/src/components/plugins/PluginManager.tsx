import { useEffect, useRef, useState } from "react"
import { clsx } from "clsx"
import {
  PuzzlePiece,
  UploadSimple,
  ArrowClockwise,
  MagnifyingGlass,
  FunnelSimple,
  Warning,
  X,
} from "@phosphor-icons/react"
import { usePlugins, type Plugin } from "@/hooks/usePlugins"
import { PluginCard } from "./PluginCard"

// ---------------------------------------------------------------------------
// Filter type
// ---------------------------------------------------------------------------

type FilterState = "all" | "enabled" | "disabled"

// ---------------------------------------------------------------------------
// Plugin settings modal (placeholder)
// ---------------------------------------------------------------------------

interface PluginSettingsModalProps {
  plugin: Plugin
  onClose: () => void
}

function PluginSettingsModal({ plugin, onClose }: PluginSettingsModalProps) {
  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="w-[420px] rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--card))] shadow-xl">
        <div className="flex items-center justify-between px-4 py-3 border-b border-[hsl(var(--border))]">
          <h2 className="text-[13px] font-semibold">{plugin.name} — Settings</h2>
          <button
            type="button"
            onClick={onClose}
            className="p-1 rounded hover:bg-[hsl(var(--secondary))] transition-colors text-[hsl(var(--muted-foreground))]"
          >
            <X size={14} />
          </button>
        </div>
        <div className="p-4">
          {plugin.config && Object.keys(plugin.config).length > 0 ? (
            <div className="space-y-3">
              {Object.entries(plugin.config).map(([key, value]) => (
                <div key={key} className="flex items-center gap-3">
                  <label className="text-[11px] text-[hsl(var(--muted-foreground))] w-32 shrink-0">
                    {key}
                  </label>
                  <input
                    type="text"
                    defaultValue={String(value)}
                    className="flex-1 rounded-[var(--radius)] border border-[hsl(var(--input))] px-2.5 py-1.5 text-[12px] bg-[hsl(var(--background))] outline-none focus:border-[hsl(var(--ring))]"
                  />
                </div>
              ))}
            </div>
          ) : (
            <p className="text-[12px] text-[hsl(var(--muted-foreground))] text-center py-6">
              This plugin has no configurable settings.
            </p>
          )}
        </div>
        <div className="flex justify-end gap-2 px-4 py-3 border-t border-[hsl(var(--border))]">
          <button
            type="button"
            onClick={onClose}
            className="px-3 py-1.5 text-[12px] rounded-[var(--radius)] border border-[hsl(var(--border))] hover:bg-[hsl(var(--secondary))] transition-colors"
          >
            Close
          </button>
          <button
            type="button"
            onClick={onClose}
            className="px-3 py-1.5 text-[12px] rounded-[var(--radius)] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] hover:opacity-90 transition-opacity"
          >
            Save
          </button>
        </div>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Main PluginManager component
// ---------------------------------------------------------------------------

export function PluginManager() {
  const { plugins, loading, error, loadPlugins, installPlugin, dismissError } = usePlugins()
  const [search, setSearch] = useState("")
  const [filter, setFilter] = useState<FilterState>("all")
  const [settingsPlugin, setSettingsPlugin] = useState<Plugin | null>(null)
  const fileInputRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    void loadPlugins()
  }, [loadPlugins])

  // Filter + search
  const filtered = plugins.filter((p) => {
    const matchesSearch =
      !search ||
      p.name.toLowerCase().includes(search.toLowerCase()) ||
      p.description.toLowerCase().includes(search.toLowerCase()) ||
      p.author.toLowerCase().includes(search.toLowerCase())

    const matchesFilter =
      filter === "all" ||
      (filter === "enabled" && p.enabled) ||
      (filter === "disabled" && !p.enabled)

    return matchesSearch && matchesFilter
  })

  const handleFileInstall = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0]
    if (!file) return
    await installPlugin(file)
    e.target.value = ""
  }

  const enabledCount = plugins.filter((p) => p.enabled).length

  return (
    <div className="flex flex-col h-full">
      {/* Page header */}
      <div className="px-6 py-4 border-b border-[hsl(var(--border))] bg-[hsl(var(--card))] shrink-0">
        <div className="flex items-center justify-between gap-4">
          <div>
            <h1 className="text-[16px] font-semibold text-[hsl(var(--foreground))] flex items-center gap-2">
              <PuzzlePiece size={18} weight="duotone" className="text-[hsl(var(--primary))]" />
              Plugin Manager
            </h1>
            <p className="text-[11px] text-[hsl(var(--muted-foreground))] mt-0.5">
              {enabledCount} of {plugins.length} plugins active
            </p>
          </div>

          <div className="flex items-center gap-2">
            <button
              type="button"
              onClick={() => void loadPlugins()}
              disabled={loading}
              title="Refresh"
              className={clsx(
                "flex h-8 w-8 items-center justify-center rounded-[var(--radius)]",
                "border border-[hsl(var(--border))] hover:bg-[hsl(var(--secondary))]",
                "transition-colors text-[hsl(var(--muted-foreground))]",
                "disabled:opacity-50",
              )}
            >
              <ArrowClockwise size={14} className={loading ? "animate-spin" : ""} />
            </button>

            <input
              ref={fileInputRef}
              type="file"
              accept=".json"
              className="hidden"
              onChange={(e) => void handleFileInstall(e)}
            />
            <button
              type="button"
              onClick={() => fileInputRef.current?.click()}
              className={clsx(
                "flex items-center gap-1.5 px-3 py-1.5 text-[12px] font-medium",
                "rounded-[var(--radius)] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]",
                "hover:opacity-90 transition-opacity",
              )}
            >
              <UploadSimple size={14} weight="duotone" />
              Install Plugin
            </button>
          </div>
        </div>

        {/* Search + filter */}
        <div className="flex gap-2 mt-3">
          <div className="flex flex-1 items-center gap-2 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-1.5 max-w-[320px]">
            <MagnifyingGlass size={13} className="text-[hsl(var(--muted-foreground))] shrink-0" />
            <input
              type="text"
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              placeholder="Search plugins..."
              className="flex-1 text-[12px] bg-transparent outline-none placeholder:text-[hsl(var(--muted-foreground))]"
            />
          </div>

          <div className="flex items-center gap-1 rounded-[var(--radius)] border border-[hsl(var(--input))] p-0.5">
            <FunnelSimple size={13} className="text-[hsl(var(--muted-foreground))] ml-2" />
            {(["all", "enabled", "disabled"] as FilterState[]).map((f) => (
              <button
                key={f}
                type="button"
                onClick={() => setFilter(f)}
                className={clsx(
                  "px-2.5 py-1 text-[11px] rounded-[var(--radius)] transition-colors capitalize",
                  filter === f
                    ? "bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]"
                    : "text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]",
                )}
              >
                {f}
              </button>
            ))}
          </div>
        </div>
      </div>

      {/* Error banner */}
      {error && (
        <div className="mx-6 mt-3 px-3 py-2.5 rounded-[var(--radius)] bg-[hsl(var(--destructive)/0.08)] border border-[hsl(var(--destructive)/0.25)] text-[11px] text-[hsl(var(--destructive))] flex items-center gap-2">
          <Warning size={14} weight="duotone" />
          <span className="flex-1">{error}</span>
          <button type="button" onClick={dismissError} className="hover:opacity-70">
            <X size={12} />
          </button>
        </div>
      )}

      {/* Plugin grid */}
      <div className="flex-1 overflow-y-auto p-6">
        {loading && plugins.length === 0 ? (
          <div className="grid grid-cols-1 gap-4 sm:grid-cols-2">
            {[1, 2, 3].map((i) => (
              <div key={i} className="skeleton rounded-[var(--radius)] h-40" />
            ))}
          </div>
        ) : filtered.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-16 text-center">
            <PuzzlePiece size={40} weight="duotone" className="text-[hsl(var(--muted-foreground))] mb-3" />
            <p className="text-[13px] font-medium text-[hsl(var(--foreground))]">
              {search ? "No plugins match your search" : "No plugins installed"}
            </p>
            <p className="text-[11px] text-[hsl(var(--muted-foreground))] mt-1">
              {search
                ? "Try a different search term"
                : "Install a plugin by uploading a JSON manifest file"}
            </p>
          </div>
        ) : (
          <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 lg:grid-cols-3">
            {filtered.map((plugin) => (
              <PluginCard
                key={plugin.id}
                plugin={plugin}
                onSettingsClick={setSettingsPlugin}
              />
            ))}
          </div>
        )}
      </div>

      {/* Settings modal */}
      {settingsPlugin && (
        <PluginSettingsModal
          plugin={settingsPlugin}
          onClose={() => setSettingsPlugin(null)}
        />
      )}
    </div>
  )
}
