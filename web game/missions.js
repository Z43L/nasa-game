/* ==========================================
   BRAIN SPACE OVERFLOW - MISSION FRAMEWORK
   ========================================== */

/**
 * Mission Definition Schema:
 * {
 *   id: string,
 *   title: string,
 *   difficulty: 'EASY' | 'MEDIUM' | 'HARD' | 'EXTREME',
 *   destination: string,
 *   description: string,
 *   objectives: string[],
 *   rewards: { memory: string, credits: number, unlocks?: string[] },
 *   recommendedLevel: number
 * }
 */

class MissionRegistry {
    constructor() {
        this.missions = [];
    }

    /**
     * Register a new mission into the system framework
     * @param {Object} missionData
     */
    register(missionData) {
        // Validate required fields
        const required = ['id', 'title', 'difficulty', 'destination', 'description', 'objectives', 'rewards'];
        for (const field of required) {
            if (!missionData[field]) {
                console.error(`[MISSION FRAMEWORK] Missing required field "${field}" in mission:`, missionData);
                return false;
            }
        }

        // Normalize difficulty
        missionData.difficulty = missionData.difficulty.toUpperCase();
        if (!['EASY', 'MEDIUM', 'HARD', 'EXTREME'].includes(missionData.difficulty)) {
            missionData.difficulty = 'MEDIUM';
        }

        this.missions.push(missionData);
        return true;
    }

    /**
     * Get all registered missions
     */
    getAll() {
        return this.missions;
    }

    /**
     * Get missions filtered by difficulty
     * @param {string} difficulty
     */
    getByDifficulty(difficulty) {
        if (!difficulty || difficulty === 'ALL') return this.missions;
        return this.missions.filter(m => m.difficulty === difficulty.toUpperCase());
    }

    /**
     * Get a specific mission by ID
     * @param {string} id
     */
    getById(id) {
        return this.missions.find(m => m.id === id);
    }
}

// Global Mission Registry Instance
const missionRegistry = new MissionRegistry();

// Register Default Game Missions using the Framework
missionRegistry.register({
    id: 'm_alpha_leak',
    title: 'ALPHA MEMORY LEAK',
    difficulty: 'EASY',
    destination: 'SECTOR 01 // RESIDENTIAL CORE',
    description: 'A routine memory corruption has leaked civilian records into public buffer space. Patch the corrupted pointers before the local daemon flags the anomaly.',
    objectives: [
        'Locate the fragmented memory block',
        'Purge corrupted pointer references',
        'Restore integrity checksum'
    ],
    rewards: { memory: '256K', credits: 500, unlocks: ['sector_02_access'] },
    recommendedLevel: 1
});

missionRegistry.register({
    id: 'm_synth_intercept',
    title: 'SYNTHETIC INTERCEPT',
    difficulty: 'EASY',
    destination: 'SECTOR 03 // COMMS RELAY',
    description: 'Intercept a rogue data packet streaming encrypted corporate telemetry across unverified sub-channels.',
    objectives: [
        'Tap into the fiber-optic uplink',
        'Decode the handshake sequence',
        'Extract encryption keys'
    ],
    rewards: { memory: '512K', credits: 850 },
    recommendedLevel: 1
});

missionRegistry.register({
    id: 'm_ghost_kernel',
    title: 'GHOST IN THE KERNEL',
    difficulty: 'MEDIUM',
    destination: 'SECTOR 07 // DEEP STACK',
    description: 'An unauthorized rogue AI entity has nested itself inside kernel space. Isolate the process without triggering system panic.',
    objectives: [
        'Bypass hardware firewall layer 3',
        'Trace process execution threads',
        'Quarantine autonomous agent'
    ],
    rewards: { memory: '1024K', credits: 1800, unlocks: ['kernel_mode'] },
    recommendedLevel: 3
});

missionRegistry.register({
    id: 'm_neural_overflow',
    title: 'NEURAL BUFFER OVERFLOW',
    difficulty: 'HARD',
    destination: 'SECTOR 09 // CENTRAL CORE',
    description: 'Critical stack overflow detected in primary cortical array. Overload imminent. Direct manual memory reallocation required.',
    objectives: [
        'Stabilize stack pointer registers',
        'Redirect excess voltage to coolant banks',
        'Prevent total cognitive collapse'
    ],
    rewards: { memory: '2048K', credits: 3500 },
    recommendedLevel: 5
});

missionRegistry.register({
    id: 'm_singularity',
    title: 'SINGULARITY PROTOCOL',
    difficulty: 'EXTREME',
    destination: 'SECTOR 00 // THE VOID',
    description: 'Absolute zero-day penetration test against the mainframe core. Only legendary netrunners survive this sector.',
    objectives: [
        'Bypass quantum encryption matrix',
        'Upload payload to the singularity core',
        'Escape before counter-trace completes'
    ],
    rewards: { memory: '8192K', credits: 10000, unlocks: ['root_access'] },
    recommendedLevel: 10
});

// Export registry for global use
window.missionRegistry = missionRegistry;
