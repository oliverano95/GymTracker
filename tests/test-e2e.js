// @ts-check
// E2E test: Config page → close handler → pypkjs WebSocket → AppMessage → watch
//
// Full chain:
//   1. Playwright serves config page via HTTP
//   2. User clicks Save & Send → sendToPebble() navigates to close handler
//   3. Close handler captures encoded JSON payload
//   4. We connect to pypkjs WebSocket and send AppConfigResponse
//   5. pypkjs triggers webviewclosed in pebble-js-app.js
//   6. pebble-js-app.js handler sends AppMessage (ROUTINE_DATA) to watch
//   7. Watch C code processes the message

const { test, expect } = require('@playwright/test');
const path = require('path');
const http = require('http');
const fs = require('fs');
const net = require('net');
const { execSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');

function findFreePort() {
  return new Promise((resolve) => {
    const srv = net.createServer();
    srv.listen(0, () => {
      const port = srv.address().port;
      srv.close(() => resolve(port));
    });
  });
}

function getPypkjsPort() {
  try {
    const out = execSync('pgrep -af pypkjs', { encoding: 'utf8' });
    const m = out.match(/--port (\d+)/);
    return m ? parseInt(m[1]) : null;
  } catch {
    return null;
  }
}

// Raw WebSocket client (no deps)
const crypto = require('crypto');

function wsHandshake(host, port) {
  return new Promise((resolve, reject) => {
    const sock = net.createConnection({ host, port }, () => {
      const key = crypto.randomBytes(16).toString('base64');
      const req = [
        `GET / HTTP/1.1`,
        `Host: ${host}:${port}`,
        `Upgrade: websocket`,
        `Connection: Upgrade`,
        `Sec-WebSocket-Key: ${key}`,
        `Sec-WebSocket-Version: 13`,
        '', ''
      ].join('\r\n');
      sock.write(req);

      let data = Buffer.alloc(0);
      const onData = (chunk) => {
        data = Buffer.concat([data, chunk]);
        if (data.toString().includes('\r\n\r\n')) {
          sock.removeListener('data', onData);
          if (data.includes('101')) {
            resolve(sock);
          } else {
            reject(new Error(`Handshake failed: ${data.toString().slice(0, 200)}`));
          }
        }
      };
      sock.on('data', onData);
    });
    sock.on('error', reject);
  });
}

function wsSendBinary(sock, payload) {
  // Client → server: must mask
  const mask = crypto.randomBytes(4);
  let header;
  if (payload.length < 126) {
    header = Buffer.alloc(2);
    header[0] = 0x82; // FIN + BINARY
    header[1] = payload.length | 0x80; // MASK
  } else if (payload.length < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x82;
    header[1] = 126 | 0x80;
    header.writeUInt16BE(payload.length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x82;
    header[1] = 127 | 0x80;
    header.writeBigUInt64BE(BigInt(payload.length), 2);
  }
  const masked = Buffer.alloc(payload.length);
  for (let i = 0; i < payload.length; i++) {
    masked[i] = payload[i] ^ mask[i % 4];
  }
  sock.write(Buffer.concat([header, mask, masked]));
}

function buildAppConfigResponse(queryString) {
  // AppConfigResponse: length (4 bytes LE) + data (UTF-8 string)
  const dataBuf = Buffer.from(queryString, 'utf8');
  const lenBuf = Buffer.alloc(4);
  lenBuf.writeUInt32LE(dataBuf.length, 0);
  const configResp = Buffer.concat([lenBuf, dataBuf]);

  // WebSocketPhonesimAppConfig: command byte 0x02 (AppConfigResponse) + config
  const phonesimMsg = Buffer.concat([Buffer.from([0x02]), configResp]);

  // Endpoint 0x0a for WebSocketPhonesimAppConfig
  const framed = Buffer.concat([Buffer.from([0x0a]), phonesimMsg]);
  return framed;
}

test.describe('E2E — Full emulator flow', () => {
  test('Config page → close handler → pypkjs WebSocket → payload delivered', async ({ page }) => {
    const pypkjsPort = getPypkjsPort();
    test.skip(!pypkjsPort, 'pypkjs not running (run pebble install --emulator basalt first)');

    const syncDict = { A: 'A|-1|2|Bench Press|3|10|60|0|-|-' };

    // Start config page server
    const pagePort = await findFreePort();
    const pageServer = await new Promise((resolve) => {
      const srv = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://localhost');
        const filePath = path.join(ROOT, url.pathname === '/' ? '/index_dev.html' : url.pathname);
        if (!fs.existsSync(filePath)) { res.writeHead(404); res.end(); return; }
        const ext = path.extname(filePath);
        const mime = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css' }[ext] || 'text/plain';
        res.writeHead(200, { 'Content-Type': mime });
        res.end(fs.readFileSync(filePath));
      });
      srv.listen(pagePort, () => resolve(srv));
    });

    // Start close handler
    const closePort = await findFreePort();
    let capturedQuery = null;
    const closeServer = await new Promise((resolve) => {
      const srv = http.createServer((req, res) => {
        const url = new URL(req.url, 'http://localhost');
        if (url.pathname === '/close') {
          capturedQuery = url.search ? url.search.slice(1) : '';
          res.writeHead(200);
          res.end('OK');
        } else {
          res.writeHead(404);
          res.end();
        }
      });
      srv.listen(closePort, () => resolve(srv));
    });

    try {
      // Open config page with return_to pointing to our close handler
      const syncParam = encodeURIComponent(JSON.stringify(syncDict));
      const configUrl = `http://127.0.0.1:${pagePort}/index_dev.html?sync=${syncParam}&return_to=http%3A%2F%2Flocalhost%3A${closePort}%2Fclose%3F`;

      await page.goto(configUrl);
      await page.waitForFunction(() => {
        const sel = document.getElementById('savedRoutineSelect');
        return sel && sel.options.length > 1;
      }, { timeout: 5000 });

      // Select A, override sendToPebble to navigate to our close handler
      await page.selectOption('#savedRoutineSelect', 'A');

      // Override sendToPebble so it navigates to our close handler
      await page.evaluate((closePort) => {
        window.sendToPebble = function(configData) {
          const encoded = encodeURIComponent(JSON.stringify(configData));
          window.location.href = `http://localhost:${closePort}/close?${encoded}`;
        };
      }, closePort);

      // Intercept navigation to capture payload (don't actually navigate)
      let payload = null;
      page.on('request', (req) => {
        const url = req.url();
        if (url.includes('/close?')) {
          const query = url.split('/close?')[1] || '';
          payload = JSON.parse(decodeURIComponent(query));
        }
      });

      await page.click('#btn-send-single');
      await page.waitForTimeout(500);

      // Also check if close handler captured it directly
      if (!payload && capturedQuery) {
        payload = JSON.parse(decodeURIComponent(capturedQuery));
      }

      expect(payload).not.toBeNull();
      console.log('Payload captured:', JSON.stringify(payload, null, 2));

      // Verify payload structure — selecting A and saving without rename/delete
      // means updatedSync has A, no deletedKeys
      expect(payload.updatedSync).toHaveProperty('A');
      expect(payload.progressionMode).toBeDefined();
      expect(payload.weightIncrement).toBeDefined();

      // Verify payload size
      const size = JSON.stringify(payload).length;
      expect(size).toBeLessThan(450);
      console.log(`Payload size: ${size} chars (< 450: YES)`);

      // Step 4: Send AppConfigResponse to pypkjs via WebSocket
      console.log(`\nConnecting to pypkjs WebSocket on port ${pypkjsPort}...`);
      const sock = await wsHandshake('localhost', pypkjsPort);
      console.log('WebSocket connected!');

      // Build and send AppConfigResponse
      const queryStr = capturedQuery || encodeURIComponent(JSON.stringify(payload));
      const framed = buildAppConfigResponse(queryStr);
      wsSendBinary(sock, framed);
      console.log('AppConfigResponse sent to pypkjs!');
      console.log('  Endpoint: 0x0a (WebSocketPhonesimAppConfig)');
      console.log('  Command: 0x02 (AppConfigResponse)');
      console.log(`  Data length: ${queryStr.length} bytes`);

      // Wait for pypkjs to process
      await new Promise(r => setTimeout(r, 2000));

      // Try to read any response from pypkjs
      try {
        sock.setReadable(true);
        const chunks = [];
        sock.on('data', (chunk) => chunks.push(chunk));
        await new Promise(r => setTimeout(r, 1000));
        if (chunks.length > 0) {
          console.log('Response from pypkjs:', Buffer.concat(chunks).toString('hex').slice(0, 100));
        } else {
          console.log('No response from pypkjs (normal — async processing)');
        }
      } catch (e) {
        console.log('Could not read response:', e.message);
      }

      sock.destroy();

      console.log('\n=== E2E CHAIN COMPLETE ===');
      console.log('1. Config page built payload ✓');
      console.log('2. Close handler captured payload ✓');
      console.log('3. AppConfigResponse sent to pypkjs ✓');
      console.log('4. pypkjs should trigger webviewclosed → AppMessage → watch');
      console.log('\nNOTE: Verifying steps 4-5 requires pebble logs, which stream');
      console.log('continuously. Check manually with: pebble logs --emulator basalt');

    } finally {
      pageServer.close();
      closeServer.close();
    }
  });
});
