import { create } from "zustand"
import { persist } from "zustand/middleware"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface Plugin {
  id: string
  name: string
  version: string
  description: string
  author: string
  enabled: boolean
  config?: Record<string, unknown>
  installedAt?: number
}

interface PluginsState {
  plugins: Plugin[]
  loading: boolean
  error: string | null

  loadPlugins: () => Promise<void>
  installPlugin: (file: File) => Promise<void>
  togglePlugin: (id: string) => Promise<void>
  uninstallPlugin: (id: string) => Promise<void>
  dismissError: () => void
}

// ---------------------------------------------------------------------------
// Default built-in plugins for demo purposes
// ---------------------------------------------------------------------------

const BUILTIN_PLUGINS: Plugin[] = [
  {
    id: "oes-mcp-bridge",
    name: "OES MCP Bridge",
    version: "1.0.0",
    description: "Connects OES Designer to Model Context Protocol for AI-driven configuration generation.",
    author: "OES Enterprise",
    enabled: true,
    installedAt: Date.now(),
  },
  {
    id: "oes-vibe-coder",
    name: "Vibe Coder",
    version: "0.9.0",
    description: "Natural language to OES configuration. Describe your business task — AI creates the full data model.",
    author: "OES Enterprise",
    enabled: true,
    installedAt: Date.now(),
  },
  {
    id: "oes-1c-importer",
    name: "1C:Enterprise Importer",
    version: "1.2.0",
    description: "Import configurations from 1C:Enterprise 8.3 format (*.cf, *.cfe). Converts metadata objects and modules.",
    author: "Tetracode",
    enabled: false,
    installedAt: Date.now(),
  },
]

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

export const usePlugins = create<PluginsState>()(
  persist(
    (set, get) => ({
      plugins: BUILTIN_PLUGINS,
      loading: false,
      error: null,

      dismissError: () => set({ error: null }),

      loadPlugins: async () => {
        set({ loading: true, error: null })
        try {
          const response = await api.get<Plugin[]>("/plugins")
          set({ plugins: response.data, loading: false })
        } catch {
          // If API is not available, keep the current state (built-ins)
          set({ loading: false })
        }
      },

      installPlugin: async (file: File) => {
        set({ loading: true, error: null })
        try {
          const text = await file.text()
          const manifest = JSON.parse(text) as Omit<Plugin, "installedAt" | "enabled">

          // Validate required fields
          if (!manifest.id || !manifest.name || !manifest.version) {
            throw new Error("Invalid plugin manifest: missing id, name, or version")
          }

          const { plugins } = get()
          const existing = plugins.find((p) => p.id === manifest.id)
          if (existing) {
            throw new Error(`Plugin "${manifest.name}" is already installed`)
          }

          try {
            await api.post("/plugins", manifest)
            const response = await api.get<Plugin[]>("/plugins")
            set({ plugins: response.data, loading: false })
          } catch {
            // Fallback: install locally
            const newPlugin: Plugin = {
              ...manifest,
              enabled: true,
              installedAt: Date.now(),
            }
            set({ plugins: [...plugins, newPlugin], loading: false })
          }
        } catch (err) {
          const message = err instanceof Error ? err.message : "Failed to install plugin"
          set({ loading: false, error: message })
        }
      },

      togglePlugin: async (id: string) => {
        const { plugins } = get()
        const plugin = plugins.find((p) => p.id === id)
        if (!plugin) return

        const updated = { ...plugin, enabled: !plugin.enabled }

        // Optimistic update
        set({ plugins: plugins.map((p) => (p.id === id ? updated : p)) })

        try {
          await api.put(`/plugins/${id}`, { enabled: updated.enabled })
        } catch {
          // Revert on failure
          set({ plugins: plugins.map((p) => (p.id === id ? plugin : p)) })
        }
      },

      uninstallPlugin: async (id: string) => {
        const { plugins } = get()
        const plugin = plugins.find((p) => p.id === id)
        if (!plugin) return

        // Optimistic removal
        set({ plugins: plugins.filter((p) => p.id !== id) })

        try {
          await api.delete(`/plugins/${id}`)
        } catch {
          // Revert on failure
          set({ plugins })
        }
      },
    }),
    {
      name: "oes-plugins",
      partialize: (s) => ({ plugins: s.plugins }),
    },
  ),
)
