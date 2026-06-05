// ============================================================================
// SpiroBird Controller — secrets template
//
// Copy this file to  controller/include/secrets.h  and fill in real values.
// secrets.h is gitignored — NEVER commit real URLs/credentials.
// The project compiles without secrets.h using these safe defaults.
//
// Wi-Fi credentials are NOT stored here — they are provisioned at runtime via
// the captive portal (SpiroBird-Setup) and saved in NVS/Preferences.
// ============================================================================
#pragma once

// Base URL of the backend (no trailing slash).
// Local development:  "http://192.168.1.100:3000"
// Render deployment:  "https://spirobird.onrender.com"
#define SERVER_BASE_URL "http://192.168.1.100:3000"

// Identifier sent with every result POST.
#define DEVICE_ID "spirobird-01"
