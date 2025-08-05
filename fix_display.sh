#!/bin/bash
echo "Applying ESP32-P4 display fixes..."

# Add LCD type configuration
if ! grep -q "CONFIG_BSP_LCD_TYPE_800_800_3_4_INCH" sdkconfig.defaults; then
    echo "" >> sdkconfig.defaults
    echo "# Display - Waveshare 3.4\" LCD Configuration" >> sdkconfig.defaults
    echo "CONFIG_BSP_LCD_TYPE_800_800_3_4_INCH=y" >> sdkconfig.defaults
    echo "✅ Added LCD type configuration"
else
    echo "✅ LCD type already configured"
fi

# Clean and rebuild
echo "Cleaning build..."
idf.py fullclean

echo "Building with new configuration..."
idf.py build

echo "Done! Now flash with: idf.py -p /dev/cu.usbserial-* flash monitor"
