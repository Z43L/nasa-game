/* ==========================================
   BRAIN SPACE OVERFLOW - MISSION FRAMEWORK[cite: 3]
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
 *   budget: number,
 *   recommendedLevel: number
 * }
 */

class MissionRegistry {
    constructor() {
        this.missions = [];
    }

    register(missionData) {
        const required = ['id', 'title', 'difficulty', 'destination', 'description', 'objectives', 'rewards', 'budget'];
        for (const field of required) {
            if (missionData[field] === undefined) {
                console.error(`[MISSION FRAMEWORK] Missing required field "${field}" in mission:`, missionData);
                return false;
            }
        }

        missionData.difficulty = missionData.difficulty.toUpperCase();
        if (!['EASY', 'MEDIUM', 'HARD', 'EXTREME'].includes(missionData.difficulty)) {
            missionData.difficulty = 'MEDIUM';
        }

        this.missions.push(missionData);
        return true;
    }

    getAll() {
        return this.missions;
    }

    getByDifficulty(difficulty) {
        if (!difficulty || difficulty === 'ALL') return this.missions;
        return this.missions.filter(m => m.difficulty === difficulty.toUpperCase());
    }

    getById(id) {
        return this.missions.find(m => m.id === id);
    }
}

const missionRegistry = new MissionRegistry();

/* ==========================================================================
   MISSION CATALOG - NASA & DEEP SPACE EXPLORATION PROTOCOLS[cite: 3]
   ========================================================================== */

// --- SAMPLE / TRAINING MISSION[cite: 3] ---
missionRegistry.register({
    id: 'm_sample_training',
    title: 'ORBITAL TELEMETRY CALIBRATION',
    difficulty: 'EASY',
    destination: 'EARTH ORBIT // LEO STATION',
    description: 'Perform a basic diagnostic run on deep space telemetry transceivers to ensure communication links are fully operational.',
    objectives: [
        'Initialize LEO orbital transceiver array',
        'Verify buffer checksum and bandwidth stability',
        'Synchronize atomic clock offset'
    ],
    rewards: { memory: '128K', credits: 250, unlocks: ['basic_telemetry'] },
    budget: 950,
    recommendedLevel: 1
});

// --- 1. LUNAR & ARTEMIS PROGRAM MISSIONS[cite: 3] ---
missionRegistry.register({
    id: 'm_artemis_iii',
    title: 'ARTEMIS III: LUNAR SOUTH POLE DESCENT',
    difficulty: 'EASY',
    destination: 'MOON // SHACKLETON CRATER',
    description: 'Provide real-time trajectory calculation and hazard-avoidance guidance during the human crew descent to the lunar South Pole.',
    objectives: [
        'Map crater shadow topography via LiDAR',
        'Monitor descent engine throttle telemetry',
        'Confirm safe crew touchdown at LZ-Alpha'
    ],
    rewards: { memory: '512K', credits: 1200, unlocks: ['lunar_south_map'] },
    budget: 1350,
    recommendedLevel: 1
});

missionRegistry.register({
    id: 'm_lunar_gateway',
    title: 'GATEWAY CORE DOCKING PROTOCOL',
    difficulty: 'MEDIUM',
    destination: 'MOON // NEAR-RECTILINEAR HALO ORBIT',
    description: 'Coordinate automated rendezvous and docking procedures for the HALO habitation module at Lunar Gateway.',
    objectives: [
        'Calculate NRHO orbital insertion vector',
        'Align laser docking guidance system',
        'Pressurize airlock seal interface'
    ],
    rewards: { memory: '1024K', credits: 2200, unlocks: ['nrho_vector_data'] },
    budget: 2100,
    recommendedLevel: 3
});

missionRegistry.register({
    id: 'm_lunar_water_ice',
    title: 'PERMANENTLY SHADOWED ICE DRILLING',
    difficulty: 'HARD',
    destination: 'MOON // CABEUS CRATER FLOOR',
    description: 'Deploy autonomous robotic rovers into dark crater traps (-230°C) to harvest water-ice deposits for hydrogen fuel synthesis.',
    objectives: [
        'Guide autonomous rover into cryogenic pit',
        'Regulate drill core bit temperature',
        'Transfer volatilized H2O payload to storage depot'
    ],
    rewards: { memory: '2048K', credits: 4500, unlocks: ['ice_refining_tech'] },
    budget: 3300,
    recommendedLevel: 5
});

