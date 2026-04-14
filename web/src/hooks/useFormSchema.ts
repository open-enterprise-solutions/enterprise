import { useState, useEffect, useRef } from "react"
import { api } from "@/lib/axios"

export interface OesFieldMeta {
  metaType?: string
  refResource?: string
  [key: string]: unknown
}

export interface OesFormProperty {
  type: string
  title?: string
  "x-component"?: string
  "x-component-props"?: Record<string, unknown>
  "x-decorator"?: string
  "x-decorator-props"?: Record<string, unknown>
  "x-oes-meta"?: OesFieldMeta
  "x-validator"?: unknown
  "x-reactions"?: unknown
  enum?: Array<{ label: string; value: unknown }>
  items?: OesFormSchema
  properties?: Record<string, OesFormProperty>
  required?: boolean
}

export interface OesObjectMeta {
  metaType: "catalog" | "document" | "enumeration" | "dataProcessor" | "report" | string
  name: string
  synonym?: string
  guid?: string
}

export type OesCommand = "save" | "delete" | "copy" | "post" | "unpost" | "close" | string

export interface OesFormSchema {
  type: string
  "x-oes-meta"?: OesObjectMeta
  "x-oes-commands"?: OesCommand[]
  properties?: Record<string, OesFormProperty>
  required?: string[]
}

// In-memory schema cache — avoids duplicate fetches within the same session.
const schemaCache = new Map<string, OesFormSchema>()

interface UseFormSchemaResult {
  schema: OesFormSchema | null
  isLoading: boolean
  error: string | null
  refetch: () => void
}

export function useFormSchema(resource: string): UseFormSchemaResult {
  const [schema, setSchema] = useState<OesFormSchema | null>(() => schemaCache.get(resource) ?? null)
  const [isLoading, setIsLoading] = useState<boolean>(!schemaCache.has(resource))
  const [error, setError] = useState<string | null>(null)
  const [version, setVersion] = useState(0)
  const abortRef = useRef<AbortController | null>(null)

  useEffect(() => {
    if (!resource) return

    // Return cached value immediately — no network call needed.
    const cached = schemaCache.get(resource)
    if (cached && version === 0) {
      setSchema(cached)
      setIsLoading(false)
      return
    }

    abortRef.current?.abort()
    const controller = new AbortController()
    abortRef.current = controller

    setIsLoading(true)
    setError(null)

    api
      .get<OesFormSchema>(`/form/${resource}/schema`, { signal: controller.signal })
      .then((res) => {
        schemaCache.set(resource, res.data)
        setSchema(res.data)
        setIsLoading(false)
      })
      .catch((err) => {
        if (err.name === "CanceledError" || err.name === "AbortError") return
        setError(err instanceof Error ? err.message : "Failed to load form schema")
        setIsLoading(false)
      })

    return () => {
      controller.abort()
    }
  }, [resource, version])

  return {
    schema,
    isLoading,
    error,
    refetch: () => {
      schemaCache.delete(resource)
      setVersion((v) => v + 1)
    },
  }
}
