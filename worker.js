importScripts('pips.js');

let WasmModule;

PipsModule().then(m => {
    WasmModule = m;

    postMessage({
        type: 'ready'
    });
});

self.onmessage = function(e) {
    if (e.data.type === 'solve') {
        try {
            const result = WasmModule.ccall('SolvePuzzleStr', 'string', ['string'], [e.data.str]);

            postMessage({
                type: 'result',
                result: result
            });
        } catch (err) {
            postMessage({
                type: 'error',
                error: err.message
            });
        }
    }
};
