export const advancingUnixTime = (unixTime: number, sampledAtMs: number, nowMs = Date.now()): number => unixTime + Math.floor((nowMs - sampledAtMs) / 1000)
