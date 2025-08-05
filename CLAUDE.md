# ESP32-P4 HowdyScreen Project Configuration

## Project Analysis Summary

**Technology Stack Detected:**
- **Platform**: ESP32-P4 dual-core RISC-V microcontroller
- **Framework**: ESP-IDF v5.3+ (Espressif IoT Development Framework)
- **Build System**: CMake with ESP-IDF component system
- **UI Framework**: LVGL v8.3+ for graphics and touch interface
- **Audio**: I2S with ESP codec components (ES8311/ES7210)
- **Connectivity**: ESP32-C6 co-processor via SDIO for WiFi 6
- **Display**: MIPI-DSI 800x800 round LCD with CST9217 touch controller
- **Communication**: WebSocket, mDNS, Opus audio encoding

**Key Project Areas:**
1. **Embedded Systems Architecture** - Dual-chip ESP32-P4 + ESP32-C6 design
2. **Real-time Audio Processing** - I2S capture, codec integration, Opus encoding
3. **Graphics & UI Development** - LVGL-based touch interface with state management
4. **Hardware Drivers** - Display, touch, audio codec, SDIO communication
5. **Network Communications** - WiFi remote, WebSocket, mDNS service discovery
6. **System Integration** - FreeRTOS task coordination, memory management

---

## AI Development Team Configuration
*Optimized by team-configurator on 2025-08-04*

### Core Development Specialists

#### **Embedded Systems Architecture & Component Integration**
- **Primary**: @code-archaeologist
  - ESP-IDF component dependency analysis and CMakeLists.txt optimization
  - Hardware driver exploration and BSP component deep-dive
  - Dual-chip architecture mapping (ESP32-P4 + ESP32-C6 SDIO)
  - Component interface design and inter-component communication patterns
  - IDF component manager manifest analysis and dependency resolution

#### **Real-time Audio & Performance Engineering**
- **Primary**: @performance-optimizer
  - I2S audio pipeline optimization with ES8311/ES7210 codec integration
  - FreeRTOS task coordination, priority tuning, and memory management
  - DMA buffer sizing and audio latency reduction (target: <50ms)
  - Opus encoding optimization for real-time streaming
  - System resource profiling and power consumption analysis

#### **LVGL UI & Round Display Specialist**
- **Primary**: @frontend-developer
  - LVGL 8.3+ circular UI design for 800x800 round display
  - CST9217 touch controller integration and gesture recognition
  - Voice assistant state visualization and feedback animations
  - MIPI-DSI display optimization and framebuffer management
  - Touch calibration and multi-touch handling for round screen

#### **Network Protocol & WebSocket Specialist**
- **Primary**: @api-architect
  - HowdyTTS WebSocket protocol design and audio streaming optimization
  - ESP32-C6 WiFi remote configuration and 6GHz/2.4GHz handling
  - mDNS service discovery with failover and connection recovery
  - Opus audio codec integration with WebSocket frames
  - Network buffer management and real-time streaming protocols

#### **Hardware Integration & BSP Developer**
- **Primary**: @backend-developer
  - Board Support Package (BSP) development and hardware abstraction
  - ESP32-P4 peripheral configuration (I2S, SPI, MIPI-DSI, SDIO)
  - Inter-processor communication between ESP32-P4 and ESP32-C6
  - Component-based architecture implementation and CMake integration
  - Hardware diagnostic tools and initialization sequence optimization

### Quality & Documentation Specialists

#### **Embedded Code Quality & Safety**
- **Primary**: @code-reviewer
  - ESP-IDF coding standards and FreeRTOS safety compliance
  - Real-time constraints validation (ISR safety, task priorities)
  - Hardware driver code review (register access, timing critical sections)
  - Component integration testing and memory leak detection
  - Thread safety analysis for dual-core RISC-V architecture

#### **Technical Documentation & Debugging**
- **Primary**: @documentation-specialist
  - Hardware setup guides for ESP32-P4-WIFI6-Touch-LCD-3.4C board
  - Component API documentation and integration examples
  - ESP-IDF debugging guides and JTAG configuration
  - Architecture decision records (ADRs) for dual-chip design
  - Performance benchmarking and optimization reports

#### **Project Architecture & Build System**
- **Primary**: @project-analyst
  - ESP-IDF component dependency resolution and CMake optimization
  - Build system configuration for dual-chip architecture
  - Memory usage analysis and flash/RAM optimization
  - Component refactoring recommendations for modular design
  - CI/CD pipeline setup for ESP-IDF projects

---

## How to Use Your Embedded Systems AI Team

### **Audio Pipeline Development**
```
"@performance-optimizer optimize the I2S audio capture pipeline for low latency"
"@backend-developer integrate the ES8311 codec with the audio processor component"
```

### **UI & Display Work**
```
"@frontend-developer create a circular audio level meter using LVGL"
"@frontend-developer implement touch gesture handling for the round display"
```

### **Network & Communication**
```
"@api-architect design the WebSocket protocol for HowdyTTS audio streaming"
"@api-architect implement mDNS discovery for automatic server detection"
```

### **Hardware Integration**
```
"@backend-developer configure the ESP32-C6 SDIO interface for WiFi remote"
"@code-archaeologist analyze the BSP component structure for display initialization"
```

### **Code Quality & Reviews**
```
"@code-reviewer check this audio pipeline code for real-time safety"
"@performance-optimizer profile memory usage in the display framebuffer"
```

### **Documentation & Analysis**
```
"@documentation-specialist create a hardware setup guide for ESP32-P4 connections"
"@project-analyst analyze component dependencies for potential conflicts"
```

---

## Optimized AI Agent Workflows for ESP32-P4 Development

### **Component-Based Audio Pipeline Development**
1. **@code-archaeologist** - Explore `components/audio_processor/` structure and I2S driver integration
2. **@performance-optimizer** - Profile audio latency and optimize DMA buffers for ES8311/ES7210
3. **@backend-developer** - Implement codec initialization and audio task coordination
4. **@api-architect** - Design audio streaming protocol with Opus encoding
5. **@code-reviewer** - Validate real-time safety and memory management

### **Round Display UI & Touch System**
1. **@frontend-developer** - Create LVGL circular widgets for voice assistant states
2. **@backend-developer** - Integrate CST9217 touch controller with `components/ui_manager/`
3. **@performance-optimizer** - Optimize MIPI-DSI refresh rates and framebuffer usage
4. **@project-analyst** - Analyze UI component dependencies and memory footprint
5. **@code-reviewer** - Review touch event handling and UI thread safety

### **Dual-Chip Network Architecture**
1. **@api-architect** - Design WebSocket protocol in `components/websocket_client/`
2. **@code-archaeologist** - Analyze ESP32-C6 SDIO communication patterns
3. **@backend-developer** - Configure WiFi remote with `esp_wifi_remote` component
4. **@performance-optimizer** - Optimize network buffer sizes and connection handling
5. **@code-reviewer** - Validate network error recovery and failover logic

### **BSP & Hardware Integration**
1. **@backend-developer** - Develop `components/bsp/` for hardware abstraction layer
2. **@code-archaeologist** - Map peripheral configurations (I2S, SPI, MIPI-DSI)
3. **@project-analyst** - Analyze hardware dependencies and component interfaces
4. **@performance-optimizer** - Optimize initialization sequences and power management
5. **@documentation-specialist** - Create hardware setup and pinout documentation

### **Service Discovery & mDNS Integration**
1. **@api-architect** - Implement mDNS in `components/service_discovery/`
2. **@backend-developer** - Integrate HowdyTTS server auto-discovery
3. **@performance-optimizer** - Optimize network scanning and connection establishment
4. **@code-reviewer** - Review service discovery timeout and error handling

---

## Optimized Commands for ESP32-P4 Development

### Component Development Tasks
- **Audio System**: "@performance-optimizer optimize components/audio_processor I2S pipeline for <50ms latency"
- **Round Display**: "@frontend-developer create circular LVGL widgets in components/ui_manager for voice states"
- **WebSocket Client**: "@api-architect implement HowdyTTS streaming protocol in components/websocket_client"
- **BSP Integration**: "@backend-developer develop hardware abstraction layer in components/bsp"
- **Service Discovery**: "@api-architect add HowdyTTS auto-discovery to components/service_discovery"

### Hardware & Performance Analysis
- **Driver Deep-dive**: "@code-archaeologist analyze CST9217 touch integration with MIPI-DSI display"
- **Memory Profiling**: "@performance-optimizer profile PSRAM usage during audio streaming and UI updates"
- **SDIO Analysis**: "@code-archaeologist explore ESP32-C6 SDIO communication in esp_wifi_remote"
- **Task Coordination**: "@performance-optimizer balance FreeRTOS priorities for audio/UI/network tasks"
- **Power Optimization**: "@backend-developer implement power management for dual-chip architecture"

### Integration & Quality Assurance
- **Component Dependencies**: "@project-analyst resolve CMakeLists.txt conflicts between components"
- **Real-time Safety**: "@code-reviewer validate ISR safety in audio pipeline and touch handlers"
- **Network Resilience**: "@api-architect test WebSocket reconnection during WiFi handoff"
- **Hardware Diagnostic**: "@backend-developer create diagnostic tools for ESP32-P4 peripheral status"

### Complex Integration Workflows
- **Complete Audio Chain**: 
  - "@code-archaeologist map audio flow from I2S to WebSocket"
  - "@performance-optimizer optimize end-to-end audio latency"
  - "@api-architect integrate Opus encoding with streaming protocol"

- **Touch-to-UI Pipeline**:
  - "@backend-developer integrate CST9217 touch events with LVGL"
  - "@frontend-developer create responsive circular UI for touch gestures"
  - "@performance-optimizer optimize touch response time on round display"

- **Network-to-Audio Integration**:
  - "@api-architect design bidirectional audio streaming over WebSocket"
  - "@backend-developer coordinate ESP32-C6 WiFi with ESP32-P4 audio processing"
  - "@performance-optimizer minimize audio buffer underruns during network fluctuations"

---

---

## Key Optimizations Made

### 1. **Component-Focused Agent Specialization**
- Agents now specifically target your 5 core components: `audio_processor`, `bsp`, `ui_manager`, `websocket_client`, `service_discovery`
- Each specialist understands your component-based ESP-IDF architecture

### 2. **Hardware-Specific Expertise**
- **Round Display Focus**: LVGL agents optimized for 800x800 circular UI design
- **Dual-Chip Architecture**: Specialists understand ESP32-P4 + ESP32-C6 SDIO communication
- **Real-time Audio**: Performance agents tuned for <50ms latency requirements

### 3. **Integration Workflow Optimization**
- Multi-agent workflows for complex tasks (audio chain, touch pipeline, network integration)
- Component dependency analysis and CMake optimization focus
- Real-time safety validation for FreeRTOS dual-core architecture

### 4. **ESP-IDF Ecosystem Alignment**
- Agents understand your IDF component manager dependencies
- CMakeLists.txt and build system optimization expertise
- ESP-IDF 5.3+ and advanced component integration knowledge

---

Your optimized embedded systems AI team is ready for ESP32-P4 HowdyScreen development!

**Component Quick Start Commands:**
- "@code-archaeologist explore components/audio_processor/ structure and I2S integration patterns"
- "@frontend-developer design circular LVGL voice assistant interface in components/ui_manager/"
- "@api-architect implement HowdyTTS WebSocket streaming in components/websocket_client/"
- "@performance-optimizer profile audio latency across the complete pipeline"
- "@backend-developer create hardware diagnostic tools for ESP32-P4 peripheral validation"