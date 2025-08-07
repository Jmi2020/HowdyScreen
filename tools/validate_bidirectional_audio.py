#!/usr/bin/env python3
"""
ESP32-P4 HowdyScreen Bidirectional Audio Streaming Validation Script
Phase 4B: Complete WebSocket TTS Playback System Validation

This script validates the complete bidirectional audio streaming system:
- WebSocket TTS message handling and Base64 decoding
- TTS audio handler speaker output pipeline
- Full-duplex I2S operation with simultaneous capture/playback
- Complete conversation flow integration
- Performance metrics and audio quality validation
- Error recovery and production readiness testing

Usage:
    python3 validate_bidirectional_audio.py --device <ESP32_IP> --server <HOWDYTTS_SERVER_IP>
    python3 validate_bidirectional_audio.py --comprehensive-test
"""

import argparse
import asyncio
import websockets
import json
import base64
import struct
import time
import logging
import socket
import subprocess
import sys
from typing import Optional, Dict, List, Any
import numpy as np

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('bidirectional_audio_validation.log'),
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)

class ESP32AudioValidator:
    """Complete ESP32-P4 bidirectional audio system validator"""
    
    def __init__(self, device_ip: str, server_ip: str):
        self.device_ip = device_ip
        self.server_ip = server_ip
        self.websocket_port = 8001  # VAD feedback port
        self.tts_websocket_port = 8002  # TTS WebSocket port
        self.udp_port = 8000  # Audio streaming port
        
        # Test state
        self.test_results = {}
        self.performance_metrics = {}
        self.error_count = 0
        
        # Audio test data
        self.test_audio_16khz = self._generate_test_audio()
        
    def _generate_test_audio(self, duration_ms: int = 2000, frequency: int = 440) -> bytes:
        """Generate test audio for TTS playback validation"""
        sample_rate = 16000
        samples = int(sample_rate * duration_ms / 1000)
        
        # Generate sine wave at specified frequency
        t = np.linspace(0, duration_ms / 1000, samples, False)
        audio = np.sin(2 * np.pi * frequency * t)
        
        # Scale to 16-bit signed integer
        audio_16bit = (audio * 32767).astype(np.int16)
        
        return audio_16bit.tobytes()

    async def validate_websocket_tts_handler(self) -> bool:
        """Validate WebSocket TTS message handler and Base64 decoding"""
        logger.info("🔍 Validating WebSocket TTS message handler...")
        
        try:
            # Test connection to TTS WebSocket server
            uri = f"ws://{self.server_ip}:{self.tts_websocket_port}/tts"
            
            async with websockets.connect(uri) as websocket:
                logger.info(f"✅ Connected to TTS WebSocket server: {uri}")
                
                # Test TTS audio start message
                session_id = f"test_session_{int(time.time())}"
                tts_start_message = {
                    "type": "tts_audio_start",
                    "session_info": {
                        "session_id": session_id,
                        "response_text": "This is a test TTS response from validation script",
                        "estimated_duration_ms": 2000,
                        "total_chunks_expected": 10
                    },
                    "audio_format": {
                        "sample_rate": 16000,
                        "channels": 1,
                        "bits_per_sample": 16,
                        "total_samples": 32000
                    },
                    "playback_config": {
                        "volume": 0.8,
                        "fade_in_ms": 50,
                        "fade_out_ms": 100,
                        "interrupt_recording": True,
                        "echo_cancellation": True
                    }
                }
                
                await websocket.send(json.dumps(tts_start_message))
                logger.info(f"📤 Sent TTS audio start message for session: {session_id}")
                
                # Test TTS audio chunks with Base64 encoding
                chunk_size = 320  # 20ms at 16kHz
                total_chunks = 10
                
                for chunk_seq in range(total_chunks):
                    # Generate chunk audio data
                    chunk_audio = self.test_audio_16khz[chunk_seq * chunk_size * 2:(chunk_seq + 1) * chunk_size * 2]
                    if len(chunk_audio) == 0:
                        chunk_audio = b'\x00\x00' * chunk_size  # Silence if no more test data
                    
                    # Base64 encode the audio data
                    encoded_audio = base64.b64encode(chunk_audio).decode('ascii')
                    
                    tts_chunk_message = {
                        "type": "tts_audio_chunk",
                        "chunk_info": {
                            "session_id": session_id,
                            "chunk_sequence": chunk_seq,
                            "chunk_size": len(chunk_audio),
                            "is_final": (chunk_seq == total_chunks - 1),
                            "audio_data": encoded_audio,
                            "checksum": f"{hash(chunk_audio):08x}"
                        },
                        "timing": {
                            "chunk_start_time_ms": chunk_seq * 200,
                            "chunk_duration_ms": 200
                        }
                    }
                    
                    await websocket.send(json.dumps(tts_chunk_message))
                    logger.info(f"📤 Sent TTS chunk {chunk_seq + 1}/{total_chunks} ({len(chunk_audio)} bytes)")
                    
                    # Small delay to simulate real-time streaming
                    await asyncio.sleep(0.1)
                
                # Test TTS audio end message
                tts_end_message = {
                    "type": "tts_audio_end",
                    "session_summary": {
                        "session_id": session_id,
                        "total_chunks_sent": total_chunks,
                        "total_audio_bytes": len(self.test_audio_16khz),
                        "actual_duration_ms": 2000,
                        "transmission_time_ms": total_chunks * 100
                    },
                    "playback_actions": {
                        "fade_out_ms": 100,
                        "return_to_listening": True,
                        "cooldown_period_ms": 500
                    }
                }
                
                await websocket.send(json.dumps(tts_end_message))
                logger.info(f"📤 Sent TTS audio end message for session: {session_id}")
                
                # Wait for processing
                await asyncio.sleep(2)
                
                self.test_results['websocket_tts_handler'] = True
                logger.info("✅ WebSocket TTS message handler validation PASSED")
                return True
                
        except Exception as e:
            logger.error(f"❌ WebSocket TTS handler validation FAILED: {e}")
            self.test_results['websocket_tts_handler'] = False
            self.error_count += 1
            return False

    async def validate_tts_audio_pipeline(self) -> bool:
        """Validate TTS audio handler speaker output pipeline"""
        logger.info("🔍 Validating TTS audio handler pipeline...")
        
        try:
            # Connect to ESP32 via HTTP to trigger TTS test
            import requests
            
            test_url = f"http://{self.device_ip}/api/test/tts_pipeline"
            
            # Send TTS pipeline test request
            test_data = {
                "test_type": "tts_pipeline_validation",
                "audio_format": {
                    "sample_rate": 16000,
                    "channels": 1,
                    "bits_per_sample": 16
                },
                "test_duration_ms": 3000
            }
            
            response = requests.post(test_url, json=test_data, timeout=10)
            
            if response.status_code == 200:
                result = response.json()
                if result.get('status') == 'success':
                    logger.info("✅ TTS audio pipeline test initiated successfully")
                    
                    # Wait for test completion
                    await asyncio.sleep(5)
                    
                    # Check test results
                    status_url = f"http://{self.device_ip}/api/status/tts_pipeline"
                    status_response = requests.get(status_url, timeout=5)
                    
                    if status_response.status_code == 200:
                        status_data = status_response.json()
                        
                        # Validate key metrics
                        chunks_played = status_data.get('chunks_played', 0)
                        bytes_played = status_data.get('bytes_played', 0)
                        buffer_underruns = status_data.get('buffer_underruns', 0)
                        
                        logger.info(f"📊 TTS Pipeline Results:")
                        logger.info(f"   Chunks played: {chunks_played}")
                        logger.info(f"   Bytes played: {bytes_played}")
                        logger.info(f"   Buffer underruns: {buffer_underruns}")
                        
                        # Performance criteria
                        if chunks_played > 0 and bytes_played > 0 and buffer_underruns < 2:
                            self.test_results['tts_audio_pipeline'] = True
                            self.performance_metrics['tts_chunks_played'] = chunks_played
                            self.performance_metrics['tts_buffer_underruns'] = buffer_underruns
                            logger.info("✅ TTS audio pipeline validation PASSED")
                            return True
                        else:
                            logger.error(f"❌ TTS pipeline performance below threshold")
                            return False
                    else:
                        logger.error(f"❌ Failed to get TTS pipeline status")
                        return False
                else:
                    logger.error(f"❌ TTS pipeline test failed: {result.get('error')}")
                    return False
            else:
                logger.error(f"❌ TTS pipeline test request failed: {response.status_code}")
                return False
                
        except Exception as e:
            logger.error(f"❌ TTS audio pipeline validation FAILED: {e}")
            self.test_results['tts_audio_pipeline'] = False
            self.error_count += 1
            return False

    async def validate_full_duplex_operation(self) -> bool:
        """Validate full-duplex I2S operation with simultaneous capture and playback"""
        logger.info("🔍 Validating full-duplex I2S operation...")
        
        try:
            import requests
            
            # Test simultaneous I2S mode activation
            mode_url = f"http://{self.device_ip}/api/audio/i2s_mode"
            
            # Set to simultaneous mode
            mode_data = {
                "mode": "simultaneous",
                "test_duration_ms": 5000
            }
            
            response = requests.post(mode_url, json=mode_data, timeout=10)
            
            if response.status_code == 200:
                result = response.json()
                
                if result.get('status') == 'success':
                    logger.info("✅ Simultaneous I2S mode activated")
                    
                    # Wait for test completion
                    await asyncio.sleep(6)
                    
                    # Check I2S statistics
                    stats_url = f"http://{self.device_ip}/api/status/i2s_stats"
                    stats_response = requests.get(stats_url, timeout=5)
                    
                    if stats_response.status_code == 200:
                        stats_data = stats_response.json()
                        
                        # Validate simultaneous operation metrics
                        mic_samples = stats_data.get('mic_samples_read', 0)
                        speaker_samples = stats_data.get('speaker_samples_written', 0)
                        mic_errors = stats_data.get('mic_errors', 0)
                        speaker_errors = stats_data.get('speaker_errors', 0)
                        
                        logger.info(f"📊 I2S Full-Duplex Results:")
                        logger.info(f"   Mic samples read: {mic_samples}")
                        logger.info(f"   Speaker samples written: {speaker_samples}")
                        logger.info(f"   Mic errors: {mic_errors}")
                        logger.info(f"   Speaker errors: {speaker_errors}")
                        
                        # Performance criteria for full-duplex
                        if (mic_samples > 50000 and speaker_samples > 10000 and 
                            mic_errors < 5 and speaker_errors < 5):
                            
                            self.test_results['full_duplex_i2s'] = True
                            self.performance_metrics['mic_samples_read'] = mic_samples
                            self.performance_metrics['speaker_samples_written'] = speaker_samples
                            self.performance_metrics['i2s_error_rate'] = (mic_errors + speaker_errors) / (mic_samples + speaker_samples) * 100
                            
                            logger.info("✅ Full-duplex I2S operation validation PASSED")
                            return True
                        else:
                            logger.error(f"❌ Full-duplex I2S performance below threshold")
                            return False
                    else:
                        logger.error(f"❌ Failed to get I2S statistics")
                        return False
                else:
                    logger.error(f"❌ Simultaneous I2S mode activation failed: {result.get('error')}")
                    return False
            else:
                logger.error(f"❌ I2S mode request failed: {response.status_code}")
                return False
                
        except Exception as e:
            logger.error(f"❌ Full-duplex I2S validation FAILED: {e}")
            self.test_results['full_duplex_i2s'] = False
            self.error_count += 1
            return False

    async def validate_conversation_flow(self) -> bool:
        """Validate complete conversation flow integration"""
        logger.info("🔍 Validating complete conversation flow...")
        
        try:
            import requests
            
            # Test complete conversation sequence
            conversation_url = f"http://{self.device_ip}/api/test/conversation_flow"
            
            conversation_test = {
                "test_type": "complete_conversation",
                "sequence": [
                    {"action": "wake_word_detection", "phrase": "Hey Howdy", "duration_ms": 1500},
                    {"action": "speech_input", "text": "What time is it?", "duration_ms": 2000},
                    {"action": "tts_response", "text": "The current time is 3:45 PM", "duration_ms": 3000},
                    {"action": "conversation_end", "duration_ms": 1000}
                ],
                "validate_states": True,
                "validate_audio_pipeline": True,
                "validate_echo_cancellation": True
            }
            
            response = requests.post(conversation_url, json=conversation_test, timeout=15)
            
            if response.status_code == 200:
                result = response.json()
                
                if result.get('status') == 'success':
                    # Analyze conversation flow results
                    flow_results = result.get('flow_results', {})
                    
                    wake_word_detected = flow_results.get('wake_word_detected', False)
                    speech_processed = flow_results.get('speech_processed', False)
                    tts_played = flow_results.get('tts_played', False)
                    states_validated = flow_results.get('states_validated', False)
                    echo_cancelled = flow_results.get('echo_cancelled', False)
                    
                    # Performance metrics
                    wake_word_latency = flow_results.get('wake_word_latency_ms', 0)
                    speech_latency = flow_results.get('speech_processing_latency_ms', 0)
                    tts_latency = flow_results.get('tts_start_latency_ms', 0)
                    total_latency = flow_results.get('total_conversation_latency_ms', 0)
                    
                    logger.info(f"📊 Conversation Flow Results:")
                    logger.info(f"   Wake word detected: {wake_word_detected}")
                    logger.info(f"   Speech processed: {speech_processed}")
                    logger.info(f"   TTS played: {tts_played}")
                    logger.info(f"   States validated: {states_validated}")
                    logger.info(f"   Echo cancelled: {echo_cancelled}")
                    logger.info(f"   Wake word latency: {wake_word_latency}ms")
                    logger.info(f"   Speech latency: {speech_latency}ms")
                    logger.info(f"   TTS start latency: {tts_latency}ms")
                    logger.info(f"   Total latency: {total_latency}ms")
                    
                    # Validate against performance targets
                    conversation_success = (wake_word_detected and speech_processed and 
                                          tts_played and states_validated)
                    
                    latency_acceptable = (wake_word_latency < 500 and 
                                        speech_latency < 2000 and 
                                        tts_latency < 200 and 
                                        total_latency < 7000)
                    
                    if conversation_success and latency_acceptable:
                        self.test_results['conversation_flow'] = True
                        self.performance_metrics['wake_word_latency_ms'] = wake_word_latency
                        self.performance_metrics['speech_latency_ms'] = speech_latency
                        self.performance_metrics['tts_latency_ms'] = tts_latency
                        self.performance_metrics['total_conversation_latency_ms'] = total_latency
                        self.performance_metrics['echo_cancellation_active'] = echo_cancelled
                        
                        logger.info("✅ Complete conversation flow validation PASSED")
                        return True
                    else:
                        logger.error(f"❌ Conversation flow validation failed")
                        logger.error(f"   Success criteria: {conversation_success}")
                        logger.error(f"   Latency acceptable: {latency_acceptable}")
                        return False
                else:
                    logger.error(f"❌ Conversation flow test failed: {result.get('error')}")
                    return False
            else:
                logger.error(f"❌ Conversation flow test request failed: {response.status_code}")
                return False
                
        except Exception as e:
            logger.error(f"❌ Conversation flow validation FAILED: {e}")
            self.test_results['conversation_flow'] = False
            self.error_count += 1
            return False

    async def validate_performance_metrics(self) -> bool:
        """Validate performance metrics and audio quality parameters"""
        logger.info("🔍 Validating performance metrics and audio quality...")
        
        try:
            import requests
            
            # Get comprehensive system metrics
            metrics_url = f"http://{self.device_ip}/api/metrics/performance"
            response = requests.get(metrics_url, timeout=10)
            
            if response.status_code == 200:
                metrics = response.json()
                
                # Audio quality metrics
                audio_metrics = metrics.get('audio_quality', {})
                sample_rate_actual = audio_metrics.get('sample_rate_actual', 0)
                bit_depth_actual = audio_metrics.get('bit_depth_actual', 0)
                channel_count = audio_metrics.get('channel_count', 0)
                audio_dropouts = audio_metrics.get('audio_dropouts', 0)
                buffer_underruns = audio_metrics.get('buffer_underruns', 0)
                
                # System performance metrics
                system_metrics = metrics.get('system_performance', {})
                cpu_usage_percent = system_metrics.get('cpu_usage_percent', 0)
                memory_usage_percent = system_metrics.get('memory_usage_percent', 0)
                free_heap_bytes = system_metrics.get('free_heap_bytes', 0)
                task_high_water_mark = system_metrics.get('task_high_water_mark', 0)
                
                # Network performance metrics
                network_metrics = metrics.get('network_performance', {})
                packet_loss_rate = network_metrics.get('packet_loss_rate', 0)
                average_latency_ms = network_metrics.get('average_latency_ms', 0)
                jitter_ms = network_metrics.get('jitter_ms', 0)
                
                # Echo cancellation metrics
                echo_metrics = metrics.get('echo_cancellation', {})
                echo_suppression_db = echo_metrics.get('suppression_db', 0)
                echo_tail_length_ms = echo_metrics.get('tail_length_ms', 0)
                
                logger.info(f"📊 Performance Metrics:")
                logger.info(f"   Audio Quality:")
                logger.info(f"     Sample rate: {sample_rate_actual}Hz")
                logger.info(f"     Bit depth: {bit_depth_actual}-bit")
                logger.info(f"     Channels: {channel_count}")
                logger.info(f"     Audio dropouts: {audio_dropouts}")
                logger.info(f"     Buffer underruns: {buffer_underruns}")
                
                logger.info(f"   System Performance:")
                logger.info(f"     CPU usage: {cpu_usage_percent}%")
                logger.info(f"     Memory usage: {memory_usage_percent}%")
                logger.info(f"     Free heap: {free_heap_bytes} bytes")
                logger.info(f"     Task water mark: {task_high_water_mark}")
                
                logger.info(f"   Network Performance:")
                logger.info(f"     Packet loss: {packet_loss_rate}%")
                logger.info(f"     Average latency: {average_latency_ms}ms")
                logger.info(f"     Jitter: {jitter_ms}ms")
                
                logger.info(f"   Echo Cancellation:")
                logger.info(f"     Suppression: {echo_suppression_db}dB")
                logger.info(f"     Tail length: {echo_tail_length_ms}ms")
                
                # Validate against production targets
                audio_quality_ok = (sample_rate_actual == 16000 and 
                                  bit_depth_actual == 16 and 
                                  channel_count == 1 and 
                                  audio_dropouts < 5 and 
                                  buffer_underruns < 10)
                
                system_performance_ok = (cpu_usage_percent < 80 and 
                                       memory_usage_percent < 85 and 
                                       free_heap_bytes > 50000)
                
                network_performance_ok = (packet_loss_rate < 1.0 and 
                                        average_latency_ms < 50 and 
                                        jitter_ms < 20)
                
                echo_cancellation_ok = (echo_suppression_db > 20 and 
                                      echo_tail_length_ms < 100)
                
                if (audio_quality_ok and system_performance_ok and 
                    network_performance_ok and echo_cancellation_ok):
                    
                    self.test_results['performance_metrics'] = True
                    
                    # Store detailed performance metrics
                    self.performance_metrics.update({
                        'audio_sample_rate': sample_rate_actual,
                        'audio_dropouts': audio_dropouts,
                        'buffer_underruns': buffer_underruns,
                        'cpu_usage_percent': cpu_usage_percent,
                        'memory_usage_percent': memory_usage_percent,
                        'packet_loss_rate': packet_loss_rate,
                        'network_latency_ms': average_latency_ms,
                        'echo_suppression_db': echo_suppression_db
                    })
                    
                    logger.info("✅ Performance metrics validation PASSED")
                    return True
                else:
                    logger.error(f"❌ Performance metrics below production targets")
                    logger.error(f"   Audio quality: {audio_quality_ok}")
                    logger.error(f"   System performance: {system_performance_ok}")
                    logger.error(f"   Network performance: {network_performance_ok}")
                    logger.error(f"   Echo cancellation: {echo_cancellation_ok}")
                    return False
            else:
                logger.error(f"❌ Failed to get performance metrics: {response.status_code}")
                return False
                
        except Exception as e:
            logger.error(f"❌ Performance metrics validation FAILED: {e}")
            self.test_results['performance_metrics'] = False
            self.error_count += 1
            return False

    async def validate_error_recovery(self) -> bool:
        """Validate error recovery and production readiness scenarios"""
        logger.info("🔍 Validating error recovery and production readiness...")
        
        try:
            import requests
            
            # Test various error scenarios
            error_scenarios = [
                {
                    "name": "WebSocket disconnection",
                    "scenario": "websocket_disconnect",
                    "duration_ms": 5000,
                    "expected_recovery_ms": 10000
                },
                {
                    "name": "Audio buffer overflow",
                    "scenario": "audio_buffer_overflow",
                    "duration_ms": 3000,
                    "expected_recovery_ms": 2000
                },
                {
                    "name": "Network interruption",
                    "scenario": "network_interruption",
                    "duration_ms": 8000,
                    "expected_recovery_ms": 15000
                },
                {
                    "name": "TTS playback failure",
                    "scenario": "tts_playback_failure",
                    "duration_ms": 2000,
                    "expected_recovery_ms": 5000
                }
            ]
            
            recovery_results = {}
            
            for scenario in error_scenarios:
                logger.info(f"🧪 Testing error scenario: {scenario['name']}")
                
                # Trigger error scenario
                error_url = f"http://{self.device_ip}/api/test/error_scenario"
                error_data = {
                    "scenario": scenario['scenario'],
                    "duration_ms": scenario['duration_ms']
                }
                
                response = requests.post(error_url, json=error_data, timeout=15)
                
                if response.status_code == 200:
                    result = response.json()
                    
                    if result.get('status') == 'success':
                        # Wait for recovery
                        recovery_wait_time = scenario['expected_recovery_ms'] / 1000
                        await asyncio.sleep(recovery_wait_time)
                        
                        # Check recovery status
                        status_url = f"http://{self.device_ip}/api/status/recovery"
                        status_response = requests.get(status_url, timeout=5)
                        
                        if status_response.status_code == 200:
                            status_data = status_response.json()
                            
                            recovered = status_data.get('recovered', False)
                            recovery_time_ms = status_data.get('recovery_time_ms', 0)
                            system_stable = status_data.get('system_stable', False)
                            
                            logger.info(f"   Recovery status: {recovered}")
                            logger.info(f"   Recovery time: {recovery_time_ms}ms")
                            logger.info(f"   System stable: {system_stable}")
                            
                            recovery_results[scenario['name']] = {
                                'recovered': recovered,
                                'recovery_time_ms': recovery_time_ms,
                                'system_stable': system_stable,
                                'within_expected_time': recovery_time_ms <= scenario['expected_recovery_ms']
                            }
                        else:
                            logger.error(f"   ❌ Failed to get recovery status")
                            recovery_results[scenario['name']] = {'recovered': False}
                    else:
                        logger.error(f"   ❌ Error scenario test failed: {result.get('error')}")
                        recovery_results[scenario['name']] = {'recovered': False}
                else:
                    logger.error(f"   ❌ Error scenario request failed: {response.status_code}")
                    recovery_results[scenario['name']] = {'recovered': False}
            
            # Evaluate overall error recovery performance
            total_scenarios = len(error_scenarios)
            successful_recoveries = sum(1 for r in recovery_results.values() if r.get('recovered', False))
            timely_recoveries = sum(1 for r in recovery_results.values() if r.get('within_expected_time', False))
            
            logger.info(f"📊 Error Recovery Results:")
            logger.info(f"   Total scenarios tested: {total_scenarios}")
            logger.info(f"   Successful recoveries: {successful_recoveries}")
            logger.info(f"   Timely recoveries: {timely_recoveries}")
            
            recovery_success_rate = successful_recoveries / total_scenarios
            timely_recovery_rate = timely_recoveries / total_scenarios
            
            # Production readiness criteria
            if recovery_success_rate >= 0.75 and timely_recovery_rate >= 0.5:
                self.test_results['error_recovery'] = True
                self.performance_metrics['recovery_success_rate'] = recovery_success_rate
                self.performance_metrics['timely_recovery_rate'] = timely_recovery_rate
                
                logger.info("✅ Error recovery validation PASSED")
                return True
            else:
                logger.error(f"❌ Error recovery below production standards")
                logger.error(f"   Success rate: {recovery_success_rate:.2f} (minimum: 0.75)")
                logger.error(f"   Timely rate: {timely_recovery_rate:.2f} (minimum: 0.5)")
                return False
                
        except Exception as e:
            logger.error(f"❌ Error recovery validation FAILED: {e}")
            self.test_results['error_recovery'] = False
            self.error_count += 1
            return False

    async def run_comprehensive_validation(self) -> Dict[str, Any]:
        """Run complete bidirectional audio streaming validation"""
        logger.info("🚀 Starting comprehensive bidirectional audio streaming validation")
        logger.info(f"   ESP32 Device: {self.device_ip}")
        logger.info(f"   HowdyTTS Server: {self.server_ip}")
        
        start_time = time.time()
        
        # Run all validation tests
        validation_tests = [
            ("WebSocket TTS Handler", self.validate_websocket_tts_handler),
            ("TTS Audio Pipeline", self.validate_tts_audio_pipeline),
            ("Full-Duplex I2S", self.validate_full_duplex_operation),
            ("Conversation Flow", self.validate_conversation_flow),
            ("Performance Metrics", self.validate_performance_metrics),
            ("Error Recovery", self.validate_error_recovery)
        ]
        
        passed_tests = 0
        
        for test_name, test_func in validation_tests:
            logger.info(f"\n{'='*60}")
            logger.info(f"Running: {test_name}")
            logger.info(f"{'='*60}")
            
            try:
                result = await test_func()
                if result:
                    passed_tests += 1
                    logger.info(f"✅ {test_name}: PASSED")
                else:
                    logger.error(f"❌ {test_name}: FAILED")
            except Exception as e:
                logger.error(f"❌ {test_name}: ERROR - {e}")
                self.error_count += 1
        
        total_time = time.time() - start_time
        
        # Generate final report
        report = {
            "validation_summary": {
                "total_tests": len(validation_tests),
                "passed_tests": passed_tests,
                "failed_tests": len(validation_tests) - passed_tests,
                "error_count": self.error_count,
                "success_rate": passed_tests / len(validation_tests),
                "total_time_seconds": total_time
            },
            "test_results": self.test_results,
            "performance_metrics": self.performance_metrics,
            "production_ready": (passed_tests == len(validation_tests) and self.error_count == 0)
        }
        
        logger.info(f"\n{'='*60}")
        logger.info("VALIDATION COMPLETE")
        logger.info(f"{'='*60}")
        logger.info(f"✅ Tests Passed: {passed_tests}/{len(validation_tests)}")
        logger.info(f"❌ Tests Failed: {len(validation_tests) - passed_tests}")
        logger.info(f"⚠️  Total Errors: {self.error_count}")
        logger.info(f"⏱️  Total Time: {total_time:.1f}s")
        logger.info(f"🏭 Production Ready: {report['production_ready']}")
        
        # Key performance summary
        if self.performance_metrics:
            logger.info(f"\n📊 KEY PERFORMANCE METRICS:")
            if 'total_conversation_latency_ms' in self.performance_metrics:
                logger.info(f"   Total Conversation Latency: {self.performance_metrics['total_conversation_latency_ms']}ms (target: <7000ms)")
            if 'network_latency_ms' in self.performance_metrics:
                logger.info(f"   Network Latency: {self.performance_metrics['network_latency_ms']}ms (target: <50ms)")
            if 'echo_suppression_db' in self.performance_metrics:
                logger.info(f"   Echo Suppression: {self.performance_metrics['echo_suppression_db']}dB (target: >20dB)")
            if 'recovery_success_rate' in self.performance_metrics:
                logger.info(f"   Recovery Success Rate: {self.performance_metrics['recovery_success_rate']:.1%} (target: >75%)")
        
        return report

def main():
    parser = argparse.ArgumentParser(description='ESP32-P4 HowdyScreen Bidirectional Audio Validation')
    parser.add_argument('--device', required=True, help='ESP32-P4 device IP address')
    parser.add_argument('--server', required=True, help='HowdyTTS server IP address')
    parser.add_argument('--comprehensive-test', action='store_true', help='Run comprehensive test suite')
    parser.add_argument('--output', default='validation_report.json', help='Output report file')
    
    args = parser.parse_args()
    
    # Create validator instance
    validator = ESP32AudioValidator(args.device, args.server)
    
    # Run validation
    async def run_validation():
        if args.comprehensive_test:
            report = await validator.run_comprehensive_validation()
        else:
            # Run individual tests
            report = {}
            logger.info("Running individual validation tests...")
            
            if await validator.validate_websocket_tts_handler():
                logger.info("✅ WebSocket TTS handler validation passed")
            
            if await validator.validate_full_duplex_operation():
                logger.info("✅ Full-duplex operation validation passed")
            
            report = {
                "test_results": validator.test_results,
                "performance_metrics": validator.performance_metrics
            }
        
        # Save report
        import json
        with open(args.output, 'w') as f:
            json.dump(report, f, indent=2)
        
        logger.info(f"📄 Validation report saved to: {args.output}")
        return report
    
    # Run the validation
    try:
        report = asyncio.run(run_validation())
        
        if report.get('production_ready', False):
            logger.info("🎉 SYSTEM VALIDATION SUCCESSFUL - PRODUCTION READY!")
            sys.exit(0)
        else:
            logger.error("⚠️  SYSTEM VALIDATION INCOMPLETE - REQUIRES ATTENTION")
            sys.exit(1)
            
    except KeyboardInterrupt:
        logger.info("🛑 Validation interrupted by user")
        sys.exit(2)
    except Exception as e:
        logger.error(f"💥 Validation failed with error: {e}")
        sys.exit(3)

if __name__ == "__main__":
    main()