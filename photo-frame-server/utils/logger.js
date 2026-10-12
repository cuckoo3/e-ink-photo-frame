'use strict';

const fs = require('node:fs');
const util = require('node:util');
const path = require('path');

const MAX_QUEUE = 500;
const MAX_LINE_LENGTH = 4096;
const normalLogPath = process.env.LOG_PATH || path.join(__dirname, '../logs', 'app.log');
const errorLogPath = process.env.ERROR_LOG_PATH || path.join(__dirname, '../logs', 'error.log');

function localTimestamp() {
    const d = new Date();
    const pad = n => String(n).padStart(2, '0');

    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ` +
           `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function createLogger(filename) {
	const dir = path.dirname(filename);
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
    }
	
    const stream = fs.createWriteStream(filename, {
        flags: 'a',
        highWaterMark: 16 * 1024,
    });

    const queue = [];

    let disabled = false;
    let blocked = false;

    stream.on('error', () => {
        disabled = true;
        queue.length = 0;
    });

    function flush() {
        if (disabled || blocked) return;

        while (queue.length > 0) {
            const line = queue.shift();

            try {
                if (!stream.write(line)) {
                    // Wait for drain before writing more.
                    blocked = true;
                    return;
                }
            } catch {
                disabled = true;
                queue.length = 0;
                return;
            }
        }
    }

    stream.on('drain', () => {
        blocked = false;
        flush();
    });

	function write(level, ...args) {
	    if (disabled) return;

	    let line;

	    try {
	        line = `[${localTimestamp()}] [${level}] ${util.format(...args)}\n`;
	        line = line.slice(0, MAX_LINE_LENGTH);
	    } catch {
	        return;
	    }

	    // Drop new messages when the queue is full.
	    if (queue.length >= MAX_QUEUE) return;

	    queue.push(line);
	    flush();
	}
	
    return {
        log: (...args) => write('INFO', ...args),
        info: (...args) => write('INFO', ...args),
        warn: (...args) => write('WARN', ...args),
        error: (...args) => write('ERROR', ...args),
    };
}

const normal = createLogger(normalLogPath);
const errors = createLogger(errorLogPath);

module.exports = {
    log: normal.log,
    info: normal.info,
    warn: errors.warn,
    error: errors.error,
};