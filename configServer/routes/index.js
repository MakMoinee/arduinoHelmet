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
    /* --- Page setup: 48mm x 210mm @ 203 DPI thermal label --- */
    @page {
      size: 48mm 210mm;
      margin: 2mm 1.5mm;
    }

    * { margin: 0; padding: 0; box-sizing: border-box; }

    /* Screen preview: center a scaled receipt card */
    body {
      font-family: 'Courier New', monospace;
      background: #e8e8e8;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 24px 10px;
    }

    /* 48mm at 96 dpi screen ≈ 181px; scale up 2x for readability */
    .receipt {
      background: #fff;
      width: 362px;           /* 181px × 2 screen preview */
      padding: 16px 14px;
      border-radius: 3px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.18);
      font-size: 15px;        /* scales down to ~7.5px on label */
      line-height: 1.55;
    }

    .title {
      text-align: center;
      font-weight: bold;
      font-size: 16px;
      letter-spacing: 0.5px;
      margin-bottom: 3px;
    }
    .subtitle {
      text-align: center;
      font-size: 12px;
      margin-bottom: 3px;
    }
    .timestamp {
      text-align: center;
      font-size: 12px;
      color: #444;
      margin-bottom: 3px;
    }
    .divider {
      border: none;
      border-top: 1px dashed #555;
      margin: 6px 0;
    }
    .section-label {
      text-align: center;
      font-weight: bold;
      font-size: 13px;
      margin: 3px 0 2px;
      letter-spacing: 0.5px;
    }
    .row {
      display: flex;
      justify-content: space-between;
      font-size: 13px;
    }
    .print-btn {
      margin-top: 20px;
      padding: 9px 28px;
      font-size: 14px;
      background: #333;
      color: #fff;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
    .print-btn:hover { background: #000; }

    /* --- Print styles: render at true 48mm width --- */
    @media print {
      body {
        background: #fff;
        padding: 0;
        display: block;
      }
      .receipt {
        width: 45mm;          /* 48mm page - 1.5mm margins each side */
        padding: 0;
        border-radius: 0;
        box-shadow: none;
        font-size: 7.5pt;     /* ~8px at 203 DPI — readable on thermal */
        line-height: 1.4;
      }
      .title      { font-size: 8.5pt; }
      .subtitle,
      .timestamp  { font-size: 7pt; }
      .section-label { font-size: 7.5pt; }
      .row        { font-size: 7pt; }
      .divider    { margin: 3px 0; }
      .print-btn  { display: none; }
    }
  </style>
</head>
<body>
  <div class="receipt">
    <div class="title">AIR QUALITY RESULTS</div>
    <div class="subtitle">Helmet Filter Report</div>
    <div class="timestamp">${dateStr} &nbsp; ${timeStr}</div>
    <div class="divider"></div>
    <div class="section-label">&#9650; BEFORE</div>
    <div class="row"><span>PM1.0</span><span>${pm1b} ug/m&sup3;</span></div>
    <div class="row"><span>PM2.5</span><span>${pm25b} ug/m&sup3;</span></div>
    <div class="row"><span>PM10 </span><span>${pm10b} ug/m&sup3;</span></div>
    <div class="divider"></div>
    <div class="section-label">&#9660; AFTER</div>
    <div class="row"><span>PM1.0</span><span>${pm1a} ug/m&sup3;</span></div>
    <div class="row"><span>PM2.5</span><span>${pm25a} ug/m&sup3;</span></div>
    <div class="row"><span>PM10 </span><span>${pm10a} ug/m&sup3;</span></div>
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
  console.log('Generated receipt link:', link);
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
