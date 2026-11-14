/* ==========================================
   BRAIN SPACE OVERFLOW - TERMINAL CONTROLLER[cite: 5]
   ========================================== */

const shipRegistry = [
    {
        id: 'scout_light',
        name: 'Scout-1 Interceptor (Light)',
        price: 400,
        fuelConsumMultiplier: 0.8,
        riskModifier: -5,
        advantage: 'High agility (-5% base risk, -20% fuel cost)',
        disadvantage: 'Light hull (+8% risk on HARD/EXTREME missions)'
    },
    {
        id: 'hauler_heavy',
        name: 'Hauler-X Heavy Freighter',
        price: 900,
        fuelConsumMultiplier: 1.4,
        riskModifier: +4,
        advantage: 'Reinforced hull & large cargo bay (halves supply shortage penalty)',
        disadvantage: 'Heavy mass (+40% fuel cost, +4% base risk)'
    },
    {
        id: 'elite_cruiser',
        name: 'Ares-V Science Cruiser',
        price: 2200,
        fuelConsumMultiplier: 1.0,
        riskModifier: -12,
        advantage: 'Quantum navigation & life support (-12% base risk)',
        disadvantage: 'High deployment cost (leaves tight budget for supplies)'
    }
];

let selectedShip = shipRegistry[0];
let currentFuelPurchased = 100;
let currentSuppliesPurchased = 100;
let currentCalculatedRisk = 15;

