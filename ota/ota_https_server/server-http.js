// server-http.js — HTTP (khong TLS) OTA firmware server, chi de dev/test khi
// bat CONFIG_OTA_MANAGER_ALLOW_HTTP + CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP (xem
// muc "Chuyen doi giua HTTP va HTTPS" trong docs/OTA_PLAN.md).
//
// Dung chung thu muc firmware/ voi server.js (HTTPS) - khong can copy ota.bin
// 2 lan. Khong doc gi tu certs/ vi HTTP khong bat tay TLS.
//
// Chay:  node server-http.js

const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = 8071;   // Khac port voi server.js (8070) de chay ca 2 song song neu can so sanh
const FIRMWARE_DIR = path.join(__dirname, 'firmware');

const server = http.createServer((req, res) => {
  const filePath = path.join(FIRMWARE_DIR, path.basename(req.url));

  fs.stat(filePath, (err, stats) => {
    const status = !err && stats.isFile() ? 200 : 404;
    console.log(`${new Date().toISOString()} ${req.socket.remoteAddress} ${req.method} ${req.url} -> ${status}`);

    if (status === 404) {
      res.writeHead(404);
      res.end('Not found');
      return;
    }

    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': stats.size,
    });

    const readStream = fs.createReadStream(filePath);
    const totalBytes = stats.size;
    let sentBytes = 0;
    let lastPercent = -1;
    const BAR_WIDTH = 40;

    readStream.on('data', (chunk) => {
      sentBytes += chunk.length;
      const percent = Math.min(100, Math.floor((sentBytes / totalBytes) * 100));
      if (percent === lastPercent) {
        return;
      }
      lastPercent = percent;

      const filled = Math.round((BAR_WIDTH * percent) / 100);
      const bar = '='.repeat(filled) + ' '.repeat(BAR_WIDTH - filled);
      process.stdout.write(`\r  [${bar}] ${percent}% (${sentBytes}/${totalBytes} bytes)`);
    });

    readStream.on('end', () => {
      process.stdout.write('\n');
      console.log(`${new Date().toISOString()} Gui thanh cong toan bo file "${path.basename(filePath)}" (${totalBytes} bytes) cho ${req.socket.remoteAddress}`);
    });

    readStream.on('error', (streamErr) => {
      process.stdout.write('\n');
      console.error(`${new Date().toISOString()} Loi khi doc/gui file: ${streamErr.message}`);
    });

    readStream.pipe(res);
  });
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`HTTP (khong TLS) OTA server listening on port ${PORT} - CHI DUNG DEV/TEST`);
  console.log(`Serving firmware from: ${FIRMWARE_DIR}`);
});
