#pragma once

//==============================================================================
// USER INTERFACE THEME CONFIGURATION
//==============================================================================
// This file contains all user interface theming and visual design constants
// for the LVGL-based touchscreen interface. These values define the visual
// appearance, colors, dimensions, and styling of all UI elements.

//------------------------------------------------------------------------------
// COLOR SCHEME (RGB565 Format)
//------------------------------------------------------------------------------
// Primary brand colors
#define THEME_COLOR_PRIMARY 0xFF3D00                                           // Primary theme color (red)
#define THEME_COLOR_ACCENT 0x00AAFF                                            // Accent color for highlights (blue)
#define THEME_COLOR_SECONDARY 0xAAAAAA                                         // Secondary theme color (light gray)

// Text colors
#define THEME_COLOR_TEXT_PRIMARY 0xFFFFFF                                      // Primary text color (white)
#define THEME_COLOR_TEXT_SECONDARY 0xCCCCCC                                    // Secondary text color (light gray)

// Background colors
#define THEME_COLOR_BACKGROUND 0x000000                                        // Background color (black)
#define THEME_COLOR_NEUTRAL 0x666666                                           // Neutral color (dark gray)
#define THEME_COLOR_BACKGROUND_MOCK 0x035e03                                   // Background color when mock hardware is active (dark green)

// Status indication colors
#define THEME_COLOR_SUCCESS 0x00AA00                                           // Success state color (green)
#define THEME_COLOR_ERROR 0xFF0000                                             // Error state color (red)
#define THEME_COLOR_WARNING 0xCC8800                                           // Warning state color (darker yellow/orange)
#define THEME_COLOR_GRINDER_ACTIVE 0x403800                                    // Grinder active indicator (dark yellow)

//------------------------------------------------------------------------------
// UI ELEMENT DIMENSIONS
//------------------------------------------------------------------------------
// Button specifications
#define THEME_BUTTON_WIDTH_PX 120                                             // Standard button width

// Progress and feedback elements
#define THEME_PROGRESS_ARC_DIAMETER_PX 200                                    // Progress arc diameter

// General layout
#define THEME_CORNER_RADIUS_PX 20                                             // Standard UI element corner radius




//------------------------------------------------------------------------------
// OPACITY VALUES
//------------------------------------------------------------------------------
#define THEME_OPACITY_OVERLAY 204                                             // Overlay background opacity (80% of 255)