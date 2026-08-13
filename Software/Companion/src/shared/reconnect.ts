export const reconnectDelayMs = (attempt: number) => Math.min(1000 * 2 ** Math.max(0, attempt), 10000)
