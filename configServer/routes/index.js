var express = require('express');
var router = express.Router();
var { spawn } = require('child_process');
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

  var receipt = [
    '================================',
    '       AIR QUALITY RESULTS      ',
    '================================',
    dateStr + '  ' + timeStr,
    '--------------------------------',
    '          BEFORE                ',
    '  PM1.0  : ' + parseFloat(q.pm1_before).toFixed(2)  + ' ug/m3',
    '  PM2.5  : ' + parseFloat(q.pm25_before).toFixed(2) + ' ug/m3',
    '  PM10   : ' + parseFloat(q.pm10_before).toFixed(2) + ' ug/m3',
    '--------------------------------',
    '          AFTER                 ',
    '  PM1.0  : ' + parseFloat(q.pm1_after).toFixed(2)   + ' ug/m3',
    '  PM2.5  : ' + parseFloat(q.pm25_after).toFixed(2)  + ' ug/m3',
    '  PM10   : ' + parseFloat(q.pm10_after).toFixed(2)  + ' ug/m3',
    '================================',
    ''
  ].join('\r\n');

  var stamp     = Date.now();
  var tmpTxt    = path.join(os.tmpdir(), 'receipt_' + stamp + '.txt');
  var tmpPs1    = path.join(os.tmpdir(), 'receipt_' + stamp + '.ps1');

  // Escape backslashes for embedding the path inside the .ps1 string literal
  var safeTxt = tmpTxt.replace(/\\/g, '\\\\');

  // PowerShell script — uses System.Drawing.Printing.PrintDocument
  // to avoid the Out-Printer "Length < 0" bug on thermal/POS printers
  var psScript = [
    'Add-Type -AssemblyName System.Drawing',
    '$printerName = "POS58"',
    '$filePath    = "' + safeTxt + '"',
    '$text        = [System.IO.File]::ReadAllText($filePath)',
    '$font        = New-Object System.Drawing.Font("Courier New", 8)',
    '$brush       = [System.Drawing.Brushes]::Black',
    '$script:txt  = $text',
    '$script:fnt  = $font',
    '$script:brsh = $brush',
    '$pd = New-Object System.Drawing.Printing.PrintDocument',
    '$pd.PrinterSettings.PrinterName = $printerName',
    '$pd.add_PrintPage({',
    '    param($s, $e)',
    '    $rect = New-Object System.Drawing.RectangleF(',
    '        $e.MarginBounds.Left, $e.MarginBounds.Top,',
    '        $e.MarginBounds.Width, $e.MarginBounds.Height)',
    '    $e.Graphics.DrawString($script:txt, $script:fnt, $script:brsh, $rect)',
    '    $e.HasMorePages = $false',
    '})',
    '$pd.Print()',
    '$pd.Dispose()',
    '$font.Dispose()'
  ].join('\r\n');

  fs.writeFileSync(tmpTxt, receipt, 'utf8');
  fs.writeFileSync(tmpPs1, psScript, 'utf8');

  var ps = spawn('powershell.exe', [
    '-ExecutionPolicy', 'Bypass',
    '-File', tmpPs1
  ]);

  var stderr = '';
  ps.stderr.on('data', function(d){ stderr += d.toString(); });

  ps.on('close', function(code) {
    fs.unlink(tmpTxt, function(){});
    fs.unlink(tmpPs1, function(){});
    if (code !== 0) {
      console.error('Print error:', stderr);
      return res.status(500).json({ error: 'Print failed', detail: stderr });
    }
    res.json({ success: true, message: 'Sent to POS58' });
  });
});

module.exports = router;
