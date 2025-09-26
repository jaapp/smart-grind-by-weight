#pragma once

//==============================================================================
// USER INTERFACE THEME CONFIGURATION
//==============================================================================
// This file contains all user interface theming and visual design constants
// for the LVGL-based touchscreen interface. These values define the visual
// appearance, colors, dimensions, and styling of all UI elements.
//
// Categories:
// - Color Palette (primary, secondary, accent, text, status colors)
// - Component Dimensions (button sizes, arc diameters, spacing)
// - Visual Effects (corner radius, transparency, animations)
// - Typography Settings (font selections, text sizing)
// - Layout Constants (margins, padding, alignment values)
//
// All constants use the THEME_ prefix to indicate UI theming parameters.
// Color values are in RGB565 format for optimal display performance.

//------------------------------------------------------------------------------
// COLOR SCHEME (RGB565 Format)
//------------------------------------------------------------------------------
// Primary brand colors
#define THEME_COLOR_PRIMARY 0xFF0000                                           // Primary theme color (red)
#define THEME_COLOR_ACCENT 0x00AAFF                                            // Accent color for highlights (blue)
#define THEME_COLOR_SECONDARY 0xAAAAAA                                         // Secondary theme color (light gray)

// Text colors
#define THEME_COLOR_TEXT_PRIMARY 0xFFFFFF                                      // Primary text color (white)
#define THEME_COLOR_TEXT_SECONDARY 0xCCCCCC                                    // Secondary text color (light gray)

// Background colors
#define THEME_COLOR_BACKGROUND 0x000000                                        // Background color (black)
#define THEME_COLOR_NEUTRAL 0x666666                                           // Neutral color (dark gray)

// Status indication colors
#define THEME_COLOR_SUCCESS 0x00AA00                                           // Success state color (green)
#define THEME_COLOR_ERROR 0xFF0000                                             // Error state color (red)
#define THEME_COLOR_WARNING 0xCC8800                                           // Warning state color (darker yellow/orange)
#define THEME_COLOR_GRINDER_ACTIVE 0x403800                                   // Grinder active indicator (dark yellow)

//------------------------------------------------------------------------------
// UI ELEMENT DIMENSIONS
//------------------------------------------------------------------------------
// Button specifications
#define THEME_BUTTON_HEIGHT_PX 90                                             // Standard button height
#define THEME_BUTTON_WIDTH_PX 120                                             // Standard button width
#define THEME_BUTTON_CORNER_RADIUS_PX 12                                      // Button corner radius

// Progress and feedback elements
#define THEME_PROGRESS_ARC_DIAMETER_PX 200                                    // Progress arc diameter

// General layout
#define THEME_CORNER_RADIUS_PX 12                                             // Standard UI element corner radius

//------------------------------------------------------------------------------  
// FONT SIZE HIERARCHY
//------------------------------------------------------------------------------
// Based on Montserrat font family from CLAUDE.md specifications
// Note: Actual font objects are defined elsewhere; these are logical size references

#define THEME_FONT_SIZE_LARGE_DISPLAY 56                                      // Large weight displays
#define THEME_FONT_SIZE_SCREEN_TITLE 36                                       // Screen titles  
#define THEME_FONT_SIZE_BUTTON_SYMBOL 32                                      // Button symbols (OK, CLOSE, PLUS, MINUS)
#define THEME_FONT_SIZE_STANDARD_TEXT 24                                      // Standard text and button labels

//------------------------------------------------------------------------------
// SPACING AND MARGINS
//------------------------------------------------------------------------------
#define THEME_MARGIN_SMALL_PX 8                                               // Small margin/padding
#define THEME_MARGIN_MEDIUM_PX 16                                             // Medium margin/padding  
#define THEME_MARGIN_LARGE_PX 24                                              // Large margin/padding
#define THEME_MARGIN_XLARGE_PX 32                                             // Extra large margin/padding

//------------------------------------------------------------------------------
// ANIMATION AND TIMING
//------------------------------------------------------------------------------
#define THEME_ANIMATION_DURATION_FAST_MS 150                                  // Fast animations
#define THEME_ANIMATION_DURATION_NORMAL_MS 300                                // Normal animations
#define THEME_ANIMATION_DURATION_SLOW_MS 500                                  // Slow animations

//------------------------------------------------------------------------------
// OPACITY VALUES
//------------------------------------------------------------------------------
#define THEME_OPACITY_DISABLED 128                                            // Disabled element opacity (50% of 255)
#define THEME_OPACITY_OVERLAY 204                                             // Overlay background opacity (80% of 255)
#define THEME_OPACITY_FULL 255                                                // Full opacity