// --- 2. MARS EXPLORATION MISSIONS[cite: 3] ---
missionRegistry.register({
    id: 'm_mars_sample_return',
    title: 'MARS SAMPLE RETURN & ASCENT UPLINK',
    difficulty: 'EASY',
    destination: 'MARS // JEZERO CRATER',
    description: 'Establish high-speed optical link with the Mars Ascent Vehicle (MAV) to download sample cache vault telemetry before launch.',
    objectives: [
        'Calibrate Mars Reconnaissance Orbiter relay',
        'Verify sample tube hermetic seals',
        'Execute automated MAV ignition sequence'
    ],
    rewards: { memory: '768K', credits: 1800, unlocks: ['mars_ascent_data'] },
    budget: 1500,
    recommendedLevel: 2
});

missionRegistry.register({
    id: 'm_mars_colony_outpost',
    title: 'ARES STATION HABITAT DEPLOYMENT',
    difficulty: 'MEDIUM',
    destination: 'MARS // OLYMPUS MONS FOOTHILLS',
    description: 'Oversee autonomous printing and pressurization of subterranean regolith habitats for the incoming Ares crew.',
    objectives: [
        'Monitor 3D regolith printer structural integrity',
        'Verify life support scrubbers and O2 recycling',
        'Deploy surface solar power array'
    ],
    rewards: { memory: '1536K', credits: 3200, unlocks: ['ares_habitat_access'] },
    budget: 2400,
    recommendedLevel: 4
});

missionRegistry.register({
    id: 'm_mars_terraforming_probe',
    title: 'MAGNETOSPHERE AUGMENTATION TEST',
    difficulty: 'HARD',
    destination: 'MARS // LAGRANGE POINT L1',
    description: 'Position an artificial magnetic dipole shield at Mars L1 to deflect solar wind and protect atmospheric thickening initiatives.',
    objectives: [
        'Deploy superconducting magnetic ring array',
        'Stabilize orbital station-keeping at L1',
        'Measure atmospheric solar radiation deflection'
    ],
    rewards: { memory: '4096K', credits: 7500, unlocks: ['shield_generator_tech'] },
    budget: 3600,
    recommendedLevel: 7
});

// --- 3. VENUS PROBE & ATMOSPHERIC MISSIONS[cite: 3] ---
missionRegistry.register({
    id: 'm_venus_davinci_probe',
    title: 'DAVINCI+ VENUS ATMOSPHERIC DESCENT',
    difficulty: 'EASY',
    destination: 'VENUS // ALPHA REGIO CLOUD LAYER',
    description: 'Guide a hardened atmospheric probe through sulfuric acid clouds to sample noble gases and surface chemistry.',
    objectives: [
        'Survive hypersonic entry thermals',
        'Deploy high-temperature parachute array',
        'Transmit gas spectrometer readings prior to impact'
    ],
    rewards: { memory: '640K', credits: 1500 },
    budget: 1400,
    recommendedLevel: 2
});

missionRegistry.register({
    id: 'm_venus_cloud_city',
    title: 'HAVOC FLOATING RESEARCH HABITAT',
    difficulty: 'MEDIUM',
    destination: 'VENUS // UPPER CLOUD CANOPY (55KM)',
    description: 'Maintain altitude and envelope pressure for a crewed floating cloud station operating at 1 bar Earth-normal pressure.',
    objectives: [
        'Regulate station buoyancy gas mixture',
        'Purify atmospheric acid vapor collectors',
        'Stabilize solar wing roll in super-rotating winds'
    ],
    rewards: { memory: '1792K', credits: 3800, unlocks: ['cloud_station_blueprints'] },
    budget: 2500,
    recommendedLevel: 4
});

// --- 4. JUPITER & JOVIAN MOON MISSIONS[cite: 3] ---
missionRegistry.register({
    id: 'm_europa_clipper',
    title: 'EUROPA SUBSURFACE OCEAN SCAN',
    difficulty: 'MEDIUM',
    destination: 'JUPITER // EUROPA CRYO-SHELL',
    description: 'Process ice-penetrating radar data during low-altitude flybys to identify thin ice spots for future thermal lander missions.',
    objectives: [
        'Filter out Jovian radiation belt noise',
        'Map subterranean thermal plume vents',
        'Identify sub-crust liquid ocean pockets'
    ],
    rewards: { memory: '2048K', credits: 4200, unlocks: ['radar_depth_scanner'] },
    budget: 2650,
    recommendedLevel: 5
});

missionRegistry.register({
    id: 'm_ganymede_magnetosphere',
    title: 'GANYMEDE MAGNETIC HARMONICS',
    difficulty: 'HARD',
    destination: 'JUPITER // GANYMEDE ORBIT',
    description: 'Map the interaction between Ganymede\'s intrinsic magnetic field and Jupiter\'s massive magnetosphere.',
    objectives: [
        'Deploy tri-axial fluxgate magnetometers',
        'Correlate auroral belt oscillations',
        'Filter high-energy electron plasma feedback'
    ],
    rewards: { memory: '3072K', credits: 6200 },
    budget: 3450,
    recommendedLevel: 6
});

missionRegistry.register({
    id: 'm_io_volcanic_capture',
    title: 'IO VOLCANIC PLUME EXTRACTION',
    difficulty: 'HARD',
    destination: 'JUPITER // IO MAGMA BASIN',
    description: 'Fly an armored sample scoop probe through Loki Patera volcanic eruptions to collect raw sulfur compounds and rare isotopes.',
    objectives: [
        'Calculate extreme radiation shielding limits',
        'Navigate close-approach volcanic ejection arc',
        'Collect and compress heavy element gases'
    ],
    rewards: { memory: '4096K', credits: 8500, unlocks: ['exotic_isotope_catalog'] },
    budget: 3800,
    recommendedLevel: 8
});

// --- 5. ASTEROID BELT & RESOURCE EXPLOITATION MISSIONS[cite: 3] ---
missionRegistry.register({
    id: 'm_psyche_heavy_metal',
    title: '16 PSYCHE METAL CORE EXTRACTION',
    difficulty: 'MEDIUM',
    destination: 'ASTEROID BELT // 16 PSYCHE',
    description: 'Survey a multi-trillion dollar metallic nickel-iron asteroid core and establish automated laser mining rigs.',
    objectives: [
        'Map surface elemental abundance via gamma spectrometry',
        'Position concentrated solar laser melting rig',
        'Stabilize asteroid rotation axis using thruster anchors'
    ],
    rewards: { memory: '2560K', credits: 6000, unlocks: ['asteroid_mining_license'] },
    budget: 2750,
    recommendedLevel: 5
});

missionRegistry.register({
    id: 'm_ceres_water_depot',
    title: 'CERES CRYOPLANETARY REFINERY',
    difficulty: 'HARD',
    destination: 'ASTEROID BELT // CERES OCCATOR CRATER',
    description: 'Construct a deep-space fuel station harvesting sodium carbonate and subsurface brine from Occator crater bright spots.',
    objectives: [
        'Extract high-salinity subterranean brine',
        'Electrolyze H2O into liquid hydrogen/oxygen propellants',
        'Dock deep-space freighter for automated refueling'
    ],
    rewards: { memory: '5120K', credits: 11000, unlocks: ['deep_space_refinery'] },
    budget: 3900,
    recommendedLevel: 7
});

missionRegistry.register({
    id: 'm_trojan_swarm_capture',
    title: 'LUCY TROJAN ASTEROID FLYBY NETWORK',
    difficulty: 'EASY',
    destination: 'JUPITER TROJANS // L4 SWARM',
    description: 'Coordinate multi-target gravitational slingshot paths through the Jupiter Trojan asteroid swarms.',
    objectives: [
        'Calculate multi-asteroid flyby trajectory solutions',
        'Capture surface imagery of primordial planetesimals',
        'Analyze carbonaceous chondrite organic signatures'
    ],
    rewards: { memory: '896K', credits: 1900 },
    budget: 1550,
    recommendedLevel: 2
});

// --- 6. DYSON SWARM & MEGASTRUCTURE MISSIONS[cite: 3] ---
missionRegistry.register({
    id: 'm_dyson_receiver_node',
    title: 'SOLAR DYSON RECEIVER ARRAY ALPHA',
    difficulty: 'HARD',
    destination: 'SUN // INNER CORONA ORBIT (0.05 AU)',
    description: 'Position massive wireless microwave energy receivers to beam gigawatts of solar power to Earth-Moon orbital grids.',
    objectives: [
        'Deploy thermal magnetic heat-shields',
        'Align phased-array microwave emitters',
        'Calibrate rectenna station-keeping guidance'
    ],
    rewards: { memory: '8192K', credits: 18000, unlocks: ['dyson_power_grid'] },
    budget: 4100,
    recommendedLevel: 9
});

missionRegistry.register({
    id: 'm_dyson_swarm_deploy',
    title: 'MEGASTRUCTURE: DYSON SWARM SYNCHRONIZATION',
    difficulty: 'EXTREME',
    destination: 'SOLAR // PHOTOSPHERE BOUNDARY',
    description: 'Deploy and orchestrate millions of automated solar collector mirrors into a self-balancing swarm surrounding the Sun.',
    objectives: [
        'Deploy autonomous solar mirror manufacturing factories',
        'Coordinate swarm orbital mechanics to avoid collisions',
        'Maximize total stellar energy capture ratio (>10%)'
    ],
    rewards: { memory: '32768K', credits: 100000, unlocks: ['kardashev_type2_status'] },
    budget: 4800,
    recommendedLevel: 15
});

// --- 7. SATURN, TITAN & OUTER SYSTEM MISSIONS[cite: 3] ---
missionRegistry.register({
    id: 'm_titan_dragonfly',
    title: 'TITAN DRAGONFLY ROTORCRAFT SURVEY',
    difficulty: 'MEDIUM',
    destination: 'SATURN // TITAN SHANGRI-LA DUNES',
    description: 'Fly an octocopter probe through Titan\'s dense nitrogen atmosphere to search for prebiotic chemical signatures in liquid methane lakes.',
    objectives: [
        'Navigate rotorcraft through cold Titan atmospheric currents',
        'Sample liquid methane / ethane shoreline sediment',
        'Transmit high-resolution landscape panoramas'
    ],
    rewards: { memory: '2048K', credits: 4800, unlocks: ['titan_flight_logs'] },
    budget: 2700,
    recommendedLevel: 5
});

missionRegistry.register({
    id: 'm_enceladus_life_search',
    title: 'ENCELADUS PLUME FLYTHROUGH',
    difficulty: 'HARD',
    destination: 'SATURN // ENCELADUS SOUTH POLE',
    description: 'Fly directly through water-ice geyser plumes erupting from Enceladus\' subterranean ocean to detect organic molecules.',
    objectives: [
        'Calculate low-velocity plume entry corridor',
        'Capture cryogenic ice grains in aerogel collector',
        'Analyze complex amino acid spectra'
    ],
    rewards: { memory: '4096K', credits: 9200, unlocks: ['cryo_biosignature_data'] },
    budget: 3750,
    recommendedLevel: 7
});

missionRegistry.register({
    id: 'm_james_webb_ii',
    title: 'JWST-II QUANTUM DEEP FIELD TELESCOPE',
    difficulty: 'EASY',
    destination: 'SUN-EARTH // LAGRANGE POINT L2',
    description: 'Align a next-generation 12-meter segmented optical telescope to map exoplanetary biosignatures across the galaxy.',
    objectives: [
        'Deploy 5-layer Kapton sunshield membrane',
        'Align primary mirror segment actuators with nanometer precision',
        'Capture atmospheric transmission spectrum of TRAPPIST-1e'
    ],
    rewards: { memory: '1024K', credits: 2100, unlocks: ['deep_space_optics'] },
    budget: 1600,
    recommendedLevel: 2
});

missionRegistry.register({
    id: 'm_interstellar_voyager',
    title: 'VOYAGER 3 INTERSTELLAR PROBE',
    difficulty: 'EXTREME',
    destination: 'HELIOSPHERE // THE DEEP VOID (200 AU)',
    description: 'Pilot a nuclear-fusion powered probe past the heliopause into true interstellar space to measure the galactic magnetic field.',
    objectives: [
        'Execute solar Oberth burn for ultra-high velocity escape',
        'Cross terminal shock boundary into interstellar medium',
        'Maintain deep-space laser comm link across light-days'
    ],
    rewards: { memory: '16384K', credits: 45000, unlocks: ['interstellar_nav_beacon'] },
    budget: 4500,
    recommendedLevel: 12
});

window.missionRegistry = missionRegistry;
