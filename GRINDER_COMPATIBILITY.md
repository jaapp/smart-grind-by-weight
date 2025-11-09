# Grinder Compatibility Matrix

Compatibility guide for Smart Grind-by-Weight modification across grinder models. GBW (Grind-by-Weight) models excluded as they don't need this mod.

## Compatibility Overview

| Model | Status | Method | 3D Parts | Notes |
|-------|--------|--------|----------|--------|
| [**Mignon Specialita**](https://www.eureka.co.it/en/products/eureka+1920/mignon+grinders/silent+range/20.aspx) | ✅ **100% Compatible** | Screen replacement | ✅ **Ready** | Tested, direct fit |
| [**Mignon XL**](https://www.eureka.co.it/en/products/eureka+oro/prosumer+grinders/prosumer/40.aspx) | ✅ **100% Compatible** | Screen replacement | ✅ **Ready** | Confirmed working |
| [**Mignon Silenzio**](https://www.eureka.co.it/en/products/eureka+1920/mignon+grinders/silent+range/19.aspx) | 🔧 **Pin soldering** | Custom mount + pins | 🏗️ **WIP design** | Timer pot, no screen |
| [**Mignon Crono**](https://www.eureka.co.it/en/products/eureka+1920/mignon+grinders/filter+range/26.aspx) | 🔧 **Extra hardware** | Custom mount + pins + relay | 🏗️ **WIP design** | Timer pot, no screen. Requires relay (230V optocoupler 3.3V or SSR-40DA) for motor control |
| [**Mignon Manuale**](https://www.eureka.co.it/en/products/eureka+1920/mignon+grinders/evolution+range/27.aspx) | 🔧 **Pin soldering** | Custom mount + pins | 🏗️ **WIP design** | Timer pot, no screen |
| [**Mignon Zero**](https://www.eureka.co.it/en/products/eureka+1920/mignon+grinders/zero+range/74.aspx) | 🔧 **Extra hardware** | Custom mount + relay | 🏗️ **WIP design** | Requires 230V optocoupler relay (3.3V logic) + external USB power |
| [**Atom series**](https://www.eureka.co.it/en/products/eureka+1920/commercial+grinders/atom+range/8.aspx) | ❓ **Unknown** | Research needed | ❌ **None** | Internals unknown |
| **Baratza models** | 🔧 **Custom work** | Full adaptation | ❌ **None** | Electronics + mounting |
| **Niche Zero** | 🔧 **Custom work** | Full adaptation | ❌ **None** | Electronics + mounting |
| **Mazzer models** | 🔧 **Custom work** | Full adaptation | ❌ **None** | Electronics + mounting |

## Methods Explained

- **Screen replacement**: Replace existing screen with Waveshare adapter
- **Pin soldering**: Direct board connections ([Besson method](https://besson.co/projects/coffee-grinder-smart-scale)) + custom mount using [abandoned chute cover](3d_files/chute%20cover/) as starting point
- **Custom work**: Full electronics research + mechanical adaptation needed

See: **[3D PRINTS](3D_PRINTS.md)** for a WIP design that adds a Waveshare screenmount for Eureka grinders that don't come with one.

![Pin soldering example](https://besson.co/_next/image?url=%2F_next%2Fstatic%2Fmedia%2Fgrinder-pins.ad081f28.jpg&w=3840&q=75)  
*Pin connections for non-screen models. Credit: [Besson Coffee Grinder Smart Scale](https://besson.co/projects/coffee-grinder-smart-scale)*

## Requirements

**Universal**: ESP32-S3 AMOLED, HX711, load cell, 5V power, motor control signal
**Pin soldering**: 2N3904 transistor for 3.3V→5V conversion
**Relay control**: 230V optocoupler relay (3.3V logic) or SSR-40DA solid-state relay. Provides safety isolation between 230V mains and ESP32. Grinder auto-detects/auto-tunes relay delays.
**Custom mounting**: 3D printed parts, mechanical adaptation
