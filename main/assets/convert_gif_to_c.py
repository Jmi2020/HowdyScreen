#!/usr/bin/env python3
import sys
import os

def convert_gif_to_c_array(gif_path, output_path):
    # Read binary data
    with open(gif_path, 'rb') as f:
        data = f.read()
    
    # Generate C array
    with open(output_path, 'w') as f:
        # Write header
        f.write('#include <stdint.h>\n\n')
        f.write('// Generated from howdy64.gif\n')
        f.write(f'// File size: {len(data)} bytes\n\n')
        
        # Write array declaration
        f.write('const uint8_t howdy64_gif_data[] = {\n')
        
        # Write data in hex format, 16 bytes per line
        for i in range(0, len(data), 16):
            f.write('    ')
            chunk = data[i:i+16]
            hex_values = [f'0x{b:02x}' for b in chunk]
            f.write(', '.join(hex_values))
            if i + 16 < len(data):
                f.write(',')
            f.write('\n')
        
        f.write('};\n\n')
        f.write(f'const uint32_t howdy64_gif_size = {len(data)};\n')

if __name__ == "__main__":
    convert_gif_to_c_array(
        '/Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen/main/assets/howdy64.gif',
        '/Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen/main/assets/howdy64_gif.c'
    )
    print("Conversion complete!")