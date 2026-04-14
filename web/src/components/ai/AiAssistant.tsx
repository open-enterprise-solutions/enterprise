import { useEffect, useRef } from "react"
import { clsx } from "clsx"
import {
  Robot,
  X,
  Minus,
  Shapes,
  ChatCircle,
} from "@phosphor-icons/react"
import { useAiAssistant } from "@/hooks/useAiAssistant"
import { AiChat } from "./AiChat"
import { McpToolList } from "./McpToolList"
import { useState } from "react"

// ---------------------------------------------------------------------------
// Tab type
// ---------------------------------------------------------------------------

type PanelTab = "chat" | "tools"

// ---------------------------------------------------------------------------
// Floating trigger button
// ---------------------------------------------------------------------------

interface AiTriggerButtonProps {
  onClick: () => void
  hasUnread?: boolean
}

function AiTriggerButton({ onClick, hasUnread }: AiTriggerButtonProps) {
  return (
    <button
      type="button"
      onClick={onClick}
      title="OES AI Assistant"
      className={clsx(
        "fixed bottom-5 right-5 z-40",
        "flex h-12 w-12 items-center justify-center",
        "rounded-full bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]",
        "shadow-lg hover:opacity-90 active:scale-95 transition-all",
        "border-2 border-[hsl(var(--primary-foreground)/0.15)]",
      )}
      aria-label="Open AI Assistant"
    >
      <Robot size={22} weight="duotone" />
      {hasUnread && (
        <span className="absolute top-0.5 right-0.5 w-3 h-3 rounded-full bg-[hsl(var(--accent))] border-2 border-[hsl(var(--primary))]" />
      )}
    </button>
  )
}

// ---------------------------------------------------------------------------
// Panel header
// ---------------------------------------------------------------------------

interface PanelHeaderProps {
  activeTab: PanelTab
  onTabChange: (tab: PanelTab) => void
  onMinimize: () => void
  onClose: () => void
}

function PanelHeader({ activeTab, onTabChange, onMinimize, onClose }: PanelHeaderProps) {
  const tabs: { id: PanelTab; label: string; icon: React.ReactNode }[] = [
    { id: "chat", label: "Chat", icon: <ChatCircle size={13} weight="duotone" /> },
    { id: "tools", label: "MCP Tools", icon: <Shapes size={13} weight="duotone" /> },
  ]

  return (
    <div className="flex items-center gap-0 border-b border-[hsl(var(--border))] bg-[hsl(var(--card))] shrink-0">
      {/* Tab buttons */}
      <div className="flex flex-1">
        {tabs.map((tab) => (
          <button
            key={tab.id}
            type="button"
            onClick={() => onTabChange(tab.id)}
            className={clsx(
              "flex items-center gap-1.5 px-3 py-2.5 text-[11px] font-medium",
              "border-b-2 transition-colors",
              activeTab === tab.id
                ? "border-[hsl(var(--primary))] text-[hsl(var(--primary))]"
                : "border-transparent text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]",
            )}
          >
            {tab.icon}
            {tab.label}
          </button>
        ))}
      </div>

      {/* Window controls */}
      <div className="flex items-center gap-0.5 pr-2">
        <button
          type="button"
          onClick={onMinimize}
          title="Minimize"
          className="p-1.5 rounded hover:bg-[hsl(var(--secondary))] transition-colors text-[hsl(var(--muted-foreground))]"
        >
          <Minus size={12} />
        </button>
        <button
          type="button"
          onClick={onClose}
          title="Close"
          className="p-1.5 rounded hover:bg-[hsl(var(--secondary))] transition-colors text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--destructive))]"
        >
          <X size={12} />
        </button>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Main AiAssistant component
// ---------------------------------------------------------------------------

interface AiAssistantProps {
  onApplyCode?: (code: string) => void
}

export function AiAssistant({ onApplyCode }: AiAssistantProps) {
  const { isOpen, open, close, messages } = useAiAssistant()
  const [minimized, setMinimized] = useState(false)
  const [activeTab, setActiveTab] = useState<PanelTab>("chat")
  const panelRef = useRef<HTMLDivElement>(null)

  // Trap focus when panel is open
  useEffect(() => {
    if (isOpen && !minimized) {
      panelRef.current?.focus()
    }
  }, [isOpen, minimized])

  // Close on Escape
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape" && isOpen) {
        close()
      }
    }
    window.addEventListener("keydown", handleKeyDown)
    return () => window.removeEventListener("keydown", handleKeyDown)
  }, [isOpen, close])

  const hasMessages = messages.length > 0

  if (!isOpen) {
    return <AiTriggerButton onClick={open} hasUnread={hasMessages} />
  }

  if (minimized) {
    return (
      <button
        type="button"
        onClick={() => setMinimized(false)}
        className={clsx(
          "fixed bottom-5 right-5 z-40",
          "flex items-center gap-2 px-4 py-2.5",
          "rounded-[var(--radius)] bg-[hsl(var(--card))] text-[hsl(var(--foreground))]",
          "border border-[hsl(var(--border))] shadow-lg",
          "hover:bg-[hsl(var(--secondary))] transition-colors",
          "text-[12px] font-medium",
        )}
      >
        <Robot size={15} weight="duotone" className="text-[hsl(var(--primary))]" />
        AI Assistant
      </button>
    )
  }

  return (
    <div
      ref={panelRef}
      tabIndex={-1}
      className={clsx(
        "fixed bottom-5 right-5 z-40",
        "flex flex-col w-[380px] h-[560px]",
        "rounded-[var(--radius)] border border-[hsl(var(--border))]",
        "bg-[hsl(var(--card))] shadow-xl",
        "outline-none",
      )}
      role="dialog"
      aria-label="OES AI Assistant"
    >
      <PanelHeader
        activeTab={activeTab}
        onTabChange={setActiveTab}
        onMinimize={() => setMinimized(true)}
        onClose={close}
      />

      {/* Panel body */}
      <div className="flex-1 overflow-hidden">
        {activeTab === "chat" && (
          <AiChat onApplyCode={onApplyCode} />
        )}
        {activeTab === "tools" && (
          <div className="h-full overflow-y-auto p-3">
            <McpToolList />
          </div>
        )}
      </div>
    </div>
  )
}
