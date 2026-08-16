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

// Per-mode grind button colors
#define THEME_COLOR_MODE_WEIGHT 0x7EC8E3                                       // Weight pane button (powder blue)
#define THEME_COLOR_MODE_TIME 0x5C1224                                         // Time pane button (wine burgundy)
#define THEME_COLOR_MODE_MANUAL 0xF8DE7E                                       // Manual pane button (butter yellow)
#define THEME_COLOR_MODE_ICON_DARK 0x1A1A1A                                    // Icon color on light mode buttons

// Screensaver ripple dot colors (dark -> bright brown ramp)
#define THEME_COLOR_SCREENSAVER_DOT_DARK 0x1C0F06                              // Ripple trough (near-black brown)
#define THEME_COLOR_SCREENSAVER_DOT 0x6F4E37                                   // Ripple midpoint (coffee brown)
#define THEME_COLOR_SCREENSAVER_DOT_BRIGHT 0xC08948                            // Ripple crest (warm tan)
#define THEME_COLOR_SCREENSAVER_PILL_BG 0x2A2A2A                               // Trains arrival-time pill background
#define THEME_COLOR_SCREENSAVER_PAGE_DOT 0x555555                              // Trains page indicator, inactive page (active uses text primary)

// Trains arrival catchability warnings (vs. the watch's walk-to-platform time):
// pill ring on the grouped view, countdown text on the board view
#define THEME_COLOR_SCREENSAVER_CATCH_RUSH 0xFFD60A                            // Reachable only by rushing (yellow)
#define THEME_COLOR_SCREENSAVER_CATCH_MISS 0xFF5B52                            // Can't be caught (red)

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