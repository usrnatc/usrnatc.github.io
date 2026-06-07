importScripts('sudsolve.js');

var Module = null;
var SolveSudokuStr = null;

SudokuModule().then(function(mod) {
    Module = mod;

    SolveSudokuStr = Module.cwrap(
        'SolveSudokuStr', 
        'string', 
        ['string']
    );

    postMessage({ type: 'ready' });
});

onmessage = function(e) {
    if (e.data.type === 'solve') {
        if (!SolveSudokuStr) {
            postMessage({ type: 'error', error: 'Module not loaded' });

            return;
        }

        try {
            var result = SolveSudokuStr(e.data.str);

            postMessage({ type: 'result', result: result });
        } catch (err) {
            postMessage({ type: 'error', error: err.message || 'Solve failed' });
        }
    }
};
