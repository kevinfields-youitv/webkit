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
#include "WebSWContextManagerConnection.h"

#if ENABLE(SERVICE_WORKER)

#include "DataReference.h"
#include "Logging.h"
#include "StorageProcessMessages.h"
#include "WebCacheStorageProvider.h"
#include "WebCoreArgumentCoders.h"
#include "WebDocumentLoader.h"
#include "WebPreferencesKeys.h"
#include "WebPreferencesStore.h"
#include "WebProcess.h"
#include "WebServiceWorkerFetchTaskClient.h"
#include "WebSocketProvider.h"
#include <WebCore/EditorClient.h>
#include <WebCore/EmptyClients.h>
#include <WebCore/EmptyFrameLoaderClient.h>
#include <WebCore/LibWebRTCProvider.h>
#include <WebCore/PageConfiguration.h>
#include <WebCore/RuntimeEnabledFeatures.h>
#include <WebCore/SerializedScriptValue.h>
#include <pal/SessionID.h>

#if USE(QUICK_LOOK)
#include <WebCore/PreviewLoaderClient.h>
#endif

using namespace PAL;
using namespace WebCore;

namespace WebKit {

class ServiceWorkerFrameLoaderClient final : public EmptyFrameLoaderClient {
public:
    ServiceWorkerFrameLoaderClient(PAL::SessionID sessionID, uint64_t pageID, uint64_t frameID)
        : m_sessionID(sessionID)
        , m_pageID(pageID)
        , m_frameID(frameID)
    {
    }

private:
    Ref<DocumentLoader> createDocumentLoader(const ResourceRequest& request, const SubstituteData& substituteData) final
    {
        return WebDocumentLoader::create(request, substituteData);
    }

    PAL::SessionID sessionID() const final { return m_sessionID; }
    uint64_t pageID() const final { return m_pageID; }
    uint64_t frameID() const final { return m_frameID; }

    PAL::SessionID m_sessionID;
    uint64_t m_pageID { 0 };
    uint64_t m_frameID { 0 };
};

WebSWContextManagerConnection::WebSWContextManagerConnection(Ref<IPC::Connection>&& connection, uint64_t pageID, const WebPreferencesStore& store)
    : m_connectionToStorageProcess(WTFMove(connection))
    , m_pageID(pageID)
{
    updatePreferences(store);
}

void WebSWContextManagerConnection::updatePreferences(const WebPreferencesStore& store)
{
    RuntimeEnabledFeatures::sharedFeatures().setServiceWorkerEnabled(true);
    RuntimeEnabledFeatures::sharedFeatures().setCacheAPIEnabled(store.getBoolValueForKey(WebPreferencesKey::cacheAPIEnabledKey()));
    RuntimeEnabledFeatures::sharedFeatures().setFetchAPIEnabled(store.getBoolValueForKey(WebPreferencesKey::fetchAPIEnabledKey()));
}

void WebSWContextManagerConnection::startServiceWorker(uint64_t serverConnectionIdentifier, const ServiceWorkerContextData& data)
{
    // FIXME: Provide a sensical session ID.
    auto sessionID = PAL::SessionID::defaultSessionID();

    PageConfiguration pageConfiguration {
        createEmptyEditorClient(),
        WebSocketProvider::create(),
        WebCore::LibWebRTCProvider::create(),
        WebProcess::singleton().cacheStorageProvider()
    };
    fillWithEmptyClients(pageConfiguration);
    auto frameLoaderClient = std::make_unique<ServiceWorkerFrameLoaderClient>(sessionID, m_pageID, ++m_previousServiceWorkerID);
    pageConfiguration.loaderClientForMainFrame = frameLoaderClient.release();

    auto serviceWorkerThreadProxy = ServiceWorkerThreadProxy::create(WTFMove(pageConfiguration), serverConnectionIdentifier, data, sessionID, WebProcess::singleton().cacheStorageProvider());
    auto serviceWorkerIdentifier = serviceWorkerThreadProxy->identifier();
    SWContextManager::singleton().registerServiceWorkerThread(WTFMove(serviceWorkerThreadProxy));

    LOG(ServiceWorker, "Context process PID: %i started worker thread %s\n", getpid(), data.workerID.utf8().data());

    m_connectionToStorageProcess->send(Messages::StorageProcess::ServiceWorkerContextStarted(serverConnectionIdentifier, data.registrationKey, serviceWorkerIdentifier, data.workerID), 0);
}

void WebSWContextManagerConnection::startFetch(uint64_t serverConnectionIdentifier, uint64_t fetchIdentifier, uint64_t serviceWorkerIdentifier, ResourceRequest&& request, FetchOptions&& options)
{
    auto* serviceWorkerThreadProxy = SWContextManager::singleton().serviceWorkerThreadProxy(serviceWorkerIdentifier);
    if (!serviceWorkerThreadProxy) {
        m_connectionToStorageProcess->send(Messages::StorageProcess::DidNotHandleFetch(serverConnectionIdentifier, fetchIdentifier), 0);
        return;
    }

    auto client = WebServiceWorkerFetchTaskClient::create(m_connectionToStorageProcess.copyRef(), serverConnectionIdentifier, fetchIdentifier);
    serviceWorkerThreadProxy->thread().postFetchTask(WTFMove(client), WTFMove(request), WTFMove(options));
}

void WebSWContextManagerConnection::postMessageToServiceWorkerGlobalScope(uint64_t destinationServiceWorkerIdentifier, const IPC::DataReference& message, const ServiceWorkerClientIdentifier& sourceIdentifier, const String& sourceOrigin)
{
    SWContextManager::singleton().postMessageToServiceWorkerGlobalScope(destinationServiceWorkerIdentifier, SerializedScriptValue::adopt(message.vector()), sourceIdentifier, sourceOrigin);
}

void WebSWContextManagerConnection::postMessageToServiceWorkerClient(const ServiceWorkerClientIdentifier& destinationIdentifier, Ref<SerializedScriptValue>&& message, uint64_t sourceServiceWorkerIdentifier, const String& sourceOrigin)
{
    m_connectionToStorageProcess->send(Messages::StorageProcess::PostMessageToServiceWorkerClient(destinationIdentifier, IPC::DataReference { message->data() }, sourceServiceWorkerIdentifier, sourceOrigin), 0);
}

} // namespace WebCore

#endif // ENABLE(SERVICE_WORKER)
