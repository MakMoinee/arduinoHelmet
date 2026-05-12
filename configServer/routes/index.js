var express = require('express');
var router = express.Router();
var { exec } = require('child_process');
var fs = require('fs');
var path = require('path');
var os = require('os');

/* GET home page. */
router.get('/', function(req, res, next) {
  res.send({
    "sps30":"http://192.168.137.65",
    "core":"http://192.168.137.180"
  });
});

router.get('/print', function(req, res, next) {
  var q = req.query;

  // Validate that all required params are present
  var required = ['pm1_before','pm25_before','pm10_before','pm1_after','pm25_after','pm10_after'];
  var missing = required.filter(function(k){ return q[k] === undefined; });
  if (missing.length) {
    return res.status(400).json({ error: 'Missing parameters: ' + missing.join(', ') });
  }

  // Build the print content
  var now = new Date();
  var dateStr = now.toLocaleDateString('en-US', { year:'numeric', month:'short', day:'numeric' });
  var timeStr = now.toLocaleTimeString('en-US', { hour:'2-digit', minute:'2-digit', second:'2-digit' });

  var lines = [
    '================================',
    '       AIR QUALITY RESULTS      ',
    '================================',
    dateStr + '  ' + timeStr,
    '--------------------------------',
    '          BEFORE                ',
    '  PM1.0  : ' + parseFloat(q.pm1_after).toFixed(2)   + ' ug/m3',
    '  PM2.5  : ' + parseFloat(q.pm25_after).toFixed(2)  + ' ug/m3',
    '  PM10   : ' + parseFloat(q.pm10_after).toFixed(2)  + ' ug/m3',
    '--------------------------------',
    '          AFTER                 ',
    '  PM1.0  : ' + parseFloat(q.pm1_before).toFixed(2)  + ' ug/m3',
    '  PM2.5  : ' + parseFloat(q.pm25_before).toFixed(2) + ' ug/m3',
    '  PM10   : ' + parseFloat(q.pm10_before).toFixed(2) + ' ug/m3',
    '================================',
    ''
  ].join('\n');

  // Write content to a temp file then print it (avoids multiline escaping issues)
  var tmpFile = path.join(os.tmpdir(), 'print_' + Date.now() + '.txt');
  fs.writeFileSync(tmpFile, lines, 'utf8');

  var psCmd = 'powershell.exe -Command "Get-Content \'' + tmpFile.replace(/\\/g, '\\\\') + '\' | Out-Printer -Name \'PT-210\'"';

  exec(psCmd, function(err, stdout, stderr) {
    fs.unlink(tmpFile, function(){});  // clean up temp file regardless
    if (err) {
      console.error('Print error:', stderr || err.message);
      return res.status(500).json({ error: 'Print failed', detail: stderr || err.message });
    }
    res.json({ success: true, message: 'Sent to PT-210' });
  });
});

module.exports = router;
