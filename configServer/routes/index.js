var express = require('express');
var router = express.Router();
var crypto = require('crypto');

/* In-memory receipt store { id: { html, expires } } */
var receipts = {};

/* GET home page. */
router.get('/', function(req, res, next) {
  res.send({
    "sps30":"http://192.168.137.65",
    "core":"http://192.168.137.180"
  });
});

/* POST/GET /print — generate a temp printable link */
router.get('/print', function(req, res, next) {
  var q = req.query;

  var required = ['pm1_before','pm25_before','pm10_before','pm1_after','pm25_after','pm10_after'];
  var missing = required.filter(function(k){ return q[k] === undefined; });
  if (missing.length) {
    return res.status(400).json({ error: 'Missing parameters: ' + missing.join(', ') });
  }

  var now = new Date();
  var dateStr = now.toLocaleDateString('en-US', { year:'numeric', month:'short', day:'2-digit' });
  var timeStr = now.toLocaleTimeString('en-US', { hour:'2-digit', minute:'2-digit', second:'2-digit' });

  var pm1b  = parseFloat(q.pm1_before).toFixed(2);
  var pm25b = parseFloat(q.pm25_before).toFixed(2);
  var pm10b = parseFloat(q.pm10_before).toFixed(2);
  var pm1a  = parseFloat(q.pm1_after).toFixed(2);
  var pm25a = parseFloat(q.pm25_after).toFixed(2);
  var pm10a = parseFloat(q.pm10_after).toFixed(2);

  var html = `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Air Quality Results</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Courier New', monospace;
      background: #f0f0f0;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 30px 10px;
    }
    .receipt {
      background: #fff;
      width: 300px;
      padding: 20px 16px;
      border-radius: 4px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.15);
      font-size: 13px;
      line-height: 1.6;
    }
    .title {
      text-align: center;
      font-weight: bold;
      font-size: 14px;
      margin-bottom: 4px;
    }
    .divider { border-top: 1px dashed #555; margin: 8px 0; }
    .section-label {
      text-align: center;
      font-weight: bold;
      margin: 4px 0;
    }
    .row { display: flex; justify-content: space-between; }
    .timestamp { text-align: center; font-size: 11px; color: #555; margin-bottom: 4px; }
    .print-btn {
      margin-top: 24px;
      padding: 10px 30px;
      font-size: 15px;
      background: #333;
      color: #fff;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
    .print-btn:hover { background: #000; }
    @media print {
      body { background: #fff; padding: 0; }
      .receipt { box-shadow: none; }
      .print-btn { display: none; }
    }
  </style>
</head>
<body>
  <div class="receipt">
    <div class="title">AIR QUALITY RESULTS</div>
    <div class="timestamp">${dateStr} &nbsp; ${timeStr}</div>
    <div class="divider"></div>
    <div class="section-label">BEFORE</div>
    <div class="row"><span>PM1.0</span><span>${pm1b} ug/m&sup3;</span></div>
    <div class="row"><span>PM2.5</span><span>${pm25b} ug/m&sup3;</span></div>
    <div class="row"><span>PM10</span><span>${pm10b} ug/m&sup3;</span></div>
    <div class="divider"></div>
    <div class="section-label">AFTER</div>
    <div class="row"><span>PM1.0</span><span>${pm1a} ug/m&sup3;</span></div>
    <div class="row"><span>PM2.5</span><span>${pm25a} ug/m&sup3;</span></div>
    <div class="row"><span>PM10</span><span>${pm10a} ug/m&sup3;</span></div>
    <div class="divider"></div>
  </div>
  <button class="print-btn" onclick="window.print()">Print</button>
</body>
</html>`;

  // Generate a unique ID, store for 10 minutes
  var id = crypto.randomBytes(8).toString('hex');
  var expires = Date.now() + 10 * 60 * 1000;
  receipts[id] = { html: html, expires: expires };

  // Clean up expired receipts
  Object.keys(receipts).forEach(function(k) {
    if (receipts[k].expires < Date.now()) delete receipts[k];
  });

  var link = 'http://' + req.headers.host + '/receipt/' + id;
  res.json({ success: true, url: link, expires_in: '10 minutes' });
});

/* GET /receipt/:id — serve the printable page */
router.get('/receipt/:id', function(req, res, next) {
  var entry = receipts[req.params.id];
  if (!entry || entry.expires < Date.now()) {
    return res.status(404).send('Receipt not found or expired.');
  }
  res.setHeader('Content-Type', 'text/html');
  res.send(entry.html);
});

module.exports = router;
