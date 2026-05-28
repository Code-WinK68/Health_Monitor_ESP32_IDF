# ECG ESP32 Monitor

Dự án đọc tín hiệu ECG từ cảm biến AD8232 bằng ESP32-S3 sử dụng ESP-IDF.

## Features

* Đọc tín hiệu ECG thô
* Xuất dữ liệu Serial tốc độ cao
* Hỗ trợ Serial Studio
* Tần số lấy mẫu 250Hz
* Xuất CSV để xử lý dữ liệu

## Hardware

* ESP32-S3
* AD8232 ECG Sensor
* USB Type-C

## Pin Connection

| AD8232 | ESP32-S3 |
| ------ | -------- |
| OUTPUT | GPIO1    |
| LO -   | GPIO2    |
| LO +   | GPIO3    |
| 3.3V   | 3.3V     |
| GND    | GND      |

## Project Structure

```text
main/
 ├── main.c
 ├── CMakeLists.txt
build/
```

## Build Project

```bash
idf.py build
```

## Flash Firmware

```bash
idf.py flash
```

## Open Serial Monitor

```bash
idf.py monitor
```

## Serial Studio Format

```text
/*ECG_VALUE*/
```

## Screenshot

Thêm ảnh project tại đây. 

## Future Improvements

* Filter nhiễu ECG
* Tính BPM
* Hiển thị waveform OLED
* Xuất dữ liệu WiFi/Bluetooth

## Author

Trần Văn Thắng
HUST - Hanoi University of Science and Technology
