import type { ServiceResult } from '../transports/Transport'
export const serviceResultLabel = (result: ServiceResult): string => ({ ok: 'Accepted', denied: 'Denied', invalid: 'Invalid', busy: 'Busy', transport_error: 'Transport error' })[result]
