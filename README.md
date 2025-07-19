# vstream

Enhanced Vosk-based Speech Recognition Server with WebSocket API

## Overview
vstream is a high-performance, real-time speech recognition server that provides WebSocket-based API access to the Vosk speech recognition engine. It's designed for production use with features like voice activity detection, speaker identification, and grammar-based recognition constraints.

## Key Features
- Real-time speech recognition via WebSocket API
- Local microphone capture support with PortAudio
- WebRTC-based Voice Activity Detection (VAD)
- Speaker identification/verification support
- Grammar-based recognition constraints
- N-best alternatives with confidence scores
- Word-level timing information
- Thread-safe architecture with lock-free audio queues
- Comprehensive logging system

## Dependencies
### Required Libraries
- Vosk API - Speech recognition engine
- PortAudio - Cross-platform audio I/O
- libfvad - WebRTC Voice Activity Detection
- Boost - WebSocket and system libraries
- nlohmann/json - JSON parsing
- moodycamel/concurrentqueue - Lock-free queue implementation

### Build Tools
- CMake 3.16 or higher
- C++20 compatible compiler (GCC 10+, Clang 11+, MSVC 2019+)
- pkg-config (for dependency discovery)

## Installing Dependencies
### Ubuntu/Debian
```bash
# System packages
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libboost-system-dev \
    libboost-thread-dev \
    portaudio19-dev \
    nlohmann-json3-dev

# Install libfvad (WebRTC VAD)
git clone https://github.com/dpirch/libfvad.git
cd libfvad
mkdir build && cd build
cmake ..
make
sudo make install
sudo ldconfig

# Install concurrentqueue
git clone https://github.com/cameron314/concurrentqueue.git
sudo cp -r concurrentqueue/concurrentqueue /usr/include/

# Install Vosk API
wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-x86_64-0.3.45.zip
unzip vosk-linux-x86_64-0.3.45.zip
sudo cp -r vosk-linux-x86_64-0.3.45/* /usr/local/
sudo ldconfig
```
### macOS
```bash
# Using Homebrew
brew install cmake boost portaudio nlohmann-json pkg-config
# Install other dependencies from source (same as Linux)
```

## Downloading Vosk Models
```bash
# Download a model (example: US English)
wget https://alphacephei.com/vosk/models/vosk-model-en-us-0.22.zip
unzip vosk-model-en-us-0.22.zip
mv vosk-model-en-us-0.22 models/

# For speaker identification (optional)
wget https://alphacephei.com/vosk/models/vosk-model-spk-0.4.zip
unzip vosk-model-spk-0.4.zip
mv vosk-model-spk-0.4 models/
```

### Building
```bash
# Clone the repository
git clone https://github.com/yourusername/vstream.git
cd vstream

# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make -j$(nproc)

# Optional: Install
sudo make install
```

## Build Options
```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release build with optimizations
cmake -DCMAKE_BUILD_TYPE=Release ..

# Custom install prefix
cmake -DCMAKE_INSTALL_PREFIX=/opt/vstream ..
```

## Usage
### Basic Server Usage
```bash
# Start server with model
./vstream --model ../models/vosk-model-en-us-0.22

# Start with microphone capture
./vstream --model ../models/vosk-model-en-us-0.22 --mic

# Custom port
./vstream --model ../models/vosk-model-en-us-0.22 --port 9090

# With speaker identification
./vstream --model ../models/vosk-model-en-us-0.22 \
          --spk-model ../models/vosk-model-spk-0.4 \
          --mic
```

### Command Line Options
```
Options:
  --model PATH       Path to Vosk model directory (required)
  --port PORT        WebSocket server port (default: 8080)
  --mic              Enable microphone capture
  --mic-device N     Specify microphone device index
  --buffer-ms MS     Audio buffer size in milliseconds (default: 100)
  --list-devices     List available audio input devices
  --spk-model PATH   Path to speaker model (optional)
  --alternatives N   Enable N-best results (default: 0)
  --no-partial       Disable partial results
  --grammar JSON     Set grammar as JSON array
  --log-level N      Set Vosk log level (default: 0)
  --help             Show help message
```
## WebSocket API
### Connecting
```js
const ws = new WebSocket('ws://localhost:8080');
```
### Sending Audio
```js
// Send audio data
const audioData = new Int16Array(buffer);
ws.send(audioData.buffer);
```
### Receiving Results
```js
ws.onmessage = (event) => {
    const result = JSON.parse(event.data);
    
    if (result.text) {
        console.log('Final:', result.text);
    } else if (result.partial) {
        console.log('Partial:', result.partial);
    }
};
```
### Commands
```js
// Reset recognizer
ws.send(JSON.stringify({
    command: 'reset'
}));

// Set grammar
ws.send(JSON.stringify({
    command: 'set_grammar',
    grammar: ['yes', 'no', 'maybe']
}));

// Get statistics
ws.send(JSON.stringify({
    command: 'stats'
}));
```
### Grammar Format
Grammar can be specified as a JSON array of allowed phrases:

```bash
# Simple word list
--grammar '["yes", "no", "maybe"]'

# With alternatives
--grammar '["turn on the light", "turn off the light", "dim the light"]'
```

## Configuration
### Audio Configuration
The microphone capture can be configured for different latency/performance trade-offs:
- --buffer-ms 50: Low latency, higher CPU usage
- --buffer-ms 100: Balanced (default)
- --buffer-ms 200: Higher latency, more efficient

### VAD Configuration
Voice Activity Detection is pre-configured with:

- 20ms frame duration
- LOW_BITRATE aggressiveness mode
- 300ms hangover time
- 100ms startup time

## API Documentation
Generate API documentation using Doxygen:

```bash
doxygen Doxyfile
# Open docs/html/index.html
```

## Examples
### Python Client Example
```python
import websocket
import json
import numpy as np

def on_message(ws, message):
    result = json.parse(message)
    if 'text' in result:
        print(f"Recognized: {result['text']}")

ws = websocket.WebSocketApp("ws://localhost:8080",
                          on_message=on_message)

# Send audio in chunks
def send_audio():
    # Read 16kHz, 16-bit PCM audio
    audio = read_audio_chunk()
    ws.send(audio.tobytes(), opcode=websocket.ABNF.OPCODE_BINARY)

ws.run_forever()
```
### Microphone Streaming
```bash
# List available microphones
./vstream --list-devices

# Use specific microphone
./vstream --model models/vosk-model-en-us-0.22 --mic --mic-device 2
```

## Performance Tuning
### Optimization Tips
1. Model Selection: Smaller models are faster but less accurate

- vosk-model-small-en-us-0.15: Fast, lower accuracy
- vosk-model-en-us-0.22: Balanced
- vosk-model-en-us-0.22-lgraph: Large, highest accuracy

2. Buffer Size: Adjust --buffer-ms based on your needs

- Real-time interaction: 50-100ms
- Batch processing: 200-300ms

3. Compile Optimizations:

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native" ..
```

## Troubleshooting
### Common Issues
1. No audio input detected

- Check microphone permissions
- Verify device index with --list-devices
- Ensure correct sample rate (16kHz)

2. High CPU usage

- Use a smaller model
- Increase buffer size
- Disable features you don't need (speaker ID, alternatives)

3. WebSocket connection fails

- Check firewall settings
- Verify port is not in use
- Check server logs for errors

## Debug Logging
Enable detailed logging:

```bash
# Set Vosk log level
./vstream --model models/vosk-model-en-us-0.22 --log-level 3

# Check log file
tail -f vstream_log_*.log
```

## License
MIT

## Acknowledgments
- Vosk API by Alpha Cephei
- WebRTC VAD implementation
- PortAudio for cross-platform audio
- All open-source contributors
