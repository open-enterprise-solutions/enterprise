import { useRef } from "react"
import {
  FloppyDisk,
  Play,
  DownloadSimple,
  UploadSimple,
  Database,
  ArrowCounterClockwise,
  Spinner,
} from "@phosphor-icons/react"
import { api } from "@/lib/axios"

interface DesignerToolbarProps {
  activeModuleId: string | null
  compiling: boolean
  saving: boolean
  onSave: () => void
  onCompile: () => void
  onUpdateDb: () => void
  onImportSuccess: () => void
}

export function DesignerToolbar({
  activeModuleId,
  compiling,
  saving,
  onSave,
  onCompile,
  onUpdateDb,
  onImportSuccess,
}: DesignerToolbarProps) {
  const fileInputRef = useRef<HTMLInputElement>(null)

  // Export configuration as JSON download
  async function handleExport() {
    try {
      const response = await api.post(
        "/designer/export/json",
        {},
        { responseType: "blob" },
      )
      const url = URL.createObjectURL(response.data as Blob)
      const a = document.createElement("a")
      a.href = url
      a.download = `oes-config-${new Date().toISOString().slice(0, 10)}.json`
      a.click()
      URL.revokeObjectURL(url)
    } catch {
      // Error will be surfaced by the parent via CompileConsole
    }
  }

  // Import configuration from JSON file
  async function handleImportFile(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0]
    if (!file) return
    const text = await file.text()
    try {
      const json = JSON.parse(text)
      await api.post("/designer/import/json", json)
      onImportSuccess()
    } catch {
      // Error surfaced by parent
    } finally {
      // Reset so the same file can be re-imported
      e.target.value = ""
    }
  }

  return (
    <div className="flex h-10 shrink-0 items-center gap-1 border-b border-[hsl(var(--border))] bg-[hsl(var(--sidebar))] px-2">
      {/* Save */}
      <ToolbarButton
        onClick={onSave}
        disabled={!activeModuleId || saving}
        title="Save module (Ctrl+S)"
        icon={
          saving
            ? <Spinner size={15} weight="duotone" className="animate-spin" />
            : <FloppyDisk size={15} weight="duotone" />
        }
        label="Save"
      />

      {/* Compile */}
      <ToolbarButton
        onClick={onCompile}
        disabled={!activeModuleId || compiling}
        title="Compile (F7)"
        icon={
          compiling
            ? <Spinner size={15} weight="duotone" className="animate-spin" />
            : <Play size={15} weight="duotone" />
        }
        label="Compile"
      />

      <div className="mx-1 h-5 w-px bg-[hsl(var(--border))]" />

      {/* Export JSON */}
      <ToolbarButton
        onClick={handleExport}
        title="Export configuration to JSON (Ctrl+Shift+E)"
        icon={<DownloadSimple size={15} weight="duotone" />}
        label="Export JSON"
      />

      {/* Import JSON */}
      <ToolbarButton
        onClick={() => fileInputRef.current?.click()}
        title="Import configuration from JSON"
        icon={<UploadSimple size={15} weight="duotone" />}
        label="Import JSON"
      />
      <input
        ref={fileInputRef}
        type="file"
        accept=".json"
        className="hidden"
        onChange={handleImportFile}
      />

      <div className="mx-1 h-5 w-px bg-[hsl(var(--border))]" />

      {/* Update DB */}
      <ToolbarButton
        onClick={onUpdateDb}
        title="Update database structure"
        icon={<Database size={15} weight="duotone" />}
        label="Update DB"
      />

      {/* Reload metadata */}
      <ToolbarButton
        onClick={() => window.location.reload()}
        title="Reload configuration"
        icon={<ArrowCounterClockwise size={15} weight="duotone" />}
        label="Reload"
      />
    </div>
  )
}

// ---------------------------------------------------------------------------
// Reusable toolbar button
// ---------------------------------------------------------------------------

interface ToolbarButtonProps {
  onClick: () => void
  disabled?: boolean
  title?: string
  icon: React.ReactNode
  label: string
}

function ToolbarButton({ onClick, disabled, title, icon, label }: ToolbarButtonProps) {
  return (
    <button
      type="button"
      onClick={onClick}
      disabled={disabled}
      title={title}
      className={[
        "flex h-7 items-center gap-1.5 rounded-[2px] px-2 text-[11px] font-medium transition-colors",
        "border border-transparent",
        disabled
          ? "cursor-not-allowed opacity-40 text-[hsl(var(--muted-foreground))]"
          : "text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))] hover:border-[hsl(var(--border))]",
      ].join(" ")}
    >
      {icon}
      <span>{label}</span>
    </button>
  )
}
