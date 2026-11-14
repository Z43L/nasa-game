/* ==========================================
   BRAIN SPACE OVERFLOW - TERMINAL CONTROLLER
   ========================================== */

document.addEventListener('DOMContentLoaded', () => {
    // State management
    let currentScreen = 'boot-screen';
    let selectedMenuIndex = 0;
    let isBooting = true;
    let audioEnabled = false;
    let currentThemeIndex = 0;
    const themes = ['green', 'amber', 'cyan', 'white'];
    const themeNames = ['CLASSIC GREEN', 'NEON AMBER', 'CYAN MATRIX', 'PALE WHITE'];

    // DOM Elements
    const bootScreen = document.getElementById('boot-screen');
    const menuScreen = document.getElementById('menu-screen');
    const settingsScreen = document.getElementById('settings-screen');
    const creditsScreen = document.getElementById('credits-screen');
    const loadScreen = document.getElementById('load-screen');

    const bootLogsContainer = document.getElementById('boot-logs');
    const bootSkipPrompt = document.getElementById('boot-skip-prompt');
    const menuItems = document.querySelectorAll('.menu-item');
    const terminalInput = document.getElementById('terminal-input');

    const themeToggleBtn = document.getElementById('theme-toggle');
    const audioToggleBtn = document.getElementById('audio-toggle');
    const systemClock = document.getElementById('system-clock');

    // Settings config buttons
    const cfgColorBtn = document.getElementById('cfg-color');
    const cfgScanlinesBtn = document.getElementById('cfg-scanlines');
    const cfgAudioBtn = document.getElementById('cfg-audio');
    const cfgSpeedBtn = document.getElementById('cfg-speed');

    // Web Audio API Sound Synthesizer
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

    // Clock update
    function updateClock() {
        const now = new Date();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        systemClock.textContent = `${hours}:${minutes}:${seconds}`;
    }
    setInterval(updateClock, 1000);
    updateClock();

    // Boot Sequence Logs
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

    // Handle Boot Bypass
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

    // Start Boot on load
    runBootSequence();

    // Menu Navigation
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
        switch(action) {
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

    // Screen Switching
    function switchScreen(screenId) {
        document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
        document.getElementById(screenId).classList.add('active');
        currentScreen = screenId;

        if (screenId === 'menu-screen') {
            terminalInput.focus();
        }
    }

    // Global Keyboard Navigation
    window.addEventListener('keydown', (e) => {
        if (isBooting) return;

        // If inside a sub-screen, ESC or Backspace returns to menu
        if (currentScreen !== 'menu-screen') {
            if (e.key === 'Escape' || e.key === 'Backspace') {
                if (document.activeElement !== terminalInput) {
                    switchScreen('menu-screen');
                    e.preventDefault();
                }
            }
            return;
        }

        // If typing in terminal input, handle Enter and Escape
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

        // Menu navigation via arrow keys
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

    // Mouse interactions for menu items
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

    // Terminal Command Processor
    function processCommand(cmd) {
        const lowerCmd = cmd.toLowerCase();
        if (!lowerCmd) return;

        playBeep(600, 0.05);
        console.log(`Command executed: ${lowerCmd}`);

        const parts = lowerCmd.split(' ');
        const mainCmd = parts[0];
        const arg = parts[1];

        switch(mainCmd) {
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
                    // Cycle theme
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

    // Theme Switcher
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

    // Audio Toggle
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

    // Scanlines toggle
    let scanlinesEnabled = true;
    cfgScanlinesBtn.addEventListener('click', () => {
        scanlinesEnabled = !scanlinesEnabled;
        const scanlineEl = document.querySelector('.scanline');
        scanlineEl.style.display = scanlinesEnabled ? 'block' : 'none';
        cfgScanlinesBtn.textContent = scanlinesEnabled ? 'ENABLED' : 'DISABLED';
        playBeep(650, 0.05);
    });

    // Back hints click
    document.querySelectorAll('.back-hint').forEach(hint => {
        hint.addEventListener('click', () => {
            switchScreen('menu-screen');
        });
    });

    // Mission Selection UI Logic
    let currentSelectedMission = null;
    const missionListContainer = document.getElementById('mission-list-container');
    const briefingTitle = document.getElementById('briefing-title');
    const briefingDestination = document.getElementById('briefing-destination');
    const briefingDesc = document.getElementById('briefing-desc');
    const briefingObjectivesList = document.getElementById('briefing-objectives-list');
    const briefingRewards = document.getElementById('briefing-rewards');
    const launchMissionBtn = document.getElementById('launch-mission-btn');
    const missionFilters = document.getElementById('mission-filters');

    function renderMissions(filter = 'ALL') {
        if (!window.missionRegistry) return;
        const missions = window.missionRegistry.getByDifficulty(filter);
        missionListContainer.innerHTML = '';

        if (missions.length === 0) {
            missionListContainer.innerHTML = '<div style="padding: 10px; opacity: 0.6;">NO MISSIONS FOUND IN THIS SECTOR.</div>';
            return;
        }

        missions.forEach((mission, index) => {
            const card = document.createElement('div');
            card.className = `mission-card ${currentSelectedMission && currentSelectedMission.id === mission.id ? 'active' : ''}`;

            const diffClass = `difficulty-${mission.difficulty.toLowerCase()}`;
            card.innerHTML = `
                <span class="mission-card-title">${mission.title}</span>
                <span class="difficulty-badge ${diffClass}">[${mission.difficulty}]</span>
            `;

            card.addEventListener('click', () => {
                selectMission(mission);
                // Update active state in cards
                document.querySelectorAll('.mission-card').forEach(c => c.classList.remove('active'));
                card.classList.add('active');
            });

            missionListContainer.appendChild(card);

            // Select first by default if none selected
            if (index === 0 && !currentSelectedMission) {
                selectMission(mission);
                card.classList.add('active');
            }
        });
    }

    function selectMission(mission) {
        currentSelectedMission = mission;
        playBeep(480, 0.03);

        briefingTitle.textContent = mission.title;
        briefingDestination.textContent = `DESTINATION: ${mission.destination}`;
        briefingDesc.textContent = mission.description;

        briefingObjectivesList.innerHTML = '';
        mission.objectives.forEach(obj => {
            const li = document.createElement('li');
            li.textContent = obj;
            briefingObjectivesList.appendChild(li);
        });

        briefingRewards.innerHTML = `<strong>REWARD:</strong> MEMORY ${mission.rewards.memory} | <strong>CREDITS:</strong> ${mission.rewards.credits} CR`;
        launchMissionBtn.removeAttribute('disabled');
        launchMissionBtn.textContent = `[ INITIATE MISSION: ${mission.title} ]`;
    }

    // Filter button listeners
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

    // Launch Mission Action
    if (launchMissionBtn) {
        launchMissionBtn.addEventListener('click', () => {
            if (!currentSelectedMission) return;
            playBeep(880, 0.1);
            alert(`[UPLINK ESTABLISHED] Launching mission: ${currentSelectedMission.title}\nDestination: ${currentSelectedMission.destination}\n\n(Game Level / Core Gameplay Initializing...)`);
        });
    }

    // Lenguaje Code Console Logic
    const lgCodeEditor = document.getElementById('lg-code-editor');
    const lgOutputTerminal = document.getElementById('lg-output-terminal');
    const lgRunBtn = document.getElementById('lg-run-btn');
    const lgTemplateSelect = document.getElementById('lg-template-select');

    if (lgRunBtn) {
        lgRunBtn.addEventListener('click', () => {
            playBeep(880, 0.1);
            const code = lgCodeEditor.value;
            lgOutputTerminal.innerHTML = `<p>> Executing script...</p>`;

            // Execute using our dynamic interpreter
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
            if (val === 'hello') lgCodeEditor.value = "print('Hola, BrainSpaceOverflow')";
            else if (val === 'traductor') lgCodeEditor.value = "import traductor\nprint(traductor_obtener('en.json', 'welcome'))";
            playBeep(400, 0.05);
        });
    }
});
