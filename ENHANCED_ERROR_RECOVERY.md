# Enhanced Error Handling and Recovery Mechanisms

## Overview

This document outlines enhanced error handling and recovery mechanisms for the ESP32-P4 HowdyScreen and HowdyTTS integration, ensuring robust production-ready operation.

## Current Error Handling Analysis

### HowdyTTS Server Error Handling

#### ✅ Existing Mechanisms
1. **WebSocket Connection Management**:
   - Automatic reconnection with exponential backoff
   - Connection health monitoring with ping/pong
   - Device session state preservation during disconnection

2. **Audio Processing Errors**:
   - VAD coordination with fallback strategies
   - Audio format validation and conversion
   - Recording failure recovery with retries

3. **Service Discovery**:
   - UDP timeout handling
   - Device availability monitoring
   - Automatic device selection

#### 🚀 Enhanced Error Handling Recommendations

### 1. Connection Resilience Enhancements

**Circuit Breaker Pattern Implementation**:

```python
# voice_assistant/connection_resilience.py
import time
from enum import Enum
from typing import Optional, Callable, Dict, Any

class CircuitState(Enum):
    CLOSED = "closed"      # Normal operation
    OPEN = "open"          # Failing, requests blocked
    HALF_OPEN = "half_open" # Testing recovery

class CircuitBreaker:
    def __init__(self, failure_threshold: int = 5, recovery_timeout: float = 30.0, 
                 success_threshold: int = 3):
        self.failure_threshold = failure_threshold
        self.recovery_timeout = recovery_timeout  
        self.success_threshold = success_threshold
        
        self.failure_count = 0
        self.success_count = 0
        self.last_failure_time: Optional[float] = None
        self.state = CircuitState.CLOSED
        
    def call(self, func: Callable, *args, **kwargs) -> Any:
        """Execute function with circuit breaker protection."""
        
        if self.state == CircuitState.OPEN:
            if self._should_attempt_reset():
                self.state = CircuitState.HALF_OPEN
                logging.info("Circuit breaker attempting reset")
            else:
                raise ConnectionError("Circuit breaker is OPEN - service unavailable")
        
        try:
            result = func(*args, **kwargs)
            self._record_success()
            return result
            
        except Exception as e:
            self._record_failure()
            raise e
    
    def _record_success(self):
        if self.state == CircuitState.HALF_OPEN:
            self.success_count += 1
            if self.success_count >= self.success_threshold:
                self._reset()
        else:
            self.failure_count = 0
            
    def _record_failure(self):
        self.failure_count += 1
        self.last_failure_time = time.time()
        
        if self.state == CircuitState.HALF_OPEN:
            self.state = CircuitState.OPEN
            logging.warning("Circuit breaker OPEN - half-open test failed")
        elif self.failure_count >= self.failure_threshold:
            self.state = CircuitState.OPEN
            logging.warning(f"Circuit breaker OPEN - {self.failure_count} failures")
    
    def _should_attempt_reset(self) -> bool:
        return (self.last_failure_time and 
                time.time() - self.last_failure_time >= self.recovery_timeout)
    
    def _reset(self):
        self.state = CircuitState.CLOSED
        self.failure_count = 0
        self.success_count = 0
        self.last_failure_time = None
        logging.info("Circuit breaker CLOSED - service recovered")
```

**Enhanced WebSocket TTS Server with Circuit Breaker**:

