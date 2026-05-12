var express = require('express');
var router = express.Router();

/* GET home page. */
router.get('/', function(req, res, next) {
  res.send({
    "sps30":"http://192.168.137.65",
    "core":"http://192.168.137.180"
  });
});

module.exports = router;
