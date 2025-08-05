#!/usr/bin/env python3
"""
Convert PNG images to LVGL C array format for ESP32-P4 Voice Assistant UI
Optimized for embedded systems with memory constraints
"""

import os
import sys
from PIL import Image
import argparse

def convert_png_to_lvgl_c_array(image_path, output_path, var_name, max_width=128, max_height=128):
    """
    Convert PNG image to LVGL C array format
    
    Args:
        image_path: Path to input PNG file
        output_path: Path to output C file
        var_name: C variable name for the image data
        max_width: Maximum width for scaling (default 128px for embedded)
        max_height: Maximum height for scaling (default 128px for embedded)
    """
    try:
        # Open and process image
        img = Image.open(image_path)
        original_size = img.size
        print(f"Processing {image_path}: {original_size[0]}x{original_size[1]}")
        
        # Convert to RGBA if not already
        if img.mode != 'RGBA':
            img = img.convert('RGBA')
        
        # Scale down if needed for embedded constraints
        if img.width > max_width or img.height > max_height:
            img.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
            print(f"Scaled to: {img.width}x{img.height}")
        
        # Convert to RGB565 format for LVGL (saves memory)
        width, height = img.size
        
        # Generate C header content
        header_content = f"""/* Auto-generated LVGL image data from {os.path.basename(image_path)} */
/* Original size: {original_size[0]}x{original_size[1]}, Scaled: {width}x{height} */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {{
#endif

extern const lv_img_dsc_t {var_name};

#ifdef __cplusplus
}}
#endif
"""
        
        # Generate C source content
        c_content = f"""/* Auto-generated LVGL image data from {os.path.basename(image_path)} */
/* Original size: {original_size[0]}x{original_size[1]}, Scaled: {width}x{height} */

#include "{var_name}.h"

/* Image data in RGB565 format */
static const uint8_t {var_name}_data[] = {{
"""
        
        # Convert pixels to RGB565 format
        pixel_data = []
        for y in range(height):
            for x in range(width):
                r, g, b, a = img.getpixel((x, y))
                
                # Handle transparency - blend with black background
                if a < 255:
                    alpha = a / 255.0
                    r = int(r * alpha)
                    g = int(g * alpha) 
                    b = int(b * alpha)
                
                # Convert to RGB565 (16-bit: 5R + 6G + 5B)
                r565 = (r >> 3) & 0x1F
                g565 = (g >> 2) & 0x3F  
                b565 = (b >> 3) & 0x1F
                
                rgb565 = (r565 << 11) | (g565 << 5) | b565
                
                # Store as little-endian bytes
                pixel_data.append(rgb565 & 0xFF)
                pixel_data.append((rgb565 >> 8) & 0xFF)
        
        # Format pixel data as C array
        for i, byte in enumerate(pixel_data):
            if i % 16 == 0:
                c_content += "\n    "
            c_content += f"0x{byte:02X}, "
        
        c_content += f"""
}};

/* LVGL image descriptor */
const lv_img_dsc_t {var_name} = {{
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = {width},
    .header.h = {height},
    .data_size = {len(pixel_data)},
    .data = {var_name}_data,
}};
"""
        
        # Write header file
        header_path = output_path.replace('.c', '.h')
        with open(header_path, 'w') as f:
            f.write(header_content)
        
        # Write C source file
        with open(output_path, 'w') as f:
            f.write(c_content)
            
        print(f"Generated: {header_path} and {output_path}")
        print(f"Image size: {width}x{height}, Data size: {len(pixel_data)} bytes")
        
        return True
        
    except Exception as e:
        print(f"Error processing {image_path}: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Convert PNG images to LVGL C arrays')
    parser.add_argument('input_dir', help='Input directory containing PNG files')
    parser.add_argument('output_dir', help='Output directory for C files')
    parser.add_argument('--max-size', type=int, default=128, help='Maximum image dimension')
    
    args = parser.parse_args()
    
    # Create output directory if it doesn't exist
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Process all PNG files in input directory
    png_files = [f for f in os.listdir(args.input_dir) if f.lower().endswith('.png')]
    
    if not png_files:
        print(f"No PNG files found in {args.input_dir}")
        return
    
    print(f"Found {len(png_files)} PNG files to convert...")
    
    success_count = 0
    for png_file in png_files:
        input_path = os.path.join(args.input_dir, png_file)
        
        # Generate variable name from filename
        var_name = png_file.lower().replace('.png', '').replace('-', '_').replace(' ', '_')
        var_name = f"howdy_img_{var_name}"
        
        output_path = os.path.join(args.output_dir, f"{var_name}.c")
        
        if convert_png_to_lvgl_c_array(input_path, output_path, var_name, args.max_size, args.max_size):
            success_count += 1
    
    print(f"\nConversion complete: {success_count}/{len(png_files)} images processed successfully")
    
    # Generate combined header file for easy inclusion
    combined_header = f"""/* Combined header for all Howdy images */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {{
#endif

"""
    
    for png_file in png_files:
        var_name = png_file.lower().replace('.png', '').replace('-', '_').replace(' ', '_')
        var_name = f"howdy_img_{var_name}"
        combined_header += f"#include \"{var_name}.h\"\n"
    
    combined_header += """
#ifdef __cplusplus
}
#endif
"""
    
    combined_header_path = os.path.join(args.output_dir, "howdy_images.h")
    with open(combined_header_path, 'w') as f:
        f.write(combined_header)
    
    print(f"Generated combined header: {combined_header_path}")

if __name__ == "__main__":
    main()