# SPS30 Sensor Data Reference

## JSON Response Keys

| Key | Full Name | What It Measures |
|---|---|---|
| `mc1p0` | Mass Concentration PM1.0 | Weight of particles smaller than 1 µm per m³ of air (µg/m³) |
| `mc2p5` | Mass Concentration PM2.5 | Weight of particles smaller than 2.5 µm — the standard air quality metric |
| `mc4p0` | Mass Concentration PM4.0 | Weight of particles smaller than 4 µm |
| `mc10p0` | Mass Concentration PM10 | Weight of particles smaller than 10 µm — dust, pollen, mould |
| `nc0p5` | Number Concentration 0.5 µm | Count of particles smaller than 0.5 µm per cm³ |
| `nc1p0` | Number Concentration 1.0 µm | Count of particles smaller than 1 µm per cm³ |
| `nc2p5` | Number Concentration 2.5 µm | Count of particles smaller than 2.5 µm per cm³ |
| `nc4p0` | Number Concentration 4.0 µm | Count of particles smaller than 4 µm per cm³ |
| `nc10p0` | Number Concentration 10 µm | Count of particles smaller than 10 µm per cm³ |

### Key

- **`mc` = mass** — how heavy the particles are collectively (µg/m³). This is what air quality indexes (AQI) are based on. PM2.5 is the most watched one for health.
- **`nc` = number** — how many individual particles are floating around per cm³, regardless of their weight.
- The number after (`1p0`, `2p5`, etc.) is the **size cutoff in micrometres** — everything smaller than that size is counted/weighed.

> WHO safe daily average for PM2.5 is 15 µg/m³.


## tests_sps.ino sample response
```
{
"mc1p0": 1.2137,
"mc2p5": 1.2834,
"mc4p0": 1.2834,
"mc10p0": 1.2834,
"nc0p5": 8.2235,
"nc1p0": 9.636,
"nc2p5": 9.686,
"nc4p0": 9.6895,
"nc10p0": 9.691,
"typicalParticleSize": 0.5133,
"lastUpdatedMs": 490149
}
```