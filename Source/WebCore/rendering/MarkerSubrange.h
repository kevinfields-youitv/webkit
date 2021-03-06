/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#include <wtf/Vector.h>

namespace WebCore {

class RenderedDocumentMarker;

struct MarkerSubrange {
    // Sorted by paint order
    enum Type {
        Unmarked,
        GrammarError,
        Correction,
        SpellingError,
        TextMatch,
        DictationAlternatives,
#if PLATFORM(IOS)
        // FIXME: See <rdar://problem/8933352>. Also, remove the PLATFORM(IOS)-guard.
        DictationPhraseWithAlternatives,
#endif
    };
#if !COMPILER_SUPPORTS(NSDMI_FOR_AGGREGATES)
    MarkerSubrange() = default;
    MarkerSubrange(unsigned startOffset, unsigned endOffset, Type type, const RenderedDocumentMarker* marker = nullptr)
        : startOffset { startOffset }
        , endOffset { endOffset }
        , type { type }
        , marker { marker }
    {
    }
#endif
    unsigned startOffset;
    unsigned endOffset;
    Type type;
    const RenderedDocumentMarker* marker { nullptr };
};

enum class OverlapStrategy { None, Frontmost };
WEBCORE_EXPORT Vector<MarkerSubrange> subdivide(const Vector<MarkerSubrange>&, OverlapStrategy = OverlapStrategy::None);

}