```python
# Enhanced voice_assistant/websocket_tts_server.py additions
class EnhancedWebSocketTTSServer(WebSocketTTSServer):
    def __init__(self, host: str = "0.0.0.0", port: int = 8002):
        super().__init__(host, port)
        
        # Circuit breakers for different operations
        self.device_circuit_breakers: Dict[str, CircuitBreaker] = {}
        self.send_circuit_breaker = CircuitBreaker(failure_threshold=3, recovery_timeout=10.0)
        
        # Enhanced error tracking
        self.error_stats = {
            'connection_failures': 0,
            'send_failures': 0,
            'protocol_errors': 0,
            'audio_format_errors': 0,
            'last_error_time': None
        }
    
    def get_device_circuit_breaker(self, device_id: str) -> CircuitBreaker:
        """Get or create circuit breaker for specific device."""
        if device_id not in self.device_circuit_breakers:
            self.device_circuit_breakers[device_id] = CircuitBreaker(
                failure_threshold=3, recovery_timeout=15.0, success_threshold=2
            )
        return self.device_circuit_breakers[device_id]
    
    async def send_tts_audio_with_recovery(self, device_id: str, audio_data: bytes, 
                                         session_id: str = None, max_retries: int = 3) -> bool:
        """Send TTS audio with enhanced error recovery."""
        
        circuit_breaker = self.get_device_circuit_breaker(device_id)
        
        for attempt in range(max_retries):
            try:
                return circuit_breaker.call(
                    self.send_tts_audio, device_id, audio_data, session_id
                )
                
            except ConnectionError as e:
                logging.warning(f"Circuit breaker blocked send to {device_id}: {e}")
                if attempt < max_retries - 1:
                    await asyncio.sleep(2 ** attempt)  # Exponential backoff
                continue
                
            except Exception as e:
                self.error_stats['send_failures'] += 1
                self.error_stats['last_error_time'] = time.time()
                
                logging.error(f"Send attempt {attempt + 1} failed for {device_id}: {e}")
                
                if attempt < max_retries - 1:
                    await asyncio.sleep(1.0 + attempt)
                continue
        
        # All retries failed
        logging.error(f"Failed to send TTS audio to {device_id} after {max_retries} attempts")
        return False
    
    def get_health_status(self) -> Dict[str, Any]:
        """Get comprehensive health status of TTS server."""
        device_states = {}
        for device_id, circuit_breaker in self.device_circuit_breakers.items():
            device_states[device_id] = {
                'circuit_state': circuit_breaker.state.value,
                'failure_count': circuit_breaker.failure_count,
                'healthy': circuit_breaker.state == CircuitState.CLOSED
            }
        
        healthy_devices = sum(1 for state in device_states.values() if state['healthy'])
        total_devices = len(self.devices)
        
        return {
            'server_healthy': len(self.devices) > 0 and healthy_devices > 0,
            'connected_devices': total_devices,
            'healthy_devices': healthy_devices,
            'device_states': device_states,
            'error_stats': self.error_stats,
            'uptime_seconds': time.time() - getattr(self, 'start_time', time.time())
        }
```

### 2. Audio Processing Error Recovery

**Enhanced Network Audio Source**:

