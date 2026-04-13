import { useState, type ReactNode } from "react"
import {
  House,
  FileText,
  Package,
  ChartBar,
  Gear,
  Users,
  MagnifyingGlass,
  Bell,
  Moon,
  Sun,
  List,
} from "@phosphor-icons/react"

interface Section {
  id: string
  label: string
  icon: ReactNode
}

const sections: Section[] = [
  { id: "home", label: "Home", icon: <House size={22} weight="duotone" /> },
  { id: "documents", label: "Documents", icon: <FileText size={22} weight="duotone" /> },
  { id: "catalogs", label: "Catalogs", icon: <Package size={22} weight="duotone" /> },
  { id: "reports", label: "Reports", icon: <ChartBar size={22} weight="duotone" /> },
  { id: "users", label: "Users", icon: <Users size={22} weight="duotone" /> },
  { id: "settings", label: "Settings", icon: <Gear size={22} weight="duotone" /> },
]

interface AppShellProps {
  children?: ReactNode
}

export function AppShell({ children }: AppShellProps) {
  const [activeSection, setActiveSection] = useState("home")
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false)
  const [darkMode, setDarkMode] = useState(false)

  const toggleDarkMode = () => {
    setDarkMode(!darkMode)
    document.documentElement.classList.toggle("dark")
  }

  return (
    <div className="flex h-screen overflow-hidden density-compact">
      {/* Sidebar — vertical icon menu (8.5-style) */}
      <aside
        className={`flex flex-col border-r border-[hsl(var(--sidebar-border))] bg-[hsl(var(--sidebar))] transition-all duration-150 ${
          sidebarCollapsed ? "w-14" : "w-56"
        }`}
      >
        {/* Logo / collapse toggle */}
        <div className="flex h-12 items-center justify-center border-b border-[hsl(var(--sidebar-border))]">
          <button
            onClick={() => setSidebarCollapsed(!sidebarCollapsed)}
            className="p-2 rounded-[var(--radius)] hover:bg-[hsl(var(--secondary))] transition-colors"
          >
            <List size={20} />
          </button>
        </div>

        {/* Section nav */}
        <nav className="flex-1 py-2">
          {sections.map((section) => (
            <button
              key={section.id}
              onClick={() => setActiveSection(section.id)}
              className={`flex w-full items-center gap-3 px-3 py-2 text-sm transition-colors ${
                activeSection === section.id
                  ? "bg-[hsl(var(--sidebar-accent)/0.1)] text-[hsl(var(--sidebar-accent))] font-medium"
                  : "text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]"
              }`}
              title={section.label}
            >
              {section.icon}
              {!sidebarCollapsed && <span>{section.label}</span>}
            </button>
          ))}
        </nav>
      </aside>

      {/* Main content area */}
      <div className="flex flex-1 flex-col overflow-hidden">
        {/* Top toolbar */}
        <header className="flex h-12 items-center gap-4 border-b border-[hsl(var(--border))] px-4">
          {/* Global search */}
          <div className="flex flex-1 items-center gap-2 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-1.5 text-sm">
            <MagnifyingGlass size={16} className="text-[hsl(var(--muted-foreground))]" />
            <input
              type="text"
              placeholder="Search..."
              className="flex-1 bg-transparent outline-none placeholder:text-[hsl(var(--muted-foreground))]"
            />
            <kbd className="hidden text-xs text-[hsl(var(--muted-foreground))] sm:inline">Ctrl+F</kbd>
          </div>

          {/* Actions */}
          <button className="p-2 rounded-[var(--radius)] hover:bg-[hsl(var(--secondary))] transition-colors relative">
            <Bell size={18} />
          </button>
          <button
            onClick={toggleDarkMode}
            className="p-2 rounded-[var(--radius)] hover:bg-[hsl(var(--secondary))] transition-colors"
          >
            {darkMode ? <Sun size={18} /> : <Moon size={18} />}
          </button>
          <div className="h-8 w-8 rounded-full bg-[hsl(var(--primary))] flex items-center justify-center text-[hsl(var(--primary-foreground))] text-xs font-medium">
            A
          </div>
        </header>

        {/* Tab bar (placeholder) */}
        <div className="flex h-9 items-center gap-1 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.5)] px-2 text-xs">
          <div className="flex items-center gap-1 rounded-[var(--radius)] bg-[hsl(var(--background))] px-3 py-1 font-medium shadow-sm">
            <House size={14} />
            <span>Dashboard</span>
          </div>
        </div>

        {/* Content */}
        <main className="flex-1 overflow-auto p-4">
          {children ?? (
            <div className="flex flex-col items-center justify-center h-full text-[hsl(var(--muted-foreground))]">
              <Package size={48} weight="duotone" className="mb-4 opacity-50" />
              <h2 className="text-lg font-medium mb-1">OES Enterprise Web Client</h2>
              <p className="text-sm">Phase 0 — Project Setup Complete</p>
              <p className="text-xs mt-2 opacity-50">
                Active section: {activeSection}
              </p>
            </div>
          )}
        </main>
      </div>
    </div>
  )
}
