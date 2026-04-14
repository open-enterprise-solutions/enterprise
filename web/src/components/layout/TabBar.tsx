import { useEffect, useRef } from "react"
import { useNavigate, useLocation } from "react-router-dom"
import {
  X,
  House,
  FileText,
  Package,
  ChartBar,
  Table,
  ListDashes,
  Gear,
} from "@phosphor-icons/react"
import { useTabStore, type Tab } from "@/stores/tab-store"

// Map icon names to Phosphor components
function TabIcon({ name, size = 13 }: { name?: string; size?: number }) {
  const props = { size, weight: "duotone" as const }
  switch (name) {
    case "house":       return <House {...props} />
    case "file-text":   return <FileText {...props} />
    case "package":     return <Package {...props} />
    case "chart-bar":   return <ChartBar {...props} />
    case "table":       return <Table {...props} />
    case "list":        return <ListDashes {...props} />
    case "gear":        return <Gear {...props} />
    default:            return <ListDashes {...props} />
  }
}

interface TabItemProps {
  tab: Tab
  isActive: boolean
  onActivate: (tab: Tab) => void
  onClose: (id: string) => void
}

function TabItem({ tab, isActive, onActivate, onClose }: TabItemProps) {
  return (
    <div
      role="tab"
      aria-selected={isActive}
      className={[
        "group relative flex shrink-0 items-center gap-1.5 h-full px-3 cursor-pointer select-none",
        "border-r border-[hsl(var(--border))] text-[11px] font-medium max-w-[180px]",
        "transition-colors",
        isActive
          ? "bg-[hsl(var(--background))] text-[hsl(var(--foreground))] after:absolute after:bottom-0 after:left-0 after:right-0 after:h-[2px] after:bg-[hsl(var(--primary))]"
          : "bg-transparent text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
      ].join(" ")}
      onClick={() => onActivate(tab)}
    >
      <TabIcon name={tab.icon} size={13} />
      <span className="truncate leading-none">{tab.title}</span>

      {tab.closable && (
        <button
          type="button"
          aria-label={`Close ${tab.title}`}
          onClick={(e) => {
            e.stopPropagation()
            onClose(tab.id)
          }}
          className={[
            "ml-0.5 flex shrink-0 items-center justify-center w-4 h-4",
            "rounded-[var(--radius)] transition-colors",
            "opacity-0 group-hover:opacity-60 hover:!opacity-100",
            isActive ? "opacity-40" : "",
            "hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
          ].join(" ")}
        >
          <X size={10} weight="bold" />
        </button>
      )}
    </div>
  )
}

export function TabBar() {
  const { tabs, activeTabId, removeTab, setActive } = useTabStore()
  const navigate = useNavigate()
  const location = useLocation()
  const scrollRef = useRef<HTMLDivElement>(null)

  // Sync active tab when location changes (back/forward navigation)
  useEffect(() => {
    const match = tabs.find((t) => t.path === location.pathname)
    if (match && match.id !== activeTabId) {
      setActive(match.id)
    }
  }, [location.pathname, tabs, activeTabId, setActive])

  // Scroll active tab into view
  useEffect(() => {
    if (!scrollRef.current || !activeTabId) return
    const el = scrollRef.current.querySelector(`[data-tab-id="${activeTabId}"]`) as HTMLElement | null
    el?.scrollIntoView({ block: "nearest", inline: "nearest" })
  }, [activeTabId])

  const handleActivate = (tab: Tab) => {
    setActive(tab.id)
    navigate(tab.path)
  }

  const handleClose = (id: string) => {
    const { activeTabId: currentActive } = useTabStore.getState()
    const closingActive = currentActive === id

    removeTab(id)

    if (closingActive) {
      // Navigate to the new active tab after removal
      const { activeTabId: nextActive, tabs: nextTabs } = useTabStore.getState()
      const nextTab = nextTabs.find((t) => t.id === nextActive)
      if (nextTab) {
        navigate(nextTab.path)
      } else {
        navigate("/")
      }
    }
  }

  if (tabs.length === 0) return null

  return (
    <div
      role="tablist"
      className="flex h-9 overflow-hidden border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.4)]"
    >
      <div
        ref={scrollRef}
        className="flex flex-1 overflow-x-auto overflow-y-hidden"
        style={{ scrollbarWidth: "none" }}
      >
        {tabs.map((tab) => (
          <div key={tab.id} data-tab-id={tab.id} className="h-full">
            <TabItem
              tab={tab}
              isActive={tab.id === activeTabId}
              onActivate={handleActivate}
              onClose={handleClose}
            />
          </div>
        ))}
      </div>
    </div>
  )
}
