// server-https.js — HTTPS OTA firmware server (chay tren Windows bang Node.js)
// Yeu cau: da cai Node.js (https://nodejs.org). Khong can npm install gi them.
//
// Cau truc thu muc:
//   certs/ca_cert.pem, certs/ca_key.pem  <- copy tu Buoc 6 trong docs/OTA_PLAN.md
//   firmware/ota.bin                     <- copy tu build/ota.bin sau moi lan build
//
// Chay:  node server-https.js

const https = require('https');
const fs = require('fs');
const path = require('path');

const PORT = 8070;
const CERTS_DIR = path.join(__dirname, 'certs');
const FIRMWARE_DIR = path.join(__dirname, 'firmware');

// key/cert doc tu certs/ o day chinh la cap khoa da tao o Buoc 6 (docs/OTA_PLAN.md).
// Day la cap khoa ma ca_cert.pem (ban public) da duoc nhung san vao firmware ESP32
// (components/ota_manager/server_certs/ca_cert.pem) - nen ESP32 se tin tuong dung
// server nay khi bat tay TLS (xem phan "Co che ESP32 verify HTTPS server" trong OTA_PLAN.md).
const options = {
  key: fs.readFileSync(path.join(CERTS_DIR, 'ca_key.pem')),
  cert: fs.readFileSync(path.join(CERTS_DIR, 'ca_cert.pem')),
};

// https.createServer(options, handler):
//   - options.key/cert o tren la thu lam server nay "noi duoc" HTTPS (TLS handshake).
//   - handler (req, res) la ham duoc Node.js tu goi MOI KHI co 1 request HTTP(S) moi
//     toi server - day chinh la diem "CHAP NHAN REQUEST". Moi lan ESP32 (hoac trinh
//     duyet/curl) ket noi toi https://<IP>:8070/<ten_file>, Node.js se chay lai
//     dung callback nay voi:
//       req = thong tin request den (URL, method, remote IP, headers...)
//       res = doi tuong dung de TRA LOI lai cho client (ghi status, header, body)
const server = https.createServer(options, (req, res) => {
  // Lay ten file tu URL. path.basename() bo het phan thu muc trong req.url
  // (vd "/../../secret.txt" -> chi con "secret.txt") de tranh directory traversal -
  // client khong the doc file ngoai FIRMWARE_DIR du co co tinh chen "../" vao URL.
  const filePath = path.join(FIRMWARE_DIR, path.basename(req.url));

  // Kiem tra file co ton tai va la file thuong (khong phai thu muc) truoc khi doc,
  // de tranh crash hoac hanh vi la neu client xin 1 duong dan khong hop le.
  fs.stat(filePath, (err, stats) => {
    const status = !err && stats.isFile() ? 200 : 404;
    console.log(`${new Date().toISOString()} ${req.socket.remoteAddress} ${req.method} ${req.url} -> ${status}`);

    if (status === 404) {
      // Khong tim thay file -> TRA LOI ngay bang 404, khong doc/gui gi them.
      res.writeHead(404);
      res.end('Not found');
      return;
    }

    // ===== Phan GUI LAI FILE cho client (ESP32) =====
    // res.writeHead(200, headers): mo dau phan tra loi - bao client "request thanh
    // cong (200)", kem theo:
    //   Content-Type: noi client day la du lieu nhi phan thuan tuy (khong phai text/json)
    //   Content-Length: kich thuoc file, de client (esp_https_ota ben ESP32) biet
    //     truoc tong so byte can tai, phuc vu tinh % tien do OTA.
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': stats.size,
    });

    // fs.createReadStream(filePath): mo file va doc thanh nhieu chunk nho (khong
    // load ca file ota.bin vao RAM cung luc - quan trong vi file firmware co the
    // vai tram KB tro len). Bat them event 'data' (moi lan co 1 chunk moi doc
    // duoc) chi de TINH VA LOG tien do - khong anh huong luong gui file thuc su,
    // luong gui thuc su van la .pipe(res) o cuoi.
    const readStream = fs.createReadStream(filePath);
    const totalBytes = stats.size;
    let sentBytes = 0;
    let lastPercent = -1;
    const BAR_WIDTH = 40;

    readStream.on('data', (chunk) => {
      sentBytes += chunk.length;
      const percent = Math.min(100, Math.floor((sentBytes / totalBytes) * 100));
      if (percent === lastPercent) {
        return;   // Chi ve lai thanh progress khi % thuc su thay doi, tranh spam console
      }
      lastPercent = percent;

      const filled = Math.round((BAR_WIDTH * percent) / 100);
      const bar = '='.repeat(filled) + ' '.repeat(BAR_WIDTH - filled);
      // '\r' (carriage return, khong xuong dong) de de lan sau, cho ra hieu ung
      // 1 thanh progress bar chay tai cho thay vi in ra hang tram dong moi lan.
      process.stdout.write(`\r  [${bar}] ${percent}% (${sentBytes}/${totalBytes} bytes)`);
    });

    readStream.on('end', () => {
      process.stdout.write('\n');   // Xuong dong sau khi thanh progress chay xong, tranh de lan vao log tiep theo
      console.log(`${new Date().toISOString()} Gui thanh cong toan bo file "${path.basename(filePath)}" (${totalBytes} bytes) cho ${req.socket.remoteAddress}`);
    });

    readStream.on('error', (streamErr) => {
      process.stdout.write('\n');
      console.error(`${new Date().toISOString()} Loi khi doc/gui file: ${streamErr.message}`);
    });

    // .pipe(res): noi thang luong doc file vao thang response (res) - moi chunk
    // doc duoc se duoc tu dong ghi ra ket noi TCP/TLS toi ESP32 ngay lap tuc,
    // Node.js tu lo lieu backpressure (khong doc nhanh hon toc do ESP32 tieu thu).
    readStream.pipe(res);
  });
});

// Su kien 'secureConnection' ban ra ngay khi 1 client bat tay TLS THANH CONG voi
// server (truoc khi Node.js kip doc noi dung request HTTP ben trong) - day la diem
// dung nhat de log "client nao vua ket noi thanh cong", tach biet voi log tung
// request o tren (1 ket noi TCP/TLS co the mang nhieu request, nhung voi ESP32/
// esp_https_ota thi thuong la 1-1).
server.on('secureConnection', (tlsSocket) => {
  console.log(
    `${new Date().toISOString()} Client connected: ${tlsSocket.remoteAddress}:${tlsSocket.remotePort} ` +
    `(TLS ${tlsSocket.getProtocol()}, cipher ${tlsSocket.getCipher().name})`
  );
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`HTTPS OTA server listening on port ${PORT}`);
  console.log(`Serving firmware from: ${FIRMWARE_DIR}`);
});
