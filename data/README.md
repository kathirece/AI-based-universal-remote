# Gesture dataset

The ESP32 data collector prints one 20-sample motion window for every capture. Samples are taken every 50 ms and use acceleration in units of `g`.

## Columns

| Column | Meaning |
|---|---|
| `label` | `UP`, `DOWN`, `LEFT`, or `RIGHT` |
| `capture_id` | Identifier shared by the 20 rows from one gesture |
| `sample_index` | Sample number from 0 to 19 |
| `ax`, `ay`, `az` | MPU6050 acceleration on the X, Y, and Z axes |

## Collection recommendations

1. Fix the MPU6050 in the same physical orientation that will be used in the final remote.
2. Upload `firmware/data_collector/data_collector.ino` and open Serial Monitor at 115200 baud.
3. Send `u`, `d`, `l`, or `r` and immediately perform that gesture.
4. Record at least 20-30 complete captures for every class. Vary the speed and starting angle slightly.
5. Copy only the CSV header and data rows into `gesture_data.csv`. Lines beginning with `#` are comments and should be excluded from a training dataset.

Raw personal datasets are ignored by `.gitignore` by default. Add a reviewed dataset intentionally only when it contains no sensitive information.

This repository does not include model-training code. The template and collector are retained only to document the data produced by the hardware.
