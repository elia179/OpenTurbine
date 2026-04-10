import sys, io, os
# Force UTF-8 stdout so colorama progress bars don't crash on cp1252
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

os.environ['PYTHONIOENCODING'] = 'utf-8'

import esptool
sys.argv = [
    'esptool',
    '--chip', 'esp32',
    '--port', 'COM8',
    '--baud', '460800',
    'write-flash',
    '0x1000',  r'C:\Users\elial\Documents\Dev\OpenTurbine\.pio2\esp32dev\bootloader.bin',
    '0x8000',  r'C:\Users\elial\Documents\Dev\OpenTurbine\.pio2\esp32dev\partitions.bin',
    '0x10000', r'C:\Users\elial\Documents\Dev\OpenTurbine\.pio2\esp32dev\fw_flash.bin',
    '0x290000', r'C:\Users\elial\Documents\Dev\OpenTurbine\.pio2\esp32dev\littlefs.bin',
]
esptool._main()
