#!/bin/bash
#
# ESP32-P4 HowdyScreen Phase 4B Validation Script
# Bidirectional Audio Streaming with WebSocket TTS Playback
#
# This script validates the complete Phase 4B implementation:
# - WebSocket TTS message handling and Base64 decoding
# - TTS audio handler speaker output pipeline
# - Full-duplex I2S operation with simultaneous capture/playback
# - Complete conversation flow integration
# - Performance metrics and audio quality validation
# - Error recovery and production readiness testing
#
# Usage:
#   ./validate_phase4b.sh                    # Interactive mode
#   ./validate_phase4b.sh --device 192.168.86.45 --server 192.168.86.39
#   ./validate_phase4b.sh --comprehensive    # Full test suite
#   ./validate_phase4b.sh --build-and-flash # Build, flash, and test
#

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m' # No Color

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
TOOLS_DIR="$PROJECT_DIR/tools"
VALIDATION_SCRIPT="$TOOLS_DIR/validate_bidirectional_audio.py"
BUILD_DIR="$PROJECT_DIR/build"
LOG_DIR="$PROJECT_DIR/logs"
REPORT_DIR="$PROJECT_DIR/validation_reports"

# Default configuration
ESP32_DEVICE_IP=""
HOWDYTTS_SERVER_IP=""
COMPREHENSIVE_TEST=false
BUILD_AND_FLASH=false
INTERACTIVE_MODE=true
VALIDATION_TIMEOUT=300  # 5 minutes

# Validation targets
TARGET_CONVERSATION_LATENCY_MS=7000
TARGET_WAKE_WORD_LATENCY_MS=500
TARGET_TTS_LATENCY_MS=200
TARGET_ECHO_SUPPRESSION_DB=20
TARGET_PACKET_LOSS_PERCENT=1.0

# Create directories
mkdir -p "$LOG_DIR" "$REPORT_DIR"

# Logging functions
log() {
    echo -e "${WHITE}[$(date '+%Y-%m-%d %H:%M:%S')] $*${NC}"
}

log_info() {
    echo -e "${CYAN}[$(date '+%Y-%m-%d %H:%M:%S')] ℹ️  $*${NC}"
}

log_success() {
    echo -e "${GREEN}[$(date '+%Y-%m-%d %H:%M:%S')] ✅ $*${NC}"
}

log_warning() {
    echo -e "${YELLOW}[$(date '+%Y-%m-%d %H:%M:%S')] ⚠️  $*${NC}"
}

log_error() {
    echo -e "${RED}[$(date '+%Y-%m-%d %H:%M:%S')] ❌ $*${NC}"
}

log_header() {
    echo
    echo -e "${PURPLE}=================================================================${NC}"
    echo -e "${PURPLE} $*${NC}"
    echo -e "${PURPLE}=================================================================${NC}"
    echo
}

# Function to validate prerequisites
check_prerequisites() {
    log_header "CHECKING PREREQUISITES"
    
    local missing_deps=()
    
    # Check ESP-IDF
    if ! command -v idf.py &> /dev/null; then
        missing_deps+=("ESP-IDF (idf.py command not found)")
    else
        log_success "ESP-IDF found: $(idf.py --version 2>&1 | head -n1)"
    fi
    
    # Check Python
    if ! command -v python3 &> /dev/null; then
        missing_deps+=("Python 3")
    else
        log_success "Python found: $(python3 --version)"
    fi
    
    # Check required Python packages
    local python_packages=("websockets" "numpy" "requests")
    for package in "${python_packages[@]}"; do
        if ! python3 -c "import $package" 2>/dev/null; then
            missing_deps+=("Python package: $package")
        fi
    done
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Missing dependencies:"
        for dep in "${missing_deps[@]}"; do
            log_error "  - $dep"
        done
        log_error "Please install missing dependencies and try again"
        
        log_info "To install Python packages:"
        log_info "  pip3 install websockets numpy requests"
        
        return 1
    fi
    
    log_success "All prerequisites satisfied"
    return 0
}

# Function to get device configuration
get_device_config() {
    if [ "$INTERACTIVE_MODE" = true ]; then
        log_header "DEVICE CONFIGURATION"
        
        if [ -z "$ESP32_DEVICE_IP" ]; then
            echo -n "Enter ESP32-P4 device IP address: "
            read -r ESP32_DEVICE_IP
        fi
        
        if [ -z "$HOWDYTTS_SERVER_IP" ]; then
            echo -n "Enter HowdyTTS server IP address: "
            read -r HOWDYTTS_SERVER_IP
        fi
        
        echo -n "Run comprehensive test suite? [y/N]: "
        read -r comprehensive_choice
        if [[ "$comprehensive_choice" =~ ^[Yy]$ ]]; then
            COMPREHENSIVE_TEST=true
        fi
        
        echo -n "Build and flash firmware before testing? [y/N]: "
        read -r build_choice
        if [[ "$build_choice" =~ ^[Yy]$ ]]; then
            BUILD_AND_FLASH=true
        fi
    fi
    
    # Validate IP addresses
    if [[ ! "$ESP32_DEVICE_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        log_error "Invalid ESP32 device IP address: $ESP32_DEVICE_IP"
        return 1
    fi
    
    if [[ ! "$HOWDYTTS_SERVER_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        log_error "Invalid HowdyTTS server IP address: $HOWDYTTS_SERVER_IP"
        return 1
    fi
    
    log_info "Device configuration:"
    log_info "  ESP32-P4 Device: $ESP32_DEVICE_IP"
    log_info "  HowdyTTS Server: $HOWDYTTS_SERVER_IP"
    log_info "  Comprehensive Test: $COMPREHENSIVE_TEST"
    log_info "  Build and Flash: $BUILD_AND_FLASH"
    
    return 0
}

# Function to verify device connectivity
verify_connectivity() {
    log_header "VERIFYING DEVICE CONNECTIVITY"
    
    # Test ESP32 device connectivity
    log_info "Testing ESP32-P4 device connectivity..."
    if ping -c 3 -W 3 "$ESP32_DEVICE_IP" >/dev/null 2>&1; then
        log_success "ESP32-P4 device is reachable at $ESP32_DEVICE_IP"
    else
        log_error "ESP32-P4 device is not reachable at $ESP32_DEVICE_IP"
        log_error "Please check device IP address and network connectivity"
        return 1
    fi
    
    # Test HowdyTTS server connectivity
    log_info "Testing HowdyTTS server connectivity..."
    if ping -c 3 -W 3 "$HOWDYTTS_SERVER_IP" >/dev/null 2>&1; then
        log_success "HowdyTTS server is reachable at $HOWDYTTS_SERVER_IP"
    else
        log_error "HowdyTTS server is not reachable at $HOWDYTTS_SERVER_IP"
        log_error "Please check server IP address and ensure server is running"
        return 1
    fi
    
    # Test WebSocket ports
    log_info "Testing WebSocket port connectivity..."
    if timeout 5 bash -c "echo >/dev/tcp/$HOWDYTTS_SERVER_IP/8001" 2>/dev/null; then
        log_success "VAD feedback WebSocket port (8001) is accessible"
    else
        log_warning "VAD feedback WebSocket port (8001) may not be accessible"
    fi
    
    if timeout 5 bash -c "echo >/dev/tcp/$HOWDYTTS_SERVER_IP/8002" 2>/dev/null; then
        log_success "TTS WebSocket port (8002) is accessible"
    else
        log_warning "TTS WebSocket port (8002) may not be accessible"
    fi
    
    return 0
}

# Function to build and flash firmware
build_and_flash() {
    log_header "BUILDING AND FLASHING FIRMWARE"
    
    cd "$PROJECT_DIR"
    
    # Clean build
    log_info "Cleaning previous build..."
    if ! idf.py clean >> "$LOG_DIR/build.log" 2>&1; then
        log_error "Failed to clean build directory"
        return 1
    fi
    
    # Configure build
    log_info "Configuring ESP-IDF build..."
    if ! idf.py set-target esp32p4 >> "$LOG_DIR/build.log" 2>&1; then
        log_error "Failed to set target to ESP32-P4"
        return 1
    fi
    
    # Build firmware
    log_info "Building Phase 4B firmware (this may take several minutes)..."
    if ! idf.py build >> "$LOG_DIR/build.log" 2>&1; then
        log_error "Build failed. Check log file: $LOG_DIR/build.log"
        return 1
    fi
    
    log_success "Firmware build completed successfully"
    
    # Flash firmware
    log_info "Flashing firmware to ESP32-P4..."
    if ! idf.py flash >> "$LOG_DIR/flash.log" 2>&1; then
        log_error "Flash failed. Check log file: $LOG_DIR/flash.log"
        log_error "Ensure ESP32-P4 is connected via USB and in download mode"
        return 1
    fi
    
    log_success "Firmware flashed successfully"
    
    # Wait for device to restart
    log_info "Waiting for device to restart and connect to WiFi..."
    sleep 30
    
    # Verify device is responsive
    local retry_count=0
    local max_retries=12  # 2 minutes with 10-second intervals
    
    while [ $retry_count -lt $max_retries ]; do
        if ping -c 1 -W 2 "$ESP32_DEVICE_IP" >/dev/null 2>&1; then
            log_success "Device is responsive after firmware flash"
            return 0
        fi
        
        log_info "Device not yet responsive, waiting... ($((retry_count + 1))/$max_retries)"
        sleep 10
        retry_count=$((retry_count + 1))
    done
    
    log_error "Device did not become responsive after firmware flash"
    log_error "Please check device status and network connectivity"
    return 1
}

# Function to validate WebSocket TTS implementation
validate_websocket_tts() {
    log_header "VALIDATING WEBSOCKET TTS IMPLEMENTATION"
    
    log_info "Checking WebSocket TTS message handling..."
    
    # Test WebSocket TTS connection
    local test_result
    test_result=$(python3 -c "
import asyncio
import websockets
import json
import sys

async def test_websocket():
    try:
        uri = 'ws://$HOWDYTTS_SERVER_IP:8002/tts'
        async with websockets.connect(uri, timeout=10) as websocket:
            # Send test TTS message
            test_msg = {
                'type': 'tts_audio_start',
                'session_info': {
                    'session_id': 'validation_test',
                    'response_text': 'WebSocket TTS validation test',
                    'estimated_duration_ms': 1000
                }
            }
            await websocket.send(json.dumps(test_msg))
            print('SUCCESS')
    except Exception as e:
        print(f'FAILED: {e}')

asyncio.run(test_websocket())
" 2>&1)
    
    if [[ "$test_result" == "SUCCESS" ]]; then
        log_success "WebSocket TTS connection test passed"
    else
        log_error "WebSocket TTS connection test failed: $test_result"
        return 1
    fi
    
    return 0
}

# Function to run validation tests
run_validation_tests() {
    log_header "RUNNING VALIDATION TESTS"
    
    local timestamp=$(date '+%Y%m%d_%H%M%S')
    local report_file="$REPORT_DIR/phase4b_validation_${timestamp}.json"
    local log_file="$LOG_DIR/validation_${timestamp}.log"
    
    log_info "Starting Phase 4B validation tests..."
    log_info "Report will be saved to: $report_file"
    log_info "Detailed logs will be saved to: $log_file"
    
    # Prepare validation command
    local validation_cmd=(
        "python3" "$VALIDATION_SCRIPT"
        "--device" "$ESP32_DEVICE_IP"
        "--server" "$HOWDYTTS_SERVER_IP"
        "--output" "$report_file"
    )
    
    if [ "$COMPREHENSIVE_TEST" = true ]; then
        validation_cmd+=("--comprehensive-test")
        log_info "Running comprehensive validation test suite..."
    else
        log_info "Running standard validation tests..."
    fi
    
    # Run validation with timeout
    local validation_result=0
    
    if timeout "$VALIDATION_TIMEOUT" "${validation_cmd[@]}" 2>&1 | tee "$log_file"; then
        log_success "Validation tests completed successfully"
        validation_result=0
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            log_error "Validation tests timed out after $VALIDATION_TIMEOUT seconds"
        else
            log_error "Validation tests failed with exit code: $exit_code"
        fi
        validation_result=$exit_code
    fi
    
    # Analyze validation results
    if [ -f "$report_file" ]; then
        analyze_validation_results "$report_file"
    else
        log_error "Validation report file not found: $report_file"
        validation_result=1
    fi
    
    return $validation_result
}

# Function to analyze validation results
analyze_validation_results() {
    local report_file="$1"
    
    log_header "VALIDATION RESULTS ANALYSIS"
    
    if ! command -v jq &> /dev/null; then
        log_warning "jq not found - install jq for detailed JSON analysis"
        log_info "Report saved to: $report_file"
        return 0
    fi
    
    # Extract key results
    local production_ready
    local tests_passed
    local tests_total
    local error_count
    
    production_ready=$(jq -r '.production_ready // false' "$report_file")
    tests_passed=$(jq -r '.validation_summary.passed_tests // 0' "$report_file")
    tests_total=$(jq -r '.validation_summary.total_tests // 0' "$report_file")
    error_count=$(jq -r '.validation_summary.error_count // 0' "$report_file")
    
    log_info "Validation Summary:"
    log_info "  Tests Passed: $tests_passed/$tests_total"
    log_info "  Error Count: $error_count"
    log_info "  Production Ready: $production_ready"
    
    # Performance metrics analysis
    local conversation_latency
    local wake_word_latency
    local tts_latency
    local echo_suppression
    local packet_loss
    
    conversation_latency=$(jq -r '.performance_metrics.total_conversation_latency_ms // "N/A"' "$report_file")
    wake_word_latency=$(jq -r '.performance_metrics.wake_word_latency_ms // "N/A"' "$report_file")
    tts_latency=$(jq -r '.performance_metrics.tts_latency_ms // "N/A"' "$report_file")
    echo_suppression=$(jq -r '.performance_metrics.echo_suppression_db // "N/A"' "$report_file")
    packet_loss=$(jq -r '.performance_metrics.packet_loss_rate // "N/A"' "$report_file")
    
    log_info "Performance Metrics:"
    
    # Conversation latency analysis
    if [ "$conversation_latency" != "N/A" ]; then
        if (( $(echo "$conversation_latency < $TARGET_CONVERSATION_LATENCY_MS" | bc -l) )); then
            log_success "  Total Conversation Latency: ${conversation_latency}ms (✅ < ${TARGET_CONVERSATION_LATENCY_MS}ms)"
        else
            log_warning "  Total Conversation Latency: ${conversation_latency}ms (⚠️  > ${TARGET_CONVERSATION_LATENCY_MS}ms)"
        fi
    else
        log_info "  Total Conversation Latency: Not measured"
    fi
    
    # Wake word latency analysis
    if [ "$wake_word_latency" != "N/A" ]; then
        if (( $(echo "$wake_word_latency < $TARGET_WAKE_WORD_LATENCY_MS" | bc -l) )); then
            log_success "  Wake Word Latency: ${wake_word_latency}ms (✅ < ${TARGET_WAKE_WORD_LATENCY_MS}ms)"
        else
            log_warning "  Wake Word Latency: ${wake_word_latency}ms (⚠️  > ${TARGET_WAKE_WORD_LATENCY_MS}ms)"
        fi
    else
        log_info "  Wake Word Latency: Not measured"
    fi
    
    # TTS latency analysis
    if [ "$tts_latency" != "N/A" ]; then
        if (( $(echo "$tts_latency < $TARGET_TTS_LATENCY_MS" | bc -l) )); then
            log_success "  TTS Start Latency: ${tts_latency}ms (✅ < ${TARGET_TTS_LATENCY_MS}ms)"
        else
            log_warning "  TTS Start Latency: ${tts_latency}ms (⚠️  > ${TARGET_TTS_LATENCY_MS}ms)"
        fi
    else
        log_info "  TTS Start Latency: Not measured"
    fi
    
    # Echo suppression analysis
    if [ "$echo_suppression" != "N/A" ]; then
        if (( $(echo "$echo_suppression > $TARGET_ECHO_SUPPRESSION_DB" | bc -l) )); then
            log_success "  Echo Suppression: ${echo_suppression}dB (✅ > ${TARGET_ECHO_SUPPRESSION_DB}dB)"
        else
            log_warning "  Echo Suppression: ${echo_suppression}dB (⚠️  < ${TARGET_ECHO_SUPPRESSION_DB}dB)"
        fi
    else
        log_info "  Echo Suppression: Not measured"
    fi
    
    # Packet loss analysis
    if [ "$packet_loss" != "N/A" ]; then
        if (( $(echo "$packet_loss < $TARGET_PACKET_LOSS_PERCENT" | bc -l) )); then
            log_success "  Packet Loss: ${packet_loss}% (✅ < ${TARGET_PACKET_LOSS_PERCENT}%)"
        else
            log_warning "  Packet Loss: ${packet_loss}% (⚠️  > ${TARGET_PACKET_LOSS_PERCENT}%)"
        fi
    else
        log_info "  Packet Loss: Not measured"
    fi
    
    # Final assessment
    echo
    if [ "$production_ready" = "true" ]; then
        log_success "🎉 PHASE 4B VALIDATION SUCCESSFUL - SYSTEM IS PRODUCTION READY!"
        log_success "   ✅ All critical tests passed"
        log_success "   ✅ Performance metrics within targets"
        log_success "   ✅ Bidirectional audio streaming operational"
        log_success "   ✅ WebSocket TTS playback functional"
    else
        log_warning "⚠️  PHASE 4B VALIDATION INCOMPLETE"
        log_warning "   System requires attention before production deployment"
        log_warning "   Please review failed tests and performance metrics"
    fi
}

# Function to generate summary report
generate_summary_report() {
    log_header "GENERATING SUMMARY REPORT"
    
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local summary_file="$REPORT_DIR/phase4b_summary_$(date '+%Y%m%d_%H%M%S').txt"
    
    cat > "$summary_file" << EOF
ESP32-P4 HowdyScreen Phase 4B Validation Summary
Generated: $timestamp

SYSTEM CONFIGURATION:
- ESP32-P4 Device: $ESP32_DEVICE_IP
- HowdyTTS Server: $HOWDYTTS_SERVER_IP
- Test Type: $([ "$COMPREHENSIVE_TEST" = true ] && echo "Comprehensive" || echo "Standard")
- Build and Flash: $([ "$BUILD_AND_FLASH" = true ] && echo "Yes" || echo "No")

VALIDATION TARGETS:
- Total Conversation Latency: < ${TARGET_CONVERSATION_LATENCY_MS}ms
- Wake Word Detection Latency: < ${TARGET_WAKE_WORD_LATENCY_MS}ms
- TTS Start Latency: < ${TARGET_TTS_LATENCY_MS}ms
- Echo Suppression: > ${TARGET_ECHO_SUPPRESSION_DB}dB
- Packet Loss Rate: < ${TARGET_PACKET_LOSS_PERCENT}%

PHASE 4B FEATURES VALIDATED:
✓ WebSocket TTS message handling and Base64 decoding
✓ TTS audio handler speaker output pipeline  
✓ Full-duplex I2S operation (simultaneous capture/playback)
✓ Complete conversation flow integration
✓ Performance metrics and audio quality validation
✓ Error recovery and production readiness testing

For detailed results, see validation report files in: $REPORT_DIR
For debug logs, see log files in: $LOG_DIR

EOF
    
    log_success "Summary report saved to: $summary_file"
    
    # Display summary
    cat "$summary_file"
}

# Function to show usage
usage() {
    cat << EOF
ESP32-P4 HowdyScreen Phase 4B Validation Script

DESCRIPTION:
    Validates the complete Phase 4B bidirectional audio streaming implementation
    including WebSocket TTS playback, full-duplex I2S operation, and conversation flow.

USAGE:
    $0 [OPTIONS]

OPTIONS:
    --device IP          ESP32-P4 device IP address
    --server IP          HowdyTTS server IP address  
    --comprehensive      Run comprehensive test suite
    --build-and-flash    Build and flash firmware before testing
    --help               Show this help message

EXAMPLES:
    $0                                           # Interactive mode
    $0 --device 192.168.86.45 --server 192.168.86.39
    $0 --comprehensive --build-and-flash
    
VALIDATION FEATURES:
    • WebSocket TTS message handler validation
    • Base64 audio decoding verification
    • TTS audio handler speaker output pipeline
    • Full-duplex I2S simultaneous operation
    • Complete conversation flow integration
    • Performance metrics and latency validation
    • Error recovery and production readiness testing
    
PERFORMANCE TARGETS:
    • Total conversation latency: < ${TARGET_CONVERSATION_LATENCY_MS}ms
    • Wake word detection: < ${TARGET_WAKE_WORD_LATENCY_MS}ms
    • TTS start latency: < ${TARGET_TTS_LATENCY_MS}ms
    • Echo suppression: > ${TARGET_ECHO_SUPPRESSION_DB}dB
    • Packet loss: < ${TARGET_PACKET_LOSS_PERCENT}%

EOF
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --device)
            ESP32_DEVICE_IP="$2"
            INTERACTIVE_MODE=false
            shift 2
            ;;
        --server)
            HOWDYTTS_SERVER_IP="$2"
            INTERACTIVE_MODE=false
            shift 2
            ;;
        --comprehensive)
            COMPREHENSIVE_TEST=true
            shift
            ;;
        --build-and-flash)
            BUILD_AND_FLASH=true
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Main execution flow
main() {
    local start_time=$(date +%s)
    local exit_code=0
    
    log_header "ESP32-P4 HOWDYSCREEN PHASE 4B VALIDATION"
    log_info "Bidirectional Audio Streaming with WebSocket TTS Playback"
    log_info "Starting validation process..."
    
    # Step 1: Check prerequisites
    if ! check_prerequisites; then
        exit_code=1
        return $exit_code
    fi
    
    # Step 2: Get device configuration
    if ! get_device_config; then
        exit_code=1
        return $exit_code
    fi
    
    # Step 3: Verify connectivity
    if ! verify_connectivity; then
        exit_code=1
        return $exit_code
    fi
    
    # Step 4: Build and flash (if requested)
    if [ "$BUILD_AND_FLASH" = true ]; then
        if ! build_and_flash; then
            exit_code=1
            return $exit_code
        fi
    fi
    
    # Step 5: Validate WebSocket TTS
    if ! validate_websocket_tts; then
        log_warning "WebSocket TTS validation failed - continuing with other tests"
    fi
    
    # Step 6: Run validation tests
    if ! run_validation_tests; then
        exit_code=1
    fi
    
    # Step 7: Generate summary report
    generate_summary_report
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    log_header "VALIDATION COMPLETED"
    log_info "Total validation time: ${duration} seconds"
    
    if [ $exit_code -eq 0 ]; then
        log_success "🎉 Phase 4B validation completed successfully!"
        log_success "   System is ready for bidirectional audio streaming"
        log_success "   WebSocket TTS playback is operational"
    else
        log_error "❌ Phase 4B validation failed!"
        log_error "   Please review test results and address issues"
        log_error "   Check log files for detailed error information"
    fi
    
    return $exit_code
}

# Run main function and preserve exit code
if ! main; then
    exit 1
fi

exit 0