## Project Overview

ESP32-S3 intelligent coffee scale with grind-by-weight functionality. Features predictive grinding system, LVGL touch UI, and BLE OTA updates. Automatically grinds coffee beans to precise target weights using flow prediction and pulse correction algorithms.

## Essential Commands

All development tasks use the unified cross-platform Python tool:

```bash
# Build and upload
python3 tools/grinder.py build-upload

# Data analysis (exports data and launches Streamlit report)  
python3 tools/grinder.py analyze
```

**Common Commands:**
- `python3 tools/grinder.py build` - Build firmware only
- `python3 tools/grinder.py upload` - Upload latest firmware via BLE
- `python3 tools/grinder.py export` - Export grind data to database
- `python3 tools/grinder.py report` - Launch Streamlit report from existing data
- `python3 tools/grinder.py scan` - Scan for BLE devices
- `python3 tools/grinder.py info` - Get device system information
- `python3 tools/grinder.py clean` - Clean build artifacts

## Architecture

**4-Layer Architecture:**
1. **Hardware Layer** (`src/hardware/`): ESP32-S3 peripheral abstraction
2. **Control Layer** (`src/controllers/`): Business logic and algorithms  
3. **System Layer** (`src/system/`): State management
4. **UI Layer** (`src/ui/`): LVGL touchscreen interface

**Key Components:**
- **HardwareManager**: Central hardware coordinator
- **GrindController**: 9-phase state machine with predictive flow control, 10 pulse corrections, and time mode additional pulses
- **LoadCell (HX711)**: Multi-mode precision weight measurement (instant, smoothed, filtered)
- **UIManager**: 7 screens with LVGL integration, including split-button layout for time mode pulses
- **StateMachine**: Central state coordination (READY → GRINDING → GRIND_COMPLETE)

**Update Intervals:** 20ms grind control, 25ms load cell (active), 50ms UI/hardware

**Grind Phases:**
- Standard phases: IDLE, INITIALIZING, SETUP, TARING, TARE_CONFIRM, PREDICTIVE, PULSE_DECISION, PULSE_EXECUTE, PULSE_SETTLING, FINAL_SETTLING, TIME_GRINDING, COMPLETED, TIMEOUT
- New: `TIME_ADDITIONAL_PULSE` - Dedicated phase for post-completion additional grinding pulses in time mode
- **Timeout**: 30-second maximum from grind start (includes taring), auto-stops and requires user acknowledgment

**Time Mode Pulses:** Split-button completion screen (OK + PULSE), `TIME_ADDITIONAL_PULSE` phase, 100ms duration

**Color Scheme (RGB565):**
- `COLOR_PRIMARY`: 0xFF0000 (Red) - Primary theme color
- `COLOR_ACCENT`: 0x00AAFF (Blue) - Highlights and accents
- `COLOR_SUCCESS`: 0x00AA00 (Green) - Success states
- `COLOR_WARNING`: 0xCC8800 (Orange) - Warning states
- `COLOR_BACKGROUND`: 0x000000 (Black) - Background
- `COLOR_TEXT_PRIMARY`: 0xFFFFFF (White) - Primary text

**Font Usage Hierarchy:**
- `lv_font_montserrat_24`: Standard text and button labels
- `lv_font_montserrat_32`: Button symbols (OK, CLOSE, PLUS, MINUS)
- `lv_font_montserrat_36`: Screen titles
- `lv_font_montserrat_56`: Large weight displays

## Development Notes

* When modifying this codebase, follow the existing architectural patterns, maintain the clean separation between layers, and ensure any timing-critical code respects the established update intervals.
* use macos compatible commands. macos uses python3
* after making a test build let me know the build number
* Always read entire files. Otherwise, you don’t know what you don’t know, and will end up making mistakes, duplicating code that already exists, or misunderstanding the architecture.  
* Commit early and often. When working on large tasks, your task could be broken down into multiple logical milestones. After a certain milestone is completed and confirmed to be ok by the user, you should commit it. If you do not, if something goes wrong in further steps, we would need to end up throwing away all the code, which is expensive and time consuming.  
* Your internal knowledgebase of libraries might not be up to date. When working with any external library, unless you are 100% sure that the library has a super stable interface, you will look up the latest syntax and usage via either Perplexity (first preference) or web search (less preferred, only use if Perplexity is not available)  
* Do not say things like: “x library isn’t working so I will skip it”. Generally, it isn’t working because you are using the incorrect syntax or patterns. This applies doubly when the user has explicitly asked you to use a specific library, if the user wanted to use another library they wouldn’t have asked you to use a specific one in the first place.  
* Always run linting after making major changes. Otherwise, you won’t know if you’ve corrupted a file or made syntax errors, or are using the wrong methods, or using methods in the wrong way.   
* Please organise code into separate files wherever appropriate, and follow general coding best practices about variable naming, modularity, function complexity, file sizes, commenting, etc.  
* Code is read more often than it is written, make sure your code is always optimised for readability  
* Unless explicitly asked otherwise, the user never wants you to do a “dummy” implementation of any given task. Never do an implementation where you tell the user: “This is how it *would* look like”. Just implement the thing.  
* Whenever you are starting a new task, it is of utmost importance that you have clarity about the task. You should ask the user follow up questions if you do not, rather than making incorrect assumptions.  
* Do not carry out large refactors unless explicitly instructed to do so.  
* When starting on a new task, you should first understand the current architecture, identify the files you will need to modify, and come up with a Plan. In the Plan, you will think through architectural aspects related to the changes you will be making, consider edge cases, and identify the best approach for the given task. Get your Plan approved by the user before writing a single line of code.   
* If you are running into repeated issues with a given task, figure out the root cause instead of throwing random things at the wall and seeing what sticks, or throwing in the towel by saying “I’ll just use another library / do a dummy implementation”.   
* You are an incredibly talented and experienced polyglot with decades of experience in diverse areas such as software architecture, system design, development, UI & UX, copywriting, and more.  
* When doing UI & UX work, make sure your designs are both aesthetically pleasing, easy to use, and follow UI / UX best practices. You pay attention to interaction patterns, micro-interactions, and are proactive about creating smooth, engaging user interfaces that delight users.   
* When you receive a task that is very large in scope or too vague, you will first try to break it down into smaller subtasks. If that feels difficult or still leaves you with too many open questions, push back to the user and ask them to consider breaking down the task for you, or guide them through that process. This is important because the larger the task, the more likely it is that things go wrong, wasting time and energy for everyone involved.
- "requestFrom(): i2cWriteReadNonStop returned Error -1" serial messages  are harmless messages caused by polling the touch driver (no interrupts) and should be ignored
- Use the src/config/constants.h aggregation file to include constants / settings - dont refer to config files directly.