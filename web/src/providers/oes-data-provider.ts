import type { DataProvider } from "@refinedev/core"
import axios from "axios"

const api = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || "/api",
  timeout: Number(import.meta.env.VITE_API_TIMEOUT) || 30000,
})

api.interceptors.request.use((config) => {
  const token = localStorage.getItem("oes_token")
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

export const oesDataProvider: DataProvider = {
  getList: async ({ resource, pagination }) => {
    const page = pagination?.currentPage ?? 1
    const pageSize = pagination?.pageSize ?? 25
    const response = await api.get(`/data/${resource}`, {
      params: { page, pageSize },
    })
    return { data: response.data.items ?? [], total: response.data.total ?? 0 }
  },

  getOne: async ({ resource, id }) => {
    const response = await api.get(`/data/${resource}/${id}`)
    return { data: response.data }
  },

  create: async ({ resource, variables }) => {
    const response = await api.post(`/data/${resource}`, variables)
    return { data: response.data }
  },

  update: async ({ resource, id, variables }) => {
    const response = await api.put(`/data/${resource}/${id}`, variables)
    return { data: response.data }
  },

  deleteOne: async ({ resource, id }) => {
    const response = await api.delete(`/data/${resource}/${id}`)
    return { data: response.data }
  },

  getApiUrl: () => import.meta.env.VITE_API_BASE_URL || "/api",
}
