import type { DataProvider, CrudFilter, CrudSort } from "@refinedev/core"
import { api, getApiUrl } from "@/lib/axios"

// Map Refine filter operators to OES query params
function buildFilterParams(filters: CrudFilter[]): Record<string, string> {
  const params: Record<string, string> = {}

  for (const filter of filters) {
    if ("field" in filter) {
      // LogicalFilter
      const key = `filter[${filter.field}]`
      const value = Array.isArray(filter.value)
        ? filter.value.join(",")
        : String(filter.value)

      switch (filter.operator) {
        case "eq":
          params[key] = value
          break
        case "ne":
          params[`filter[${filter.field}][ne]`] = value
          break
        case "lt":
          params[`filter[${filter.field}][lt]`] = value
          break
        case "lte":
          params[`filter[${filter.field}][lte]`] = value
          break
        case "gt":
          params[`filter[${filter.field}][gt]`] = value
          break
        case "gte":
          params[`filter[${filter.field}][gte]`] = value
          break
        case "contains":
          params[`filter[${filter.field}][like]`] = value
          break
        case "in":
          params[`filter[${filter.field}][in]`] = value
          break
        default:
          params[key] = value
      }
    }
    // ConditionalFilter (or/and) — skip for now, not supported by backend
  }

  return params
}

// Map Refine sorters to OES ?sort=field,-field2 format
function buildSortParam(sorters: CrudSort[]): string {
  return sorters
    .map((s) => (s.order === "desc" ? `-${s.field}` : s.field))
    .join(",")
}

export const oesDataProvider: DataProvider = {
  getList: async ({ resource, pagination, sorters, filters }) => {
    const page = pagination?.currentPage ?? 1
    const pageSize = pagination?.pageSize ?? 25

    const params: Record<string, string | number> = { page, pageSize }

    if (sorters && sorters.length > 0) {
      params.sort = buildSortParam(sorters)
    }

    if (filters && filters.length > 0) {
      Object.assign(params, buildFilterParams(filters))
    }

    const response = await api.get(`/data/${resource}`, { params })
    const body = response.data

    // Strip internal _type suffix keys added by the backend
    const rawData: Record<string, unknown>[] = body.data ?? []
    const cleanedData = rawData.map((row) => {
      const clean: Record<string, unknown> = {}
      for (const [k, v] of Object.entries(row)) {
        if (!k.endsWith("_type")) clean[k] = v
      }
      return clean
    })

    // Backend returns: { data: [...], meta: { total, page, pageSize } }
    return {
      data: cleanedData as any[],
      total: body.meta?.total ?? body.total ?? 0,
    }
  },

  getOne: async ({ resource, id }) => {
    const response = await api.get(`/data/${resource}/${id}`)
    // Backend returns: { data: {...} }
    return { data: response.data.data ?? response.data }
  },

  create: async ({ resource, variables }) => {
    const response = await api.post(`/data/${resource}`, variables)
    return { data: response.data.data ?? response.data }
  },

  update: async ({ resource, id, variables }) => {
    const response = await api.put(`/data/${resource}/${id}`, variables)
    return { data: response.data.data ?? response.data }
  },

  deleteOne: async ({ resource, id }) => {
    const response = await api.delete(`/data/${resource}/${id}`)
    return { data: response.data.data ?? response.data }
  },

  getApiUrl,
}
