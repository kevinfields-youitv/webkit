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

#include "config.h"

#if ENABLE(SERVICE_WORKER)
#include "ServiceWorkerWindowClient.h"

#include "JSDOMPromiseDeferred.h"

namespace WebCore {

ServiceWorkerWindowClient::ServiceWorkerWindowClient(ScriptExecutionContext& context, const Identifier& identifier)
    : ServiceWorkerClient(context, identifier, Type::Window)
{
}

VisibilityState ServiceWorkerWindowClient::visibilityState() const
{
    return VisibilityState::Visible;
}

bool ServiceWorkerWindowClient::isFocused() const
{
    return true;
}

void ServiceWorkerWindowClient::focus(Ref<DeferredPromise>&& promise)
{
    promise->reject(Exception { NotSupportedError, ASCIILiteral("windowClient.focus() is not yet supported") });
}

void ServiceWorkerWindowClient::navigate(const String& url, Ref<DeferredPromise>&& promise)
{
    UNUSED_PARAM(url);
    promise->reject(Exception { NotSupportedError, ASCIILiteral("windowClient.navigate() is not yet supported") });
}

} // namespace WebCore

#endif // ENABLE(SERVICE_WORKER)