```python
# Enhanced voice_assistant/network_audio_source.py additions
class AudioProcessingError(Exception):
    """Specific exception for audio processing failures."""
    pass

class EnhancedNetworkAudioSource(NetworkAudioSource):
    def __init__(self, target_room: Optional[str] = None):
        super().__init__(target_room)
        
        # Error recovery configuration
        self.recovery_config = {
            'max_consecutive_failures': 5,
            'failure_backoff_seconds': [1, 2, 4, 8, 16],
            'audio_quality_threshold': 0.1,  # Minimum audio quality score
            'silence_detection_timeout': 10.0  # Max silence before considering failure
        }
        
        # Error tracking
        self.consecutive_failures = 0
        self.last_successful_recording = time.time()
        self.audio_quality_history = deque(maxlen=10)
    
    def record_audio_with_recovery(self, file_path: str, max_duration: float = 30.0,
                                 silence_timeout: float = 2.0, **kwargs) -> Tuple[bool, Optional[str]]:
        """Record audio with enhanced error recovery."""
        
        for attempt in range(len(self.recovery_config['failure_backoff_seconds'])):
            try:
                # Check if we should attempt recording
                if not self._should_attempt_recording():
                    return False, "Audio source unhealthy - skipping recording"
                
                # Attempt recording
                success, error_message = self.record_audio(
                    file_path, max_duration, silence_timeout, **kwargs
                )
                
                if success:
                    # Validate audio quality
                    if self._validate_audio_quality(file_path):
                        self._record_successful_operation()
                        return True, None
                    else:
                        success = False
                        error_message = "Audio quality below threshold"
                
                # Handle failure
                self._record_failed_operation()
                
                if attempt < len(self.recovery_config['failure_backoff_seconds']) - 1:
                    backoff_time = self.recovery_config['failure_backoff_seconds'][attempt]
                    logging.warning(f"Recording attempt {attempt + 1} failed: {error_message}. "
                                  f"Retrying in {backoff_time} seconds...")
                    
                    # Attempt to recover
                    self._attempt_device_recovery()
                    time.sleep(backoff_time)
                else:
                    logging.error(f"All recording attempts failed. Last error: {error_message}")
                    return False, f"Recording failed after {attempt + 1} attempts: {error_message}"
                    
            except Exception as e:
                logging.error(f"Unexpected error in recording attempt {attempt + 1}: {e}")
                self._record_failed_operation()
                
                if attempt < len(self.recovery_config['failure_backoff_seconds']) - 1:
                    time.sleep(self.recovery_config['failure_backoff_seconds'][attempt])
                else:
                    return False, f"Recording failed with exception: {str(e)}"
        
        return False, "Maximum recovery attempts exceeded"
    
    def _should_attempt_recording(self) -> bool:
        """Determine if recording should be attempted based on recent failures."""
        
        # Don't attempt if too many consecutive failures
        if self.consecutive_failures >= self.recovery_config['max_consecutive_failures']:
            time_since_last_success = time.time() - self.last_successful_recording
            if time_since_last_success < 60.0:  # Wait at least 1 minute
                return False
        
        # Check device availability
        if not self.active_device:
            return False
        
        # Check recent audio quality
        if len(self.audio_quality_history) >= 3:
            recent_avg_quality = sum(self.audio_quality_history) / len(self.audio_quality_history)
            if recent_avg_quality < self.recovery_config['audio_quality_threshold']:
                return False
        
        return True
    
    def _validate_audio_quality(self, file_path: str) -> bool:
        """Validate recorded audio quality."""
        try:
            # Basic audio file validation
            if not os.path.exists(file_path):
                return False
            
            file_size = os.path.getsize(file_path)
            if file_size < 1000:  # Less than 1KB
                logging.warning(f"Audio file too small: {file_size} bytes")
                return False
            
            # Audio content validation using pydub
            from pydub import AudioSegment
            audio = AudioSegment.from_file(file_path)
            
            # Check duration (should have some content)
            duration_ms = len(audio)
            if duration_ms < 500:  # Less than 500ms
                logging.warning(f"Audio too short: {duration_ms}ms")
                return False
            
            # Check for silence (crude check)
            if audio.dBFS < -40:  # Very quiet audio
                logging.warning(f"Audio too quiet: {audio.dBFS} dBFS")
                quality_score = 0.2
            else:
                quality_score = min(1.0, (audio.dBFS + 40) / 20)  # Normalize to 0-1
            
            self.audio_quality_history.append(quality_score)
            logging.debug(f"Audio quality score: {quality_score:.2f}")
            
            return quality_score >= self.recovery_config['audio_quality_threshold']
            
        except Exception as e:
            logging.error(f"Audio quality validation failed: {e}")
            return False
    
    def _record_successful_operation(self):
        """Record successful audio operation."""
        self.consecutive_failures = 0
        self.last_successful_recording = time.time()
        logging.debug("Audio operation successful - reset failure counter")
    
    def _record_failed_operation(self):
        """Record failed audio operation."""
        self.consecutive_failures += 1
        logging.warning(f"Audio operation failed - consecutive failures: {self.consecutive_failures}")
    
    def _attempt_device_recovery(self):
        """Attempt to recover device connection."""
        try:
            if self.active_device:
                logging.info(f"Attempting recovery for device {self.active_device.device_id}")
                
                # Reset device connection
                device_id = self.active_device.device_id
                self.active_device = None
                
                # Wait for device to potentially reconnect
                time.sleep(2.0)
                
                # Try to reselect device
                self._select_active_device()
                
                if self.active_device:
                    logging.info(f"Device recovery successful: {self.active_device.device_id}")
                else:
                    logging.warning("Device recovery failed - no devices available")
        
        except Exception as e:
            logging.error(f"Device recovery attempt failed: {e}")
    
    def get_health_metrics(self) -> Dict[str, Any]:
        """Get comprehensive health metrics."""
        current_time = time.time()
        
        return {
            'device_healthy': self.active_device is not None,
            'consecutive_failures': self.consecutive_failures,
            'time_since_last_success': current_time - self.last_successful_recording,
            'average_audio_quality': (
                sum(self.audio_quality_history) / len(self.audio_quality_history) 
                if self.audio_quality_history else 0.0
            ),
            'active_device_info': (
                self.active_device.device_id if self.active_device else None
            ),
            'recording_state': 'active' if self.is_recording else 'idle',
            'stats': self.get_stats()
        }
```

### 3. ESP32-P4 Client Error Recovery

**Enhanced WebSocket Client Error Handling**:

```c
// components/websocket_client/src/enhanced_websocket_client.c

typedef struct {
    uint32_t connection_failures;
    uint32_t send_failures;
    uint32_t receive_failures;
    uint32_t protocol_errors;
    uint32_t audio_decode_failures;
    uint32_t recovery_attempts;
    uint64_t last_failure_time;
    uint64_t last_successful_operation;
} ws_error_stats_t;

typedef struct {
    bool circuit_open;
    uint32_t failure_count;
    uint64_t last_failure_time;
    uint32_t failure_threshold;
    uint32_t recovery_timeout_ms;
} ws_circuit_breaker_t;

static ws_error_stats_t error_stats = {0};
static ws_circuit_breaker_t circuit_breaker = {
    .failure_threshold = 5,
    .recovery_timeout_ms = 30000
};

esp_err_t ws_client_send_with_recovery(const uint8_t *data, size_t len, 
                                      ws_message_type_t type, uint32_t max_retries) {
    
    // Check circuit breaker
    if (circuit_breaker.circuit_open) {
        uint64_t current_time = esp_timer_get_time() / 1000;
        if (current_time - circuit_breaker.last_failure_time < circuit_breaker.recovery_timeout_ms) {
            ESP_LOGW(TAG, "Circuit breaker open - operation blocked");
            return ESP_ERR_INVALID_STATE;
        } else {
            // Attempt to close circuit breaker
            circuit_breaker.circuit_open = false;
            circuit_breaker.failure_count = 0;
            ESP_LOGI(TAG, "Circuit breaker attempting recovery");
        }
    }
    
    esp_err_t last_error = ESP_OK;
    
    for (uint32_t attempt = 0; attempt < max_retries; attempt++) {
        // Check connection state
        if (s_ws_client.state != WS_CLIENT_STATE_CONNECTED) {
            ESP_LOGW(TAG, "WebSocket not connected - attempting reconnection");
            last_error = attempt_reconnection();
            if (last_error != ESP_OK) {
                error_stats.connection_failures++;
                if (attempt < max_retries - 1) {
                    vTaskDelay(pdMS_TO_TICKS(1000 * (attempt + 1))); // Exponential backoff
                }
                continue;
            }
        }
        
        // Attempt to send message
        switch (type) {
            case WS_MSG_TYPE_AUDIO_STREAM:
                last_error = ws_client_send_binary_audio((const int16_t*)data, len / sizeof(int16_t));
                break;
            case WS_MSG_TYPE_CONTROL:
                last_error = ws_client_send_text((const char*)data);
                break;
            default:
                last_error = ESP_ERR_INVALID_ARG;
                break;
        }
        
        if (last_error == ESP_OK) {
            // Success - record and return
            record_successful_operation();
            return ESP_OK;
        } else {
            // Failure - record and retry
            record_failed_operation(last_error);
            error_stats.send_failures++;
            
            if (attempt < max_retries - 1) {
                ESP_LOGW(TAG, "Send attempt %d failed: %s, retrying...", 
                        attempt + 1, esp_err_to_name(last_error));
                vTaskDelay(pdMS_TO_TICKS(500 * (attempt + 1)));
            }
        }
    }
    
    ESP_LOGE(TAG, "All send attempts failed: %s", esp_err_to_name(last_error));
    return last_error;
}

static esp_err_t attempt_reconnection(void) {
    ESP_LOGI(TAG, "Attempting WebSocket reconnection");
    error_stats.recovery_attempts++;
    
    // Stop current connection
    ws_client_stop();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Restart connection
    esp_err_t ret = ws_client_start();
    if (ret == ESP_OK) {
        // Wait for connection
        uint32_t wait_time = 0;
        const uint32_t max_wait = 10000; // 10 seconds
        
        while (s_ws_client.state != WS_CLIENT_STATE_CONNECTED && wait_time < max_wait) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_time += 100;
        }
        
        if (s_ws_client.state == WS_CLIENT_STATE_CONNECTED) {
            ESP_LOGI(TAG, "Reconnection successful");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Reconnection timeout");
            return ESP_ERR_TIMEOUT;
        }
    }
    
    return ret;
}

static void record_successful_operation(void) {
    error_stats.last_successful_operation = esp_timer_get_time() / 1000;
    
    // Reset circuit breaker
    if (circuit_breaker.circuit_open) {
        circuit_breaker.circuit_open = false;
        circuit_breaker.failure_count = 0;
        ESP_LOGI(TAG, "Circuit breaker closed - service recovered");
    }
}

static void record_failed_operation(esp_err_t error) {
    error_stats.last_failure_time = esp_timer_get_time() / 1000;
    circuit_breaker.failure_count++;
    circuit_breaker.last_failure_time = error_stats.last_failure_time;
    
    // Open circuit breaker if threshold reached
    if (circuit_breaker.failure_count >= circuit_breaker.failure_threshold) {
        circuit_breaker.circuit_open = true;
        ESP_LOGW(TAG, "Circuit breaker opened due to %d failures", circuit_breaker.failure_count);
    }
    
    // Categorize error
    switch (error) {
        case ESP_ERR_INVALID_STATE:
        case ESP_FAIL:
            error_stats.connection_failures++;
            break;
        case ESP_ERR_TIMEOUT:
            error_stats.send_failures++;
            break;
        default:
            error_stats.protocol_errors++;
            break;
    }
}

esp_err_t ws_client_get_health_status(ws_health_status_t *status) {
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t current_time = esp_timer_get_time() / 1000;
    
    status->connection_healthy = (s_ws_client.state == WS_CLIENT_STATE_CONNECTED);
    status->circuit_breaker_open = circuit_breaker.circuit_open;
    status->consecutive_failures = circuit_breaker.failure_count;
    status->time_since_last_success = current_time - error_stats.last_successful_operation;
    status->time_since_last_failure = current_time - error_stats.last_failure_time;
    status->total_errors = error_stats.connection_failures + error_stats.send_failures + 
                          error_stats.receive_failures + error_stats.protocol_errors;
    
    memcpy(&status->error_stats, &error_stats, sizeof(ws_error_stats_t));
    
    return ESP_OK;
}
```

### 4. System Health Monitoring

**Comprehensive Health Check System**:

```python
# voice_assistant/health_monitor.py

import asyncio
import logging
import time
from typing import Dict, List, Any, Optional
from dataclasses import dataclass
from enum import Enum

class HealthStatus(Enum):
    HEALTHY = "healthy"
    DEGRADED = "degraded" 
    UNHEALTHY = "unhealthy"
    CRITICAL = "critical"

@dataclass
class HealthMetric:
    name: str
    status: HealthStatus
    value: Any
    threshold: Any
    message: str
    timestamp: float

class SystemHealthMonitor:
    def __init__(self):
        self.metrics: Dict[str, HealthMetric] = {}
        self.alerts: List[Dict[str, Any]] = []
        self.monitoring_active = False
        
    async def start_monitoring(self, interval_seconds: float = 30.0):
        """Start continuous health monitoring."""
        self.monitoring_active = True
        
        while self.monitoring_active:
            await self.collect_all_metrics()
            await asyncio.sleep(interval_seconds)
    
    def stop_monitoring(self):
        """Stop health monitoring."""
        self.monitoring_active = False
    
    async def collect_all_metrics(self):
        """Collect all system health metrics."""
        current_time = time.time()
        
        # Network connectivity
        await self._check_network_health()
        
        # WebSocket server health
        await self._check_websocket_server_health()
        
        # Audio processing health
        await self._check_audio_processing_health()
        
        # Device connectivity health
        await self._check_device_health()
        
        # System resource health
        await self._check_system_resources()
        
        # Generate alerts for unhealthy systems
        await self._generate_alerts()
        
        logging.debug(f"Health check completed - {len(self.metrics)} metrics collected")
    
    async def _check_websocket_server_health(self):
        """Check WebSocket TTS server health."""
        try:
            from .websocket_tts_server import get_websocket_tts_server
            tts_server = get_websocket_tts_server()
            
            if not tts_server:
                self._record_metric("websocket_server", HealthStatus.CRITICAL, 
                                  False, True, "WebSocket TTS server not running")
                return
            
            # Check server status
            devices = tts_server.get_connected_devices()
            connected_count = len(devices)
            
            if connected_count == 0:
                status = HealthStatus.DEGRADED
                message = "No ESP32-P4 devices connected"
            elif connected_count < 3:  # Assuming multi-device deployment
                status = HealthStatus.HEALTHY
                message = f"{connected_count} devices connected"
            else:
                status = HealthStatus.HEALTHY
                message = f"{connected_count} devices connected - optimal"
            
            self._record_metric("websocket_devices", status, connected_count, 1, message)
            
            # Check server error rates
            if hasattr(tts_server, 'get_health_status'):
                health_status = tts_server.get_health_status()
                error_rate = (health_status.get('error_stats', {}).get('send_failures', 0) / 
                             max(1, health_status.get('connected_devices', 1)))
                
                if error_rate > 0.1:  # >10% error rate
                    status = HealthStatus.UNHEALTHY
                elif error_rate > 0.05:  # >5% error rate
                    status = HealthStatus.DEGRADED
                else:
                    status = HealthStatus.HEALTHY
                
                self._record_metric("websocket_error_rate", status, error_rate, 0.05,
                                  f"Error rate: {error_rate:.2%}")
        
        except Exception as e:
            self._record_metric("websocket_server", HealthStatus.CRITICAL, 
                              str(e), None, f"WebSocket server check failed: {e}")
    
    async def _check_audio_processing_health(self):
        """Check audio processing pipeline health."""
        try:
            from .network_audio_source import get_network_audio_source
            audio_source = get_network_audio_source()
            
            if not audio_source:
                self._record_metric("audio_processing", HealthStatus.DEGRADED,
                                  False, True, "Network audio source not available")
                return
            
            # Check audio source health
            if hasattr(audio_source, 'get_health_metrics'):
                metrics = audio_source.get_health_metrics()
                
                # Device connectivity
                device_healthy = metrics.get('device_healthy', False)
                consecutive_failures = metrics.get('consecutive_failures', 0)
                
                if not device_healthy:
                    status = HealthStatus.UNHEALTHY
                    message = "No active audio device"
                elif consecutive_failures > 5:
                    status = HealthStatus.DEGRADED
                    message = f"{consecutive_failures} consecutive failures"
                else:
                    status = HealthStatus.HEALTHY
                    message = "Audio processing healthy"
                
                self._record_metric("audio_device", status, device_healthy, True, message)
                
                # Audio quality
                avg_quality = metrics.get('average_audio_quality', 0.0)
                if avg_quality < 0.3:
                    status = HealthStatus.UNHEALTHY
                elif avg_quality < 0.6:
                    status = HealthStatus.DEGRADED
                else:
                    status = HealthStatus.HEALTHY
                
                self._record_metric("audio_quality", status, avg_quality, 0.6,
                                  f"Average quality: {avg_quality:.2f}")
        
        except Exception as e:
            self._record_metric("audio_processing", HealthStatus.CRITICAL,
                              str(e), None, f"Audio processing check failed: {e}")
    
    def _record_metric(self, name: str, status: HealthStatus, value: Any, 
                      threshold: Any, message: str):
        """Record a health metric."""
        self.metrics[name] = HealthMetric(
            name=name,
            status=status,
            value=value,
            threshold=threshold,
            message=message,
            timestamp=time.time()
        )
    
    async def _generate_alerts(self):
        """Generate alerts for unhealthy systems."""
        current_time = time.time()
        
        # Clear old alerts (older than 5 minutes)
        self.alerts = [alert for alert in self.alerts 
                      if current_time - alert['timestamp'] < 300]
        
        # Generate new alerts for critical/unhealthy metrics
        for metric_name, metric in self.metrics.items():
            if metric.status in [HealthStatus.CRITICAL, HealthStatus.UNHEALTHY]:
                
                # Check if we already have a recent alert for this metric
                recent_alert = any(
                    alert['metric'] == metric_name and 
                    current_time - alert['timestamp'] < 120  # 2 minutes
                    for alert in self.alerts
                )
                
                if not recent_alert:
                    alert = {
                        'metric': metric_name,
                        'status': metric.status.value,
                        'message': metric.message,
                        'value': metric.value,
                        'timestamp': current_time
                    }
                    self.alerts.append(alert)
                    
                    # Log critical alerts
                    if metric.status == HealthStatus.CRITICAL:
                        logging.error(f"CRITICAL ALERT: {metric_name} - {metric.message}")
                    else:
                        logging.warning(f"HEALTH ALERT: {metric_name} - {metric.message}")
    
    def get_overall_health(self) -> Dict[str, Any]:
        """Get overall system health summary."""
        if not self.metrics:
            return {
                'overall_status': HealthStatus.UNKNOWN.value,
                'message': 'No health metrics collected yet'
            }
        
        # Determine overall status based on individual metrics
        critical_count = sum(1 for m in self.metrics.values() if m.status == HealthStatus.CRITICAL)
        unhealthy_count = sum(1 for m in self.metrics.values() if m.status == HealthStatus.UNHEALTHY)
        degraded_count = sum(1 for m in self.metrics.values() if m.status == HealthStatus.DEGRADED)
        
        if critical_count > 0:
            overall_status = HealthStatus.CRITICAL
            message = f"{critical_count} critical issues detected"
        elif unhealthy_count > 0:
            overall_status = HealthStatus.UNHEALTHY
            message = f"{unhealthy_count} unhealthy systems"
        elif degraded_count > 0:
            overall_status = HealthStatus.DEGRADED
            message = f"{degraded_count} systems degraded"
        else:
            overall_status = HealthStatus.HEALTHY
            message = "All systems healthy"
        
        return {
            'overall_status': overall_status.value,
            'message': message,
            'metrics': {name: {
                'status': metric.status.value,
                'value': metric.value,
                'message': metric.message
            } for name, metric in self.metrics.items()},
            'recent_alerts': self.alerts[-10:],  # Last 10 alerts
            'last_check': max(m.timestamp for m in self.metrics.values()) if self.metrics else None
        }

# Global health monitor instance
_health_monitor: Optional[SystemHealthMonitor] = None

def get_health_monitor() -> SystemHealthMonitor:
    """Get global health monitor instance."""
    global _health_monitor
    if _health_monitor is None:
        _health_monitor = SystemHealthMonitor()
    return _health_monitor
```

## Implementation Summary

### 🚀 Enhanced Error Recovery Features

1. **Circuit Breaker Pattern**: Prevents cascading failures by temporarily blocking operations to failing services
2. **Exponential Backoff**: Intelligent retry logic with increasing delays
3. **Audio Quality Validation**: Ensures recorded audio meets quality thresholds
4. **Device Recovery**: Automatic device reconnection and selection
5. **Health Monitoring**: Comprehensive system health tracking with alerts
6. **Connection Resilience**: Multiple layers of connection failure handling

### ✅ Production Benefits

- **Reduced Downtime**: Automatic recovery from transient failures
- **Improved User Experience**: Graceful handling of errors without user impact
- **Better Diagnostics**: Comprehensive error tracking and health metrics
- **Scalability**: Circuit breaker patterns prevent system overload
- **Maintainability**: Clear error categorization and logging

This enhanced error handling system ensures the ESP32-P4 HowdyScreen and HowdyTTS integration remains robust and reliable in production environments.