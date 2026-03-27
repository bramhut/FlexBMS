
type RegisterValue = {
    length: number;
    name: string;
}

export const registerValues: Record<string, RegisterValue[]> = {
    '03': [
        { length: 3, name: 'CYCLIC TIMER' },
        { length: 3, name: 'DIAG TIMEOUT' },
        { length: 1, name: 'I MEAS EN' },
        { length: 1, name: '-' },
        { length: 1, name: 'CB DRIVEN' },
        { length: 1, name: 'DIAG ST' },
        { length: 1, name: 'CB MANUAL PAUSE' },
        { length: 1, name: '-' },
        { length: 1, name: 'FAULT WAVE' },
        { length: 2, name: 'WAVE DC BITx' },
        { length: 1, name: 'x' }
    ]
};