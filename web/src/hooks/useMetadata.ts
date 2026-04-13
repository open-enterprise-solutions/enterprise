import { create } from "zustand"
import { api } from "@/lib/axios"

// Types matching the backend /api/metadata/tree response
export interface MetaObjectRef {
  id: string
  name: string
  fullName: string
  guid?: string
}

export interface MetadataTree {
  catalogs: MetaObjectRef[]
  documents: MetaObjectRef[]
  enumerations: MetaObjectRef[]
  reports?: MetaObjectRef[]
  dataProcessors?: MetaObjectRef[]
  informationRegisters?: MetaObjectRef[]
  accumulationRegisters?: MetaObjectRef[]
  accountingRegisters?: MetaObjectRef[]
  constants?: MetaObjectRef[]
}

interface MetadataState {
  tree: MetadataTree | null
  loading: boolean
  error: string | null
  lastFetchedAt: number | null

  load: () => Promise<void>
  reset: () => void
}

const CACHE_TTL_MS = 5 * 60 * 1000 // 5 minutes

export const useMetadataStore = create<MetadataState>((set, get) => ({
  tree: null,
  loading: false,
  error: null,
  lastFetchedAt: null,

  load: async () => {
    const { loading, lastFetchedAt } = get()

    // Prevent concurrent fetches and honour TTL cache
    if (loading) return
    if (lastFetchedAt && Date.now() - lastFetchedAt < CACHE_TTL_MS) return

    set({ loading: true, error: null })

    try {
      const response = await api.get<MetadataTree>("/metadata/tree")
      set({
        tree: response.data,
        loading: false,
        lastFetchedAt: Date.now(),
      })
    } catch (err) {
      const message =
        err instanceof Error ? err.message : "Failed to load metadata"
      set({ loading: false, error: message })
    }
  },

  reset: () => {
    set({ tree: null, loading: false, error: null, lastFetchedAt: null })
  },
}))

// Convenience hook — loads on first access, returns stable references
export function useMetadata() {
  const { tree, loading, error, load } = useMetadataStore()
  return { tree, loading, error, load }
}
