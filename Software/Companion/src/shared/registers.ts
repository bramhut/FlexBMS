export type RegisterField = { bits: number; name: string }

// Compact BCC register descriptions retained from the maintained BMS Companion.
// Unknown addresses remain readable and are shown as a raw 16-bit value.
export const registerFields: Record<string, RegisterField[]> = {
  '03': [
    { bits: 3, name: 'CYCLIC TIMER' }, { bits: 3, name: 'DIAG TIMEOUT' },
    { bits: 1, name: 'I MEAS EN' }, { bits: 1, name: 'RESERVED' },
    { bits: 1, name: 'CB DRIVEN' }, { bits: 1, name: 'DIAG ST' },
    { bits: 1, name: 'CB MANUAL PAUSE' }, { bits: 1, name: 'RESERVED' },
    { bits: 1, name: 'FAULT WAVE' }, { bits: 2, name: 'WAVE DC BITx' },
    { bits: 1, name: 'x' },
  ],
}
