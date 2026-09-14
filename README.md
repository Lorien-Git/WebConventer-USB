```
    ⣠⣴⣿⣿⣿⣿⣿⣿⡇  ⢸⣿⣿⣷⡀    
    ⢸⣿⣿⣿⣿⣿⣿⣿⣿⡄ ⢸⣿⣿⣿⡇    
     ⢸⣿⣿⣿⣿⡿⠿⠿⠿⠾⠛⠛⣉⣇⣀  ⢀⡀ 
 ⣠⣶⣿⣿⣿⣿⣿⡿⢟⣛⣭⣶⢿⣿⠟⠻⠭⠛⡟⠛⠉ 
       ⢸    ⣰⣿⣿⣷⠈⠒⢒⢊⠤⠊⠁   
        ⢹⠋⢸⡿⠶  ⢹⡶⢖⠉       
          ⢡⢻⣙⣵⡿⡆⠱⡀       
            ⠈⡇  ⡸⠋ 
```
Credits: @Lorien-Git, @tytso, @yhirose, @richgel999

## Installation & Usage

### Method 1: Release (Recommended)
1. Download `webconventer-usb.exe` from the latest [Releases](https://github.com/Lorien-Git/webconventer-usb/releases).
2. Run `webconventer-usb.exe` as Administrator.
3. The modern web control panel will automatically launch in your browser at `http://localhost:48080`.

### Method 2: Debug (Developer / Detailed Console Logs)
1. Run `build.bat` and select `[2] Build Debug` (or run `webconventer-usb-debug.exe`).
2. Run `webconventer-usb-debug.exe` as Administrator.
3. A live console will open alongside the web panel showing granular block I/O and format stage logs in real-time.

## Third-Party Components
- **libext2fs (Theodore Ts'o)**: Ext4 journal (`mkjournal`), block & inode group allocator, superblock initialization (GPLv2/LGPLv2).
- **cpp-httplib (Yuji Hirose)**: Fast multi-threaded embedded HTTP server (MIT).
- **miniz (Rich Geldreich)**: Streaming ZIP archive compressor and extractor (MIT).

## License

Copyright (c) 2026 Lorien-Git. All rights reserved. 
No one is permitted to copy, modify, or distribute this software or any part of it without explicit permission.
