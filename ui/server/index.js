import express from 'express';
import multer from 'multer';
import { v4 as uuidv4 } from 'uuid';
import cors from 'cors';
import { spawn } from 'child_process';
import path from 'path';
import fs from 'fs/promises';
import { constants } from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3001;

// Middlewares
app.use(cors());
app.use(express.json());

// Set up temporary storage for uploads
const tempDir = path.join(__dirname, 'tmp');
const upload = multer({ dest: tempDir });

// Ensure temp directory exists
await fs.mkdir(tempDir, { recursive: true });

// Resolve the binary path
const COMPRESSOR_BIN = path.resolve(process.env.COMPRESSOR_BIN || path.join(__dirname, '../../compressor'));

/*
 * DESIGN NOTE: Why child_process.spawn instead of exec?
 * ─────────────────────────────────────────────────────────────────
 * 1. Buffer Limits: `exec` buffers the entire stdout/stderr in memory
 *    before resolving the callback. By default, this buffer is limited
 *    (e.g. 1MB). If the C++ binary outputs a lot of logs (or if we
 *    were to stream binary data via stdout), `exec` would crash with
 *    a "maxBuffer exceeded" error.
 * 2. Streaming: `spawn` provides Node.js Streams for stdout/stderr.
 *    We can process the output as it arrives, which is more scalable.
 *    In our case, we accumulate stderr dynamically.
 * 3. Security: `spawn` does not invoke a shell by default, meaning
 *    arguments are passed directly to the executable without shell
 *    expansion, preventing shell injection vulnerabilities.
 * ─────────────────────────────────────────────────────────────────
 */

app.get('/health', async (req, res) => {
    try {
        await fs.access(COMPRESSOR_BIN);
        res.json({ status: 'ok', binaryPath: COMPRESSOR_BIN });
    } catch (err) {
        res.status(500).json({ error: 'Binary missing', binaryPath: COMPRESSOR_BIN });
    }
});

app.post('/compress', upload.single('file'), async (req, res) => {
    if (!req.file) {
        return res.status(400).json({ error: 'No file uploaded' });
    }

    const inputPath = req.file.path;
    const outputPath = path.join(tempDir, `${uuidv4()}_out.bin`);

    const cleanup = async () => {
        try {
            await fs.unlink(inputPath).catch(() => {});
            await fs.unlink(outputPath).catch(() => {});
        } catch (e) {}
    };

    try {
        const algo = req.body.algo || 'auto';
        const threads = req.body.threads || '1';
        const isDecompress = req.body.decompress === 'true';

        // Prepare arguments for the C++ binary
        const args = [
            '--input', inputPath,
            '--output', outputPath,
            '--algo', algo,
            '--threads', threads,
            '--verbose' // Always use verbose to capture stats in stderr/stdout
        ];

        if (isDecompress) {
            args.push('--decompress');
        }

        // Spawn the child process
        // If running on Windows, execute via WSL because the binary is a Linux ELF.
        let child;
        if (process.platform === 'win32') {
            // Need to convert Windows path to WSL path or just rely on WSL interop
            // WSL interop can run `wsl ./compressor` if cwd is correct, or just use wslpath
            // Alternatively, since COMPRESSOR_BIN might have spaces, we can use bash -c
            const wslPath = COMPRESSOR_BIN.replace(/\\/g, '/').replace(/^([A-Za-z]):/, (m, p1) => `/mnt/${p1.toLowerCase()}`);
            const wslInput = inputPath.replace(/\\/g, '/').replace(/^([A-Za-z]):/, (m, p1) => `/mnt/${p1.toLowerCase()}`);
            const wslOutput = outputPath.replace(/\\/g, '/').replace(/^([A-Za-z]):/, (m, p1) => `/mnt/${p1.toLowerCase()}`);
            
            const wslArgs = [
                '--input', wslInput,
                '--output', wslOutput,
                '--algo', algo,
                '--threads', threads,
                '--verbose'
            ];
            if (isDecompress) wslArgs.push('--decompress');

            child = spawn('wsl', [wslPath, ...wslArgs]);
        } else {
            child = spawn(COMPRESSOR_BIN, args);
        }

        let stderrData = '';

        child.stderr.on('data', (data) => {
            stderrData += data.toString();
        });
        
        let stdoutData = '';
        child.stdout.on('data', (data) => {
            stdoutData += data.toString();
        });

        const exitCode = await new Promise((resolve) => {
            child.on('close', resolve);
        });

        if (exitCode === 0) {
            // Success: Stream the output file to the client
            const originalName = req.file.originalname;
            let downloadName = originalName;
            if (isDecompress) {
                // Remove .cmp extension if present
                if (downloadName.endsWith('.cmp')) {
                    downloadName = downloadName.slice(0, -4);
                } else {
                    downloadName = 'decompressed_' + downloadName;
                }
            } else {
                downloadName += '.cmp';
            }

            res.download(outputPath, downloadName, (err) => {
                if (err) {
                    console.error("Error sending file:", err);
                    if (!res.headersSent) {
                        res.status(500).json({ error: 'Error sending file' });
                    }
                }
                cleanup();
            });
        } else {
            // Failure: Return stderr output as JSON
            console.error("Process failed:", stderrData);
            res.status(500).json({ error: 'Compression/Decompression failed', details: stderrData || stdoutData });
            cleanup();
        }
    } catch (err) {
        console.error("Internal Server Error:", err);
        res.status(500).json({ error: 'Internal server error', details: err.message });
        cleanup();
    }
});

app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
    console.log(`Binary path resolved to: ${COMPRESSOR_BIN}`);
});