document.addEventListener('DOMContentLoaded', () => {
    let currentScreen = 'boot-screen';
    let selectedMenuIndex = 0;
    let isBooting = true;
    let audioEnabled = false;
    let currentThemeIndex = 0;
    const themes = ['green', 'amber', 'cyan', 'white'];
    const themeNames = ['CLASSIC GREEN', 'NEON AMBER', 'CYAN MATRIX', 'PALE WHITE'];

    // DOM Elements[cite: 5]
    const bootScreen = document.getElementById('boot-screen');
    const menuScreen = document.getElementById('menu-screen');
    const settingsScreen = document.getElementById('settings-screen');
    const creditsScreen = document.getElementById('credits-screen');
    const loadScreen = document.getElementById('load-screen');

    const bootLogsContainer = document.getElementById('boot-logs');
    const menuItems = document.querySelectorAll('.menu-item');
    const terminalInput = document.getElementById('terminal-input');

    const themeToggleBtn = document.getElementById('theme-toggle');
    const audioToggleBtn = document.getElementById('audio-toggle');
    const systemClock = document.getElementById('system-clock');

    const cfgColorBtn = document.getElementById('cfg-color');
    const cfgScanlinesBtn = document.getElementById('cfg-scanlines');
    const cfgAudioBtn = document.getElementById('cfg-audio');

    // Web Audio API Sound Synthesizer[cite: 5]
    let audioCtx = null;

    function initAudio() {
        if (!audioCtx) {
            audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        }
    }

    function playBeep(freq = 440, duration = 0.05, type = 'sine') {
        if (!audioEnabled) return;
        try {
            initAudio();
            if (audioCtx.state === 'suspended') {
                audioCtx.resume();
            }
            const osc = audioCtx.createOscillator();
            const gain = audioCtx.createGain();
            osc.type = type;
            osc.frequency.value = freq;
            gain.gain.setValueAtTime(0.05, audioCtx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + duration);
            osc.connect(gain);
            gain.connect(audioCtx.destination);
            osc.start();
            osc.stop(audioCtx.currentTime + duration);
        } catch (e) {
            console.error('Audio error:', e);
        }
    }

    // Clock update[cite: 5]
    function updateClock() {
        const now = new Date();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        systemClock.textContent = `${hours}:${minutes}:${seconds}`;
    }
    setInterval(updateClock, 1000);
    updateClock();

    // Boot Sequence Logs[cite: 5]
    const bootLogs = [
        "BIOS v4.11 - BRAIN SPACE OVERFLOW KERNEL",
        "Initializing core memory banks... [ 640K / 640K OK ]",
        "Loading neural interface drivers... SUCCESS",
        "Mounting file system /dev/neuro0 ... CLEAN",
        "Verifying quantum checksums... OK",
        "Connecting to mainframe sector 7... CONNECTED",
        "Checking cortical stack stability... WARNING: FLUCTUATING",
        "Decrypting security enclave... DONE.",
        "SYSTEM READY. WELCOME, OPERATOR."
    ];

    async function runBootSequence() {
        bootLogsContainer.innerHTML = '';
        for (let i = 0; i < bootLogs.length; i++) {
            if (!isBooting) break;
            const p = document.createElement('p');
            p.textContent = `> ${bootLogs[i]}`;
            bootLogsContainer.appendChild(p);
            playBeep(300 + i * 50, 0.03);
            await new Promise(resolve => setTimeout(resolve, 180));
        }
        finishBoot();
    }

    function finishBoot() {
        if (!isBooting) return;
        isBooting = false;
        bootScreen.classList.remove('active');
        menuScreen.classList.add('active');
        currentScreen = 'menu-screen';
        terminalInput.focus();
        playBeep(880, 0.15);
    }

    window.addEventListener('keydown', (e) => {
        if (isBooting && (e.key === 'Enter' || e.key === ' ' || e.key === 'Escape')) {
            isBooting = false;
            finishBoot();
            e.preventDefault();
        }
    });

    bootScreen.addEventListener('click', () => {
        if (isBooting) {
            isBooting = false;
            finishBoot();
        }
    });

    runBootSequence();

    // Menu Navigation[cite: 5]
    function updateMenuSelection() {
        menuItems.forEach((item, index) => {
            if (index === selectedMenuIndex) {
                item.classList.add('active');
            } else {
                item.classList.remove('active');
            }
        });
        playBeep(520, 0.02);
    }

    function handleMenuAction(action) {
        playBeep(750, 0.08);
        switch (action) {
            case 'new-game':
                switchScreen('mission-screen');
                renderMissions('ALL');
                break;
            case 'load-game':
                switchScreen('load-screen');
                break;
            case 'settings':
                switchScreen('settings-screen');
                break;
            case 'credits':
                switchScreen('credits-screen');
                break;
            case 'exit':
                if (confirm('Disconnect neural link?')) {
                    document.body.innerHTML = '<div style="display:flex;justify-content:center;align-items:center;height:100vh;color:#00ff66;font-family:monospace;font-size:1.5rem;">CONNECTION TERMINATED. SAFE TO DISCONNECT.</div>';
                }
                break;
        }
    }

    function switchScreen(screenId) {
        document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
        document.getElementById(screenId).classList.add('active');
        currentScreen = screenId;

        if (screenId === 'menu-screen') {
            terminalInput.focus();
        }
    }

    window.addEventListener('keydown', (e) => {
        if (isBooting) return;

        if (currentScreen !== 'menu-screen') {
            if (e.key === 'Escape' || e.key === 'Backspace') {
                if (document.activeElement !== terminalInput && document.activeElement.tagName !== 'TEXTAREA') {
                    switchScreen('menu-screen');
                    e.preventDefault();
                }
            }
            return;
        }

        if (document.activeElement === terminalInput) {
            if (e.key === 'Enter') {
                processCommand(terminalInput.value.trim());
                terminalInput.value = '';
                e.preventDefault();
            } else if (e.key === 'Escape') {
                terminalInput.blur();
                e.preventDefault();
            }
            return;
        }

        if (e.key === 'ArrowDown') {
            selectedMenuIndex = (selectedMenuIndex + 1) % menuItems.length;
            updateMenuSelection();
            e.preventDefault();
        } else if (e.key === 'ArrowUp') {
            selectedMenuIndex = (selectedMenuIndex - 1 + menuItems.length) % menuItems.length;
            updateMenuSelection();
            e.preventDefault();
        } else if (e.key === 'Enter') {
            const action = menuItems[selectedMenuIndex].getAttribute('data-action');
            handleMenuAction(action);
            e.preventDefault();
        } else if (['1', '2', '3', '4', '5'].includes(e.key)) {
            const idx = parseInt(e.key) - 1;
            if (idx < menuItems.length) {
                selectedMenuIndex = idx;
                updateMenuSelection();
                const action = menuItems[selectedMenuIndex].getAttribute('data-action');
                handleMenuAction(action);
                e.preventDefault();
            }
        } else if (e.key === '/') {
            terminalInput.focus();
            e.preventDefault();
        }
    });

    menuItems.forEach((item, index) => {
        item.addEventListener('mouseenter', () => {
            selectedMenuIndex = index;
            updateMenuSelection();
        });
        item.addEventListener('click', () => {
            const action = item.getAttribute('data-action');
            handleMenuAction(action);
        });
    });

    function processCommand(cmd) {
        const lowerCmd = cmd.toLowerCase();
        if (!lowerCmd) return;

        playBeep(600, 0.05);

        const parts = lowerCmd.split(' ');
        const mainCmd = parts[0];
        const arg = parts[1];

        switch (mainCmd) {
            case 'help':
                alert('AVAILABLE COMMANDS:\n- start / new : Initialize new game\n- load : Open snapshot restore\n- settings : Open system configuration\n- credits : View neural archives\n- theme [green|amber|cyan|white] : Change color theme\n- audio [on|off] : Toggle synthesizer audio\n- clear : Clear terminal buffer\n- hack : Run diagnostics easter egg');
                break;
            case 'start':
            case 'new':
            case 'missions':
            case 'mission':
                handleMenuAction('new-game');
                break;
            case 'load':
                handleMenuAction('load-game');
                break;
            case 'settings':
                handleMenuAction('settings');
                break;
            case 'credits':
            case 'about':
                handleMenuAction('credits');
                break;
            case 'theme':
                if (['green', 'amber', 'cyan', 'white'].includes(arg)) {
                    currentThemeIndex = themes.indexOf(arg);
                    applyTheme(currentThemeIndex);
                } else {
                    currentThemeIndex = (currentThemeIndex + 1) % themes.length;
                    applyTheme(currentThemeIndex);
                }
                break;
            case 'audio':
                toggleAudio();
                break;
            case 'clear':
                terminalInput.value = '';
                break;
            case 'hack':
                alert('ACCESS GRANTED: Root privileges acquired. BSO neural core operating at 100% capacity.');
                break;
            case 'exit':
            case 'quit':
                handleMenuAction('exit');
                break;
            case 'code':
            case 'lg':
                switchScreen('lg-console-screen');
                break;
            default:
                alert(`Command not recognized: "${cmd}". Type "help" for valid system commands.`);
                break;
        }
    }

    function applyTheme(index) {
        currentThemeIndex = index;
        const theme = themes[currentThemeIndex];
        if (theme === 'green') {
            document.documentElement.removeAttribute('data-theme');
        } else {
            document.documentElement.setAttribute('data-theme', theme);
        }
        themeToggleBtn.textContent = `[COLOR: ${themeNames[currentThemeIndex]}]`;
        cfgColorBtn.textContent = themeNames[currentThemeIndex];
        playBeep(900, 0.08);
    }

    themeToggleBtn.addEventListener('click', () => {
        currentThemeIndex = (currentThemeIndex + 1) % themes.length;
        applyTheme(currentThemeIndex);
        terminalInput.focus();
    });

    cfgColorBtn.addEventListener('click', () => {
        currentThemeIndex = (currentThemeIndex + 1) % themes.length;
        applyTheme(currentThemeIndex);
    });

    function toggleAudio() {
        audioEnabled = !audioEnabled;
        const statusText = audioEnabled ? '[AUDIO: ON]' : '[AUDIO: OFF]';
        audioToggleBtn.textContent = statusText;
        cfgAudioBtn.textContent = audioEnabled ? 'ENABLED (SYNTH)' : 'MUTED';
        if (audioEnabled) {
            playBeep(440, 0.1);
        }
    }

    audioToggleBtn.addEventListener('click', () => {
        toggleAudio();
        terminalInput.focus();
    });

    cfgAudioBtn.addEventListener('click', () => {
        toggleAudio();
    });

    let scanlinesEnabled = true;
    cfgScanlinesBtn.addEventListener('click', () => {
        scanlinesEnabled = !scanlinesEnabled;
        const scanlineEl = document.querySelector('.scanline');
        scanlineEl.style.display = scanlinesEnabled ? 'block' : 'none';
        cfgScanlinesBtn.textContent = scanlinesEnabled ? 'ENABLED' : 'DISABLED';
        playBeep(650, 0.05);
    });

    document.querySelectorAll('.back-hint').forEach(hint => {
        hint.addEventListener('click', () => {
            switchScreen('menu-screen');
        });
    });

    // ==========================================
    // MISSION SELECTION & OUTFITTING UI LOGIC
    // ==========================================
    let currentSelectedMission = null;
    const missionListContainer = document.getElementById('mission-list-container');
    const briefingTitle = document.getElementById('briefing-title');
    const briefingDestination = document.getElementById('briefing-destination');
    const briefingDesc = document.getElementById('briefing-desc');
    const briefingObjectivesList = document.getElementById('briefing-objectives-list');
    const briefingRewards = document.getElementById('briefing-rewards');
    const launchMissionBtn = document.getElementById('launch-mission-btn');
    const missionFilters = document.getElementById('mission-filters');

    const shipSelect = document.getElementById('ship-select');
    const shipInfoBox = document.getElementById('ship-info-box');
    const missionBudgetVal = document.getElementById('mission-budget-val');
    const missionRemainingVal = document.getElementById('mission-remaining-val');
    const fuelSlider = document.getElementById('fuel-slider');
    const fuelVal = document.getElementById('fuel-val');
    const fuelCost = document.getElementById('fuel-cost');
    const fuelReqLabel = document.getElementById('fuel-req-label');
    const suppliesSlider = document.getElementById('supplies-slider');
    const suppliesVal = document.getElementById('supplies-val');
    const suppliesCost = document.getElementById('supplies-cost');
    const suppliesReqLabel = document.getElementById('supplies-req-label');
    const riskPercentageVal = document.getElementById('risk-percentage-val');
    const riskWarningText = document.getElementById('risk-warning-text');

    function initShipSelector() {
        if (!shipSelect) return;
        shipSelect.innerHTML = '';
        shipRegistry.forEach(ship => {
            const opt = document.createElement('option');
            opt.value = ship.id;
            opt.textContent = `${ship.name} [${ship.price} CR]`;
            shipSelect.appendChild(opt);
        });
        shipSelect.value = selectedShip.id;
        updateShipInfo(selectedShip);
    }

    function updateShipInfo(ship) {
        selectedShip = ship;
        if (shipInfoBox) {
            shipInfoBox.innerHTML = `
                <div style="color: #00ff66;">+ PRO: ${ship.advantage}</div>
                <div style="color: #ff4444;">- CON: ${ship.disadvantage}</div>
            `;
        }
        calculateMissionRiskAndCost();
    }

    if (shipSelect) {
        shipSelect.addEventListener('change', (e) => {
            const ship = shipRegistry.find(s => s.id === e.target.value);
            if (ship) {
                playBeep(550, 0.03);
                updateShipInfo(ship);
            }
        });
    }

    function getRecommendedLogistics(difficulty) {
        switch (difficulty) {
            case 'EASY':
                return { reqFuel: 60, reqSupplies: 60, baseRisk: 12 };
            case 'MEDIUM':
                return { reqFuel: 110, reqSupplies: 110, baseRisk: 26 };
            case 'HARD':
                return { reqFuel: 160, reqSupplies: 160, baseRisk: 44 };
            case 'EXTREME':
                return { reqFuel: 210, reqSupplies: 210, baseRisk: 62 };
            default:
                return { reqFuel: 100, reqSupplies: 100, baseRisk: 20 };
        }
    }

    function calculateMissionRiskAndCost() {
        if (!currentSelectedMission) return;

        const { reqFuel, reqSupplies, baseRisk } = getRecommendedLogistics(currentSelectedMission.difficulty);

        if (fuelReqLabel) fuelReqLabel.textContent = reqFuel;
        if (suppliesReqLabel) suppliesReqLabel.textContent = reqSupplies;

        const fQty = fuelSlider ? parseInt(fuelSlider.value, 10) : reqFuel;
        const sQty = suppliesSlider ? parseInt(suppliesSlider.value, 10) : reqSupplies;

        currentFuelPurchased = fQty;
        currentSuppliesPurchased = sQty;

        const calculatedFuelCost = Math.round(fQty * 2 * selectedShip.fuelConsumMultiplier);
        const calculatedSuppliesCost = Math.round(sQty * 3);
        const totalShipCost = selectedShip.price;

        if (fuelVal) fuelVal.textContent = fQty;
        if (fuelCost) fuelCost.textContent = calculatedFuelCost;
        if (suppliesVal) suppliesVal.textContent = sQty;
        if (suppliesCost) suppliesCost.textContent = calculatedSuppliesCost;

        const totalCost = totalShipCost + calculatedFuelCost + calculatedSuppliesCost;
        const assignedBudget = currentSelectedMission.budget || (currentSelectedMission.rewards.credits + 800);
        const remainingBudget = assignedBudget - totalCost;

        if (missionBudgetVal) missionBudgetVal.textContent = assignedBudget;
        if (missionRemainingVal) missionRemainingVal.textContent = remainingBudget;

        // Calculate dynamic risk percentage
        let risk = baseRisk + selectedShip.riskModifier;

        // Light hull penalty on high-tier missions
        if (selectedShip.id === 'scout_light' && (currentSelectedMission.difficulty === 'HARD' || currentSelectedMission.difficulty === 'EXTREME')) {
            risk += 8;
        }

        // Fuel shortage penalty
        if (fQty < reqFuel) {
            const deficit = reqFuel - fQty;
            risk += deficit * 0.45;
        } else if (fQty > reqFuel * 1.25) {
            risk -= 3;
        }

        // Supplies shortage penalty (halved on Heavy Freighter)
        if (sQty < reqSupplies) {
            const deficit = reqSupplies - sQty;
            const supplyMultiplier = selectedShip.id === 'hauler_heavy' ? 0.3 : 0.6;
            risk += deficit * supplyMultiplier;
        } else if (sQty > reqSupplies * 1.25) {
            risk -= 3;
        }

        risk = Math.max(1, Math.min(99, Math.round(risk * 10) / 10));
        currentCalculatedRisk = risk;

        if (riskPercentageVal) {
            riskPercentageVal.textContent = `${risk.toFixed(1)}%`;
            if (risk < 30) {
                riskPercentageVal.style.color = '#00ff66';
            } else if (risk < 65) {
                riskPercentageVal.style.color = '#ffb000';
            } else {
                riskPercentageVal.style.color = '#ff3333';
            }
        }

        if (riskWarningText) {
            if (risk < 30) {
                riskWarningText.textContent = '> Optimal parameters. High mission survival probability.';
                riskWarningText.style.color = '#00ff66';
            } else if (risk < 65) {
                riskWarningText.textContent = '> WARNING: Rationed resources increase failure probability.';
                riskWarningText.style.color = '#ffb000';
            } else {
                riskWarningText.textContent = '> CRITICAL DANGER: Severe fuel/supply deficit. Catastrophic risk.';
                riskWarningText.style.color = '#ff3333';
            }
        }

        if (launchMissionBtn) {
            if (remainingBudget < 0) {
                if (missionRemainingVal) missionRemainingVal.style.color = '#ff3333';
                launchMissionBtn.setAttribute('disabled', 'true');
                launchMissionBtn.textContent = '[ BUDGET EXCEEDED - REDUCE COSTS ]';
            } else {
                if (missionRemainingVal) missionRemainingVal.style.color = '#00ff66';
                launchMissionBtn.removeAttribute('disabled');
                launchMissionBtn.textContent = `[ INITIATE MISSION (${risk.toFixed(1)}% RISK) ]`;
            }
        }
    }

    if (fuelSlider) fuelSlider.addEventListener('input', calculateMissionRiskAndCost);
    if (suppliesSlider) suppliesSlider.addEventListener('input', calculateMissionRiskAndCost);

    function renderMissions(filter = 'ALL') {
        if (!window.missionRegistry || !missionListContainer) return;
        const missions = window.missionRegistry.getByDifficulty(filter);
        missionListContainer.innerHTML = '';

        if (missions.length === 0) {
            missionListContainer.innerHTML = '<div style="padding: 10px; opacity: 0.6;">NO MISSIONS FOUND IN THIS SECTOR.</div>';
            return;
        }

        let missionToSelect = null;

        missions.forEach((mission, index) => {
            const card = document.createElement('div');
            const isSelected = currentSelectedMission && currentSelectedMission.id === mission.id;
            card.className = `mission-card ${isSelected ? 'active' : ''}`;

            const diffClass = `difficulty-${mission.difficulty.toLowerCase()}`;
            card.innerHTML = `
                <div>
                    <div class="mission-card-title">${mission.title}</div>
                    <div style="font-size: 0.78rem; opacity: 0.75;">BUDGET: ${mission.budget} CR</div>
                </div>
                <span class="difficulty-badge ${diffClass}">[${mission.difficulty}]</span>
            `;

            card.addEventListener('click', () => {
                selectMission(mission);
                document.querySelectorAll('.mission-card').forEach(c => c.classList.remove('active'));
                card.classList.add('active');
            });

            missionListContainer.appendChild(card);

            if (isSelected) {
                missionToSelect = mission;
            } else if (index === 0 && !missionToSelect) {
                missionToSelect = mission;
                card.classList.add('active');
            }
        });

        if (missionToSelect) {
            selectMission(missionToSelect);
        }
    }

    function selectMission(mission) {
        currentSelectedMission = mission;
        playBeep(480, 0.03);

        if (briefingTitle) briefingTitle.textContent = mission.title;
        if (briefingDestination) briefingDestination.textContent = `DESTINATION: ${mission.destination}`;
        if (briefingDesc) briefingDesc.textContent = mission.description;

        if (briefingObjectivesList) {
            briefingObjectivesList.innerHTML = '';
            mission.objectives.forEach(obj => {
                const li = document.createElement('li');
                li.textContent = obj;
                briefingObjectivesList.appendChild(li);
            });
        }

        if (briefingRewards) {
            briefingRewards.innerHTML = `<strong>REWARD:</strong> MEMORY ${mission.rewards.memory} | <strong>CREDITS:</strong> ${mission.rewards.credits} CR`;
        }

        // Set sliders to recommended defaults for the newly selected mission
        const { reqFuel, reqSupplies } = getRecommendedLogistics(mission.difficulty);
        if (fuelSlider) fuelSlider.value = reqFuel;
        if (suppliesSlider) suppliesSlider.value = reqSupplies;

        initShipSelector();
        calculateMissionRiskAndCost();
    }

    if (missionFilters) {
        missionFilters.addEventListener('click', (e) => {
            if (e.target.classList.contains('filter-btn')) {
                document.querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));
                e.target.classList.add('active');
                const filter = e.target.getAttribute('data-filter');
                renderMissions(filter);
                playBeep(600, 0.04);
            }
        });
    }

    if (launchMissionBtn) {
        launchMissionBtn.addEventListener('click', () => {
            if (!currentSelectedMission) return;
            playBeep(880, 0.1);
            alert(
                `[UPLINK ESTABLISHED] Launching Mission: ${currentSelectedMission.title}\n` +
                `Destination: ${currentSelectedMission.destination}\n` +
                `Vessel: ${selectedShip.name}\n` +
                `Fuel: ${currentFuelPurchased}t | Supplies: ${currentSuppliesPurchased}%\n` +
                `Mission Risk: ${currentCalculatedRisk.toFixed(1)}%\n\n` +
                `(Core Gameplay Initializing...)`
            );
        });
    }

    // Initialize ship dropdown immediately on load as well
    initShipSelector();

    // Lenguaje Code Console Logic[cite: 5]
    const lgCodeEditor = document.getElementById('lg-code-editor');
    const lgOutputTerminal = document.getElementById('lg-output-terminal');
    const lgRunBtn = document.getElementById('lg-run-btn');
    const lgTemplateSelect = document.getElementById('lg-template-select');

    if (lgRunBtn) {
        lgRunBtn.addEventListener('click', () => {
            playBeep(880, 0.1);
            const code = lgCodeEditor.value;
            lgOutputTerminal.innerHTML = `<p>> Executing script...</p>`;

            if (window.lenguajeInterpreter) {
                const results = window.lenguajeInterpreter.execute(code);

                setTimeout(() => {
                    lgOutputTerminal.innerHTML = `<p>> [LGC] Compiling...</p>`;
                    lgOutputTerminal.innerHTML += `<p>> [LGC] Running: stdout...</p>`;

                    results.forEach(line => {
                        lgOutputTerminal.innerHTML += `<p style="color: white;">${line}</p>`;
                    });

                    lgOutputTerminal.innerHTML += `<p>> [OK] Script executed in 14ms.</p>`;
                    lgOutputTerminal.scrollTop = lgOutputTerminal.scrollHeight;
                }, 400);
            }
        });
    }

    if (lgTemplateSelect) {
        lgTemplateSelect.addEventListener('change', (e) => {
            const val = e.target.value;
            if (val === 'hello') lgCodeEditor.value = "print('Hello, BrainSpaceOverflow')";
            else if (val === 'traductor') lgCodeEditor.value = "import traductor\nprint(traductor_obtener('en.json', 'welcome'))";
            playBeep(400, 0.05);
        });
    }
});
