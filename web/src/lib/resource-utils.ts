// Maps URL section names to API singular type names
const SECTION_TO_TYPE: Record<string, string> = {
  catalogs: "catalog",
  documents: "document",
  enumerations: "enumeration",
  informationRegisters: "informationRegister",
  accumulationRegisters: "accumulationRegister",
  accountingRegisters: "accountingRegister",
  reports: "report",
  dataProcessors: "dataProcessor",
  constants: "constant",
}

/**
 * Convert URL params (section + resource name) to API resource string.
 * e.g. ("catalogs", "Products") → "catalog.Products"
 */
export function toApiResource(section: string, resource: string): string {
  const apiType = SECTION_TO_TYPE[section] ?? section
  return `${apiType}.${resource}`
}

/**
 * Parse an API resource string back to section + name.
 * e.g. "catalog.Products" → { section: "catalogs", name: "Products" }
 */
export function parseApiResource(apiResource: string): { section: string; name: string } {
  const dot = apiResource.indexOf(".")
  if (dot === -1) return { section: apiResource, name: apiResource }

  const type = apiResource.substring(0, dot)
  const name = apiResource.substring(dot + 1)

  // Reverse lookup
  const section = Object.entries(SECTION_TO_TYPE).find(([, v]) => v === type)?.[0] ?? type
  return { section, name }
}

export { SECTION_TO_TYPE }
