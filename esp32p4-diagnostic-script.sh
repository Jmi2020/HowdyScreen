#!/bin/bash

# ESP32-P4 Waveshare Display Diagnostic Script
# Run this in your project directory: /Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen

echo "ESP32-P4 Waveshare 3.4\" Display Diagnostic"
echo "=========================================="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "❌ ERROR: Not in project directory. Please run from /Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen"
    exit 1
fi

echo "✅ Project directory found"

# Check for LCD type configuration
echo -n "Checking LCD type configuration... "
if grep -q "CONFIG_BSP_LCD_TYPE_800_800_3_4_INCH" sdkconfig.defaults 2>/dev/null || grep -q "CONFIG_BSP_LCD_TYPE_800_800_3_4_INCH=y" sdkconfig 2>/dev/null; then
    echo "✅ Found"
else
    echo "❌ MISSING - This is your problem!"
    echo "   Fix: Add 'CONFIG_BSP_LCD_TYPE_800_800_3_4_INCH=y' to sdkconfig.defaults"
fi

# Check BSP header usage
echo -n "Checking BSP header in main source... "
MAIN_FILE=$(grep -l "app_main" main/*.c 2>/dev/null | head -1)
if [ -n "$MAIN_FILE" ]; then
    if grep -q "esp32_p4_wifi6_touch_lcd_xc.h" "$MAIN_FILE"; then
        echo "✅ Correct BSP header"
    else
        echo "⚠️  Using generic BSP header"
        echo "   Fix: Change to #include \"bsp/esp32_p4_wifi6_touch_lcd_xc.h\""
    fi
else
    echo "❓ No main file found"
fi

# Check for MIPI-DSI escape clock setting
echo -n "Checking MIPI-DSI components... "
if [ -d "managed_components/espressif__esp_lcd_jd9365" ]; then
    echo "✅ JD9365 LCD driver found"
else
    echo "⚠️  JD9365 driver not found in managed_components"
fi

# Check display initialization method
echo -n "Checking display initialization... "
if grep -q "bsp_display_start" main/*.c 2>/dev/null; then
    echo "✅ Using high-level BSP function"
elif grep -q "bsp_display_new" main/*.c 2>/dev/null; then
    echo "⚠️  Using low-level display functions"
    echo "   Consider using bsp_display_start() for LVGL support"
else
    echo "❓ No display initialization found"
fi

# Check for required components
echo -n "Checking required components... "
MISSING_COMPONENTS=""
[ ! -d "managed_components/lvgl__lvgl" ] && MISSING_COMPONENTS="$MISSING_COMPONENTS lvgl"
[ ! -d "managed_components/espressif__esp_lvgl_port" ] && MISSING_COMPONENTS="$MISSING_COMPONENTS esp_lvgl_port"
[ ! -d "managed_components/waveshare__esp_lcd_touch_cst9217" ] && MISSING_COMPONENTS="$MISSING_COMPONENTS touch_driver"

if [ -z "$MISSING_COMPONENTS" ]; then
    echo "✅ All found"
else
    echo "⚠️  Missing:$MISSING_COMPONENTS"
fi

# Generate fix script
echo ""
echo "Generating fix script..."
cat > fix_display.sh << 'EOF'
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
EOF

chmod +x fix_display.sh
echo "✅ Created fix_display.sh - run './fix_display.sh' to apply fixes"

echo ""
echo "=========================================="
echo "Summary:"
echo "- Main issue: Missing LCD type configuration"
echo "- Run './fix_display.sh' to fix and rebuild"
echo "- Then flash and monitor to test"
echo "=========================================="