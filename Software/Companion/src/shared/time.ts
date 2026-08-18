export const advancingUnixTime = (unixTime: number, sampledAtMs: number, nowMs = Date.now()): number => unixTime + Math.floor((nowMs - sampledAtMs) / 1000)

export const advancingUptimeMs = (uptimeMs: number, sampledAtMs: number, nowMs = Date.now()): number => uptimeMs + Math.max(0, nowMs - sampledAtMs)

export const formatUptime = (uptimeMs: number): string => {
  const totalSeconds = Math.floor(uptimeMs / 1000)
  const days = Math.floor(totalSeconds / 86400)
  const hours = Math.floor(totalSeconds % 86400 / 3600)
  const minutes = Math.floor(totalSeconds % 3600 / 60)
  const seconds = totalSeconds % 60
  if (days > 0) return `${days} d ${hours} h ${minutes} min`
  if (hours > 0) return `${hours} h ${minutes} min`
  if (minutes > 0) return `${minutes} min ${seconds} s`
  return `${seconds} s`
}
