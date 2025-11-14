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

## Methods Explained

- **Screen replacement**: Replace existing screen with Waveshare adapter
- **Pin soldering**: Direct board connections ([Besson method](https://besson.co/projects/coffee-grinder-smart-scale)) + custom mount using [abandoned chute cover](3d_files/chute%20cover/) as starting point
- **Custom work**: Full electronics research + mechanical adaptation needed

See: **[3D PRINTS](3D_PRINTS.md)** for a WIP design that adds a Waveshare screenmount for Eureka grinders that don't come with one.

![Pin soldering example](https://besson.co/_next/image?url=%2F_next%2Fstatic%2Fmedia%2Fgrinder-pins.ad081f28.jpg&w=3840&q=75)  
*Pin connections for non-screen models. Credit: [Besson Coffee Grinder Smart Scale](https://besson.co/projects/coffee-grinder-smart-scale)*

## How to adapt this mod for a not yet supported grinder

Bringing the Smart Grind-by-Weight setup to a new grinder mostly means recreating the mechanical interface and ensuring safe motor control.

- Design and print a new 3D mount that secures the load cell inside the grinder without interfering with the chute or burr carrier.
- Integrate the SSR-40DA (or equivalent) so the ESP32 can switch the grinder motor safely, tapping into the grinder's existing on/off path while keeping mains fully isolated.

For a concrete wiring example, reference the [Eureka Specialita reverse engineering notes](docs/eureka-specialita-reverse-engineering.md) that document how the mod ties into the stock control board.
