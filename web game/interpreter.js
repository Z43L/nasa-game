/* ==========================================
   BRAIN SPACE OVERFLOW - LENGUAJE (.LG) CLIENT-SIDE INTERPRETER
   ========================================== */

class LenguajeInterpreter {
    constructor() {
        this.reset();
    }

    reset() {
        this.variables = new Map();
        this.functions = new Map();
        this.outputBuffer = [];
    }

    log(text) {
        this.outputBuffer.push(text);
    }

    error(msg, lineNum = 1) {
        this.outputBuffer.push(`<span style="color: #ff3333;">error: [LGC Line ${lineNum}] ${msg}</span>`);
    }

    /**
     * Execute a .lg script string and return output lines
     * @param {string} code
     * @returns {string[]} output lines
     */
    execute(code) {
        this.reset();
        const lines = code.split('\n');

        let inFunction = false;
        let currentFnName = null;
        let currentFnBody = [];
        let currentFnArgs = [];

        for (let i = 0; i < lines.length; i++) {
            const rawLine = lines[i];
            const line = rawLine.trim();
            const lineNum = i + 1;

            // Skip comments and empty lines
            if (!line || line.startsWith('#')) continue;

            // Handle function definition: fn name(args) -> type:
            if (line.startsWith('fn ')) {
                const match = line.match(/^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)/);
                if (match) {
                    inFunction = true;
                    currentFnName = match[1];
                    currentFnArgs = match[2].split(',').map(s => s.trim()).filter(Boolean);
                    currentFnBody = [];
                    continue;
                } else {
                    this.error(`Sintaxis de función inválida`, lineNum);
                    continue;
                }
            }

            // If inside function body (indented lines)
            if (inFunction) {
                if (rawLine.startsWith('    ') || rawLine.startsWith('\t')) {
                    currentFnBody.push(line);
                    continue;
                } else {
                    // Function block ended
                    this.functions.set(currentFnName, { args: currentFnArgs, body: currentFnBody });
                    inFunction = false;
                    currentFnName = null;
                }
            }

            // Handle imports
            if (line.startsWith('import ')) {
                const mod = line.replace('import', '').trim();
                this.log(`> [LGC] Importando módulo '${mod}'... OK`);
                continue;
            }

            // Handle print statement: print(...)
            if (line.startsWith('print(') && line.endsWith(')')) {
                const expr = line.substring(6, line.length - 1).trim();
                const evaluated = this.evaluateExpression(expr);
                this.log(String(evaluated));
                continue;
            }

            // Handle variable assignment: var = expr
            if (line.includes('=') && !line.includes('==')) {
                const parts = line.split('=').map(p => p.trim());
                const varName = parts[0].replace(/^(mut\s+)?/, '').split(':')[0].trim(); // strip 'mut ' and type annotation
                const expr = parts.slice(1).join('=');
                const val = this.evaluateExpression(expr);
                this.variables.set(varName, val);
                continue;
            }

            // Handle direct function calls or expressions
            if (line.includes('(') && line.endsWith(')')) {
                this.evaluateExpression(line);
                continue;
            }

            // If unrecognized statement
            this.error(`Instrucción no reconozida o error de sintaxis: "${line}"`, lineNum);
        }

        // If function definition was at the very end
        if (inFunction && currentFnName) {
            this.functions.set(currentFnName, { args: currentFnArgs, body: currentFnBody });
        }

        return this.outputBuffer;
    }

    evaluateExpression(expr) {
        expr = expr.trim();

        // String literal
        if ((expr.startsWith('"') && expr.endsWith('"')) || (expr.startsWith("'") && expr.endsWith("'"))) {
            return expr.substring(1, expr.length - 1);
        }

        // Number literal
        if (!isNaN(expr)) {
            return Number(expr);
        }

        // Boolean literal
        if (expr === 'true') return true;
        if (expr === 'false') return false;

        // Variable lookup
        if (this.variables.has(expr)) {
            return this.variables.get(expr);
        }

        // Function call evaluation: func(args)
        const fnMatch = expr.match(/^([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)$/);
        if (fnMatch) {
            const fnName = fnMatch[1];
            const argsStr = fnMatch[2];
            const args = argsStr ? argsStr.split(',').map(s => this.evaluateExpression(s.trim())) : [];

            // Check built-in mock functions
            if (fnName === 'traductor_obtener') {
                return "Bienvenido a BrainSpaceOverflow (Traducción OK)";
            }
            if (fnName === 'archivo_leer') {
                return "{ 'sector': '07', 'status': 'CORRUPTED' }";
            }

            // Check custom user function
            if (this.functions.has(fnName)) {
                const fn = this.functions.get(fnName);
                // Simple local scope execution simulation
                let lastResult = null;
                for (const bLine of fn.body) {
                    if (bLine.startsWith('return ')) {
                        const retExpr = bLine.substring(7).trim();
                        lastResult = this.evaluateExpression(retExpr);
                        break;
                    }
                    if (bLine.startsWith('print(')) {
                        const pExpr = bLine.substring(6, bLine.length - 1).trim();
                        this.log(String(this.evaluateExpression(pExpr)));
                    }
                }
                return lastResult !== null ? lastResult : "Void";
            }

            return `[Resultado de ${fnName}(${args.join(', ')})]`;
        }

        // Basic arithmetic / concatenation evaluation
        if (expr.includes('+')) {
            const parts = expr.split('+').map(p => this.evaluateExpression(p.trim()));
            if (typeof parts[0] === 'number' && typeof parts[1] === 'number') {
                return parts[0] + parts[1];
            }
            return parts.join('');
        }

        if (expr.includes('>')) {
            const parts = expr.split('>').map(p => this.evaluateExpression(p.trim()));
            return parts[0] > parts[1];
        }

        return expr;
    }
}

// Export global interpreter instance
window.lenguajeInterpreter = new LenguajeInterpreter();
