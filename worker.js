importScripts('pips.js');
importScripts('sudsolve.js');

let PipsWasmModule = null;
let SudokuModuleRef = null;
let SolveSudokuStr = null;

PipsModule().then(m => {
    PipsWasmModule = m;
    postMessage({ type: 'ready' });
});

SudokuModule().then(function(mod) {
    SudokuModuleRef = mod;
    SolveSudokuStr = SudokuModuleRef.cwrap(
        'SolveSudokuStr', 
        'string', 
        ['string']
    );
    postMessage({ type: 'ready' });
});

self.onmessage = function(e) {
    if (e.data.type === 'solve_pips') {
        if (!PipsWasmModule) {
            postMessage({ type: 'error', error: 'Pips module not loaded' });
            return;
        }
        try {
            const result = PipsWasmModule.ccall('SolvePuzzleStr', 'string', ['string'], [e.data.str]);
            postMessage({ type: 'result', result: result });
        } catch (err) {
            postMessage({ type: 'error', error: err.message });
        }
    }
    
    if (e.data.type === 'solve_sudoku') {
        if (!SolveSudokuStr) {
            postMessage({ type: 'error', error: 'Sudoku module not loaded' });
            return;
        }
        try {
            const result = SolveSudokuStr(e.data.str);
            postMessage({ type: 'result', result: result });
        } catch (err) {
            postMessage({ type: 'error', error: err.message || 'Solve failed' });
        }
    }
};
