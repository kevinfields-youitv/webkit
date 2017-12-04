/*
 * Copyright (C) 2010-2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if PLATFORM(GTK)
#define DEFAULT_WEBKIT_TABSTOLINKS_ENABLED true
#else
#define DEFAULT_WEBKIT_TABSTOLINKS_ENABLED false
#endif

#if ENABLE(SMOOTH_SCROLLING)
#define DEFAULT_WEBKIT_SCROLL_ANIMATOR_ENABLED true
#else
#define DEFAULT_WEBKIT_SCROLL_ANIMATOR_ENABLED false
#endif

#if PLATFORM(COCOA) || PLATFORM(GTK)
#define DEFAULT_HIDDEN_PAGE_DOM_TIMER_THROTTLING_ENABLED true
#define DEFAULT_HIDDEN_PAGE_CSS_ANIMATION_SUSPENSION_ENABLED true
#else
#define DEFAULT_HIDDEN_PAGE_DOM_TIMER_THROTTLING_ENABLED false
#define DEFAULT_HIDDEN_PAGE_CSS_ANIMATION_SUSPENSION_ENABLED false
#endif

#if ENABLE(SERVER_PRECONNECT)
#define DEFAULT_LINK_PRECONNECT_ENABLED true
#else
#define DEFAULT_LINK_PRECONNECT_ENABLED false
#endif

#if PLATFORM(COCOA)
#define DEFAULT_PDFPLUGIN_ENABLED true
#else
#define DEFAULT_PDFPLUGIN_ENABLED false
#endif

#if PLATFORM(IOS)
#define DEFAULT_ALLOWS_PICTURE_IN_PICTURE_MEDIA_PLAYBACK true
#define DEFAULT_BACKSPACE_KEY_NAVIGATION_ENABLED false
#define DEFAULT_FRAME_FLATTENING FrameFlattening::FullyEnabled
#define DEFAULT_SHOULD_PRINT_BACKGROUNDS true
#define DEFAULT_TEXT_AREAS_ARE_RESIZABLE false
#define DEFAULT_JAVASCRIPT_CAN_OPEN_WINDOWS_AUTOMATICALLY false
#define DEFAULT_SHOULD_RESPECT_IMAGE_ORIENTATION true
#define DEFAULT_PASSWORD_ECHO_ENABLED true
#define DEFAULT_ALLOWS_INLINE_MEDIA_PLAYBACK false
#define DEFAULT_ALLOWS_INLINE_MEDIA_PLAYBACK_AFTER_FULLSCREEN true
#define DEFAULT_INLINE_MEDIA_PLAYBACK_REQUIRES_PLAYS_INLINE_ATTRIBUTE true
#define DEFAULT_INVISIBLE_AUTOPLAY_NOT_PERMITTED true
#define DEFAULT_MEDIA_DATA_LOADS_AUTOMATICALLY false
#define DEFAULT_MEDIA_CONTROLS_SCALE_WITH_PAGE_ZOOM false
#define DEFAULT_TEMPORARY_TILE_COHORT_RETENTION_ENABLED false
#define DEFAULT_REQUIRES_USER_GESTURE_FOR_AUDIO_PLAYBACK true
#define DEFAULT_LEGACY_ENCRYPTED_MEDIA_API_ENABLED false
#define DEFAULT_INTERACTIVE_MNEDIA_CAPTURE_STREAM_REPROMPT_INTERVAL_IN_MINUTES 1
#else
#define DEFAULT_ALLOWS_PICTURE_IN_PICTURE_MEDIA_PLAYBACK false
#define DEFAULT_BACKSPACE_KEY_NAVIGATION_ENABLED true
#define DEFAULT_FRAME_FLATTENING FrameFlattening::Disabled
#define DEFAULT_SHOULD_PRINT_BACKGROUNDS false
#define DEFAULT_TEXT_AREAS_ARE_RESIZABLE true
#define DEFAULT_JAVASCRIPT_CAN_OPEN_WINDOWS_AUTOMATICALLY true
#define DEFAULT_SHOULD_RESPECT_IMAGE_ORIENTATION false
#define DEFAULT_PASSWORD_ECHO_ENABLED false
#define DEFAULT_ALLOWS_INLINE_MEDIA_PLAYBACK true
#define DEFAULT_ALLOWS_INLINE_MEDIA_PLAYBACK_AFTER_FULLSCREEN false
#define DEFAULT_INLINE_MEDIA_PLAYBACK_REQUIRES_PLAYS_INLINE_ATTRIBUTE false
#define DEFAULT_INVISIBLE_AUTOPLAY_NOT_PERMITTED false
#define DEFAULT_MEDIA_DATA_LOADS_AUTOMATICALLY true
#define DEFAULT_MEDIA_CONTROLS_SCALE_WITH_PAGE_ZOOM true
#define DEFAULT_TEMPORARY_TILE_COHORT_RETENTION_ENABLED true
#define DEFAULT_REQUIRES_USER_GESTURE_FOR_AUDIO_PLAYBACK false
#define DEFAULT_LEGACY_ENCRYPTED_MEDIA_API_ENABLED true
#define DEFAULT_INTERACTIVE_MNEDIA_CAPTURE_STREAM_REPROMPT_INTERVAL_IN_MINUTES 10
#endif

#if PLATFORM(COCOA)
#define DEFAULT_ALLOW_MEDIA_CONTENT_TYPES_REQUIRING_HARDWARE_SUPPORT_AS_FALLBACK true
#else
#define DEFAULT_ALLOW_MEDIA_CONTENT_TYPES_REQUIRING_HARDWARE_SUPPORT_AS_FALLBACK false
#endif

#if PLATFORM(MAC) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 101300
#define DEFAULT_SUBPIXEL_ANTIALIASED_LAYER_TEXT_ENABLED true
#else
#define DEFAULT_SUBPIXEL_ANTIALIASED_LAYER_TEXT_ENABLED false
#endif

#if PLATFORM(IOS_SIMULATOR)
#define DEFAULT_ACCELERATED_DRAWING_ENABLED false
#define DEFAULT_CANVAS_USES_ACCELERATED_DRAWING false
#else
#define DEFAULT_ACCELERATED_DRAWING_ENABLED true
#define DEFAULT_CANVAS_USES_ACCELERATED_DRAWING true
#endif

#if PLATFORM(COCOA)
#define DEFAULT_SHOULD_CAPTURE_AUDIO_IN_UIPROCESS true
#else
#define DEFAULT_SHOULD_CAPTURE_AUDIO_IN_UIPROCESS false
#endif

#if PLATFORM(COCOA)
#define DEFAULT_MODERN_MEDIA_CONTROLS_ENABLED true
#else
#define DEFAULT_MODERN_MEDIA_CONTROLS_ENABLED false
#endif

#if PLATFORM(MAC) && __MAC_OS_X_VERSION_MIN_REQUIRED < 101200
// <https://webkit.org/b/168415> El Capitan NetworkLoadTiming values are sometimes jumbled
#define DEFAULT_RESOURCE_TIMING_ENABLED false
#else
#define DEFAULT_RESOURCE_TIMING_ENABLED true
#endif

#if PLATFORM(COCOA)
#define DEFAULT_DATA_TRANSFER_ITEMS_ENABLED true
#define DEFAULT_DIRECTORY_UPLOAD_ENABLED true
#else
#define DEFAULT_DATA_TRANSFER_ITEMS_ENABLED false
#define DEFAULT_DIRECTORY_UPLOAD_ENABLED false
#endif

#if PLATFORM(COCOA)

#define DEFAULT_STANDARD_FONT_FAMILY "Times"
#define DEFAULT_FANTASY_FONT_FAMILY "Papyrus"
#define DEFAULT_FIXED_FONT_FAMILY "Courier"
#define DEFAULT_SANS_SERIF_FONT_FAMILY "Helvetica"
#define DEFAULT_SERIF_FONT_FAMILY "Times"

#if PLATFORM(IOS)
#define DEFAULT_CURSIVE_FONT_FAMILY "Snell Roundhand"
#define DEFAULT_PICTOGRAPH_FONT_FAMILY "AppleColorEmoji"
#else
#define DEFAULT_CURSIVE_FONT_FAMILY "Apple Chancery"
#define DEFAULT_PICTOGRAPH_FONT_FAMILY "Apple Color Emoji"
#endif

#else

#define DEFAULT_STANDARD_FONT_FAMILY "Times"
#define DEFAULT_CURSIVE_FONT_FAMILY "Comic Sans MS"
#define DEFAULT_FANTASY_FONT_FAMILY "Impact"
#define DEFAULT_FIXED_FONT_FAMILY "Courier New"
#define DEFAULT_SANS_SERIF_FONT_FAMILY "Helvetica"
#define DEFAULT_SERIF_FONT_FAMILY "Times"
#define DEFAULT_PICTOGRAPH_FONT_FAMILY "Times"

#endif

// Our Xcode build system does not currently have any concept of DEVELOPER_MODE.
// Cocoa ports must disable experimental features on release branches for now.
#if ENABLE(DEVELOPER_MODE) || PLATFORM(COCOA)
#define DEFAULT_EXPERIMENTAL_FEATURES_ENABLED true
#else
#define DEFAULT_EXPERIMENTAL_FEATURES_ENABLED false
#endif
