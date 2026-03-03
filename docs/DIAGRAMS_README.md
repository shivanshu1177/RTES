# RTES PlantUML Diagrams

This directory contains PlantUML diagrams for the Real-Time Trading Exchange Simulator.

## Diagrams

### 1. **architecture.puml** - System Architecture
- Shows all major components (Gateway, Risk, Matching, UDP)
- Lock-free queues (SPSC, MPMC)
- Memory management (Order Pool)
- Data flow between components
- Latency annotations

### 2. **order_flow.puml** - Order Flow Sequence
- End-to-end order processing
- Step-by-step timing breakdown
- Shows BUY 100 AAPL @ $150.00 example
- Total latency: ~9μs

### 3. **class_diagram.puml** - Core Classes
- Data structures (Order, Trade, MessageHeader)
- Lock-free queues (SPSC, MPMC)
- Core components (Gateway, Risk, Matching, OrderBook)
- Memory pools
- Relationships and dependencies

### 4. **threading_model.puml** - Threading & Deployment
- Thread architecture (8 threads)
- Thread communication via lock-free queues
- Shared memory (Order Pool, Queues)
- External connections (TCP, UDP, HTTP)
- Performance characteristics

### 5. **order_lifecycle.puml** - Order State Machine
- Order states (PENDING → ACCEPTED → FILLED)
- State transitions
- Rejection reasons
- Cancellation flow

## How to View

### Online (Easiest)
1. Go to http://www.plantuml.com/plantuml/uml/
2. Copy-paste diagram code
3. View rendered diagram

### VS Code
1. Install "PlantUML" extension
2. Open `.puml` file
3. Press `Alt+D` to preview

### Command Line
```bash
# Install PlantUML
brew install plantuml  # macOS
sudo apt install plantuml  # Linux

# Generate PNG
plantuml architecture.puml
plantuml order_flow.puml
plantuml class_diagram.puml
plantuml threading_model.puml
plantuml order_lifecycle.puml

# Generate SVG (better quality)
plantuml -tsvg *.puml

# Generate all at once
plantuml *.puml
```

### Docker
```bash
docker run -v $(pwd):/data plantuml/plantuml *.puml
```

## Output Files
After rendering, you'll get:
- `architecture.png` - System architecture diagram
- `order_flow.png` - Order flow sequence
- `class_diagram.png` - Class relationships
- `threading_model.png` - Threading model
- `order_lifecycle.png` - State machine

## Interview Preparation

### Use These Diagrams To:
1. **Explain Architecture**: Show `architecture.puml` to explain component interaction
2. **Trace Order Flow**: Walk through `order_flow.puml` step-by-step
3. **Discuss Data Structures**: Reference `class_diagram.puml` for implementation details
4. **Explain Threading**: Use `threading_model.puml` to show concurrency design
5. **Describe Order States**: Use `order_lifecycle.puml` for state transitions

### Key Points to Highlight:
- **Lock-free queues** for inter-thread communication
- **Single-writer** per symbol (no contention)
- **Memory pools** for zero-allocation hot path
- **Cache prefetching** for performance
- **~9μs end-to-end latency**

## Customization

To modify diagrams:
1. Edit `.puml` files
2. Re-render with PlantUML
3. Verify changes

### Common Customizations:
- Add new components
- Change colors (`!define COMPONENT_BG #COLOR`)
- Add notes (`note right of Component`)
- Adjust layout (`left to right direction`)

## Resources
- PlantUML Guide: https://plantuml.com/guide
- Sequence Diagrams: https://plantuml.com/sequence-diagram
- Class Diagrams: https://plantuml.com/class-diagram
- Component Diagrams: https://plantuml.com/component-diagram
- State Diagrams: https://plantuml.com/state-diagram
