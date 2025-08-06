#!/usr/bin/env python3
"""
Convert Howdy PNG images to LVGL C image arrays
Resizes images to optimal sizes for 800x800 display
"""

import os
import sys
from PIL import Image
import struct

def rgb565_from_rgb888(r, g, b):
    """Convert RGB888 to RGB565"""
    r = (r >> 3) & 0x1F
    g = (g >> 2) & 0x3F  
    b = (b >> 3) & 0x1F
    return (r << 11) | (g << 5) | b

def convert_png_to_lvgl_c(png_path, output_path, target_width, target_height):
    """Convert PNG to LVGL C array format"""
    
    # Open and resize image
    img = Image.open(png_path)
    original_size = img.size
    
    # Convert to RGBA if not already
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # Resize with high-quality resampling
    img = img.resize((target_width, target_height), Image.Resampling.LANCZOS)
    
    # Get image data
    pixels = list(img.getdata())
    
    # Convert to RGB565 with alpha
    rgb565_data = []
    alpha_data = []
    
    for r, g, b, a in pixels:
        # Convert RGB to RGB565
        rgb565 = rgb565_from_rgb888(r, g, b)
        rgb565_data.append(rgb565)
        alpha_data.append(a)
    
    # Generate C file
    img_name = os.path.splitext(os.path.basename(png_path))[0].lower()
    var_name = f"howdy_img_{img_name}"
    
    header_content = f"""/* Auto-generated LVGL image data from {os.path.basename(png_path)} */
/* Original size: {original_size[0]}x{original_size[1]}, Target: {target_width}x{target_height} */

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
    
    c_content = f"""/* Auto-generated LVGL image data from {os.path.basename(png_path)} */
/* Original size: {original_size[0]}x{original_size[1]}, Target: {target_width}x{target_height} */

#include "lvgl.h"

static const uint8_t {var_name}_map[] = {{
"""
    
    # Add RGB565 data (2 bytes per pixel)
    for i, rgb565 in enumerate(rgb565_data):
        if i % 8 == 0:
            c_content += "\n    "
        c_content += f"0x{rgb565 & 0xFF:02x}, 0x{(rgb565 >> 8) & 0xFF:02x}, "
    
    c_content += f"""
}};

const lv_img_dsc_t {var_name} = {{
    .header.cf = LV_IMG_CF_RGB565,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = {target_width},
    .header.h = {target_height},
    .data_size = {len(rgb565_data) * 2},
    .data = {var_name}_map,
}};
"""
    
    # Write files
    header_path = os.path.join(output_path, f"{var_name}.h")
    c_path = os.path.join(output_path, f"{var_name}.c")
    
    with open(header_path, 'w') as f:
        f.write(header_content)
    
    with open(c_path, 'w') as f:
        f.write(c_content)
    
    print(f"Converted {png_path} -> {header_path}, {c_path}")
    print(f"  Size: {original_size[0]}x{original_size[1]} -> {target_width}x{target_height}")
    return var_name

def main():
    visual_dir = "/Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen/Visual"
    output_dir = "/Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen/components/ui_manager/images"
    
    # Image mappings with target sizes optimized for 800x800 display
    images = [
        ("HowdyMidget.png", 240, 350),      # Main character - larger
        ("ArmRaiseHowdy.png", 240, 350),   # Greeting pose
        ("HowdyLeft.png", 240, 350),       # Looking left
        ("howdyRight.png", 240, 350),      # Looking right  
        ("HowdyRight2.png", 240, 350),     # Alternative right
        ("howdybackward.png", 240, 350),   # Looking back
    ]
    
    os.makedirs(output_dir, exist_ok=True)
    
    converted_images = []
    
    for png_file, width, height in images:
        png_path = os.path.join(visual_dir, png_file)
        if os.path.exists(png_path):
            var_name = convert_png_to_lvgl_c(png_path, output_dir, width, height)
            converted_images.append(var_name)
        else:
            print(f"Warning: {png_path} not found")
    
    # Generate combined header
    combined_header = """/* Combined header for all Howdy images - Large versions for 800x800 display */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

"""
    
    for var_name in converted_images:
        combined_header += f'#include "{var_name}.h"\n'
    
    combined_header += """
#ifdef __cplusplus
}
#endif
"""
    
    with open(os.path.join(output_dir, "howdy_images.h"), 'w') as f:
        f.write(combined_header)
    
    print(f"\nGenerated {len(converted_images)} large Howdy images for 800x800 display!")
    print(f"Images are now 240x350 pixels (was 88x128)")

if __name__ == "__main__":
    main()