/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "web/ServiceWorkerGlobalScopeProxy.h"

#include "bindings/core/v8/WorkerScriptController.h"
#include "core/dom/CrossThreadTask.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/dom/MessagePort.h"
#include "core/events/MessageEvent.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/workers/WorkerGlobalScope.h"
#include "core/workers/WorkerThread.h"
#include "modules/background_sync/SyncEvent.h"
#include "modules/background_sync/SyncRegistration.h"
#include "modules/fetch/Headers.h"
#include "modules/geofencing/CircularGeofencingRegion.h"
#include "modules/geofencing/GeofencingEvent.h"
#include "modules/navigatorconnect/AcceptConnectionObserver.h"
#include "modules/navigatorconnect/CrossOriginServiceWorkerClient.h"
#include "modules/navigatorconnect/ServicePortCollection.h"
#include "modules/navigatorconnect/WorkerNavigatorServices.h"
#include "modules/notifications/Notification.h"
#include "modules/notifications/NotificationEvent.h"
#include "modules/notifications/NotificationEventInit.h"
#include "modules/push_messaging/PushEvent.h"
#include "modules/push_messaging/PushMessageData.h"
#include "modules/serviceworkers/ExtendableEvent.h"
#include "modules/serviceworkers/FetchEvent.h"
#include "modules/serviceworkers/InstallEvent.h"
#include "modules/serviceworkers/ServiceWorkerGlobalScope.h"
#include "modules/serviceworkers/WaitUntilObserver.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "public/platform/WebCrossOriginServiceWorkerClient.h"
#include "public/platform/modules/notifications/WebNotificationData.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerEventResult.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerRequest.h"
#include "public/web/WebSerializedScriptValue.h"
#include "public/web/modules/serviceworker/WebServiceWorkerContextClient.h"
#include "web/WebEmbeddedWorkerImpl.h"
#include "wtf/Assertions.h"
#include "wtf/Functional.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

PassOwnPtr<ServiceWorkerGlobalScopeProxy> ServiceWorkerGlobalScopeProxy::create(WebEmbeddedWorkerImpl& embeddedWorker, Document& document, WebServiceWorkerContextClient& client)
{
    return adoptPtr(new ServiceWorkerGlobalScopeProxy(embeddedWorker, document, client));
}

ServiceWorkerGlobalScopeProxy::~ServiceWorkerGlobalScopeProxy()
{
}

void ServiceWorkerGlobalScopeProxy::setRegistration(WebPassOwnPtr<WebServiceWorkerRegistration::Handle> handle)
{
    ASSERT(m_workerGlobalScope);
    m_workerGlobalScope->setRegistration(handle);
}

void ServiceWorkerGlobalScopeProxy::dispatchActivateEvent(int eventID)
{
    ASSERT(m_workerGlobalScope);
    WaitUntilObserver* observer = WaitUntilObserver::create(m_workerGlobalScope, WaitUntilObserver::Activate, eventID);
    RefPtrWillBeRawPtr<Event> event(ExtendableEvent::create(EventTypeNames::activate, ExtendableEventInit(), observer));
    m_workerGlobalScope->dispatchExtendableEvent(event.release(), observer);
}

void ServiceWorkerGlobalScopeProxy::dispatchFetchEvent(int eventID, const WebServiceWorkerRequest& webRequest)
{
    ASSERT(m_workerGlobalScope);
    RespondWithObserver* observer = RespondWithObserver::create(m_workerGlobalScope, eventID, webRequest.url(), webRequest.mode(), webRequest.frameType(), webRequest.requestContext());
    bool defaultPrevented = false;
    Request* request = Request::create(m_workerGlobalScope, webRequest);
    request->headers()->setGuard(Headers::ImmutableGuard);
    FetchEventInit eventInit;
    eventInit.setCancelable(true);
    eventInit.setRequest(request);
    eventInit.setIsReload(webRequest.isReload());
    RefPtrWillBeRawPtr<FetchEvent> fetchEvent(FetchEvent::create(EventTypeNames::fetch, eventInit, observer));
    defaultPrevented = !m_workerGlobalScope->dispatchEvent(fetchEvent.release());
    observer->didDispatchEvent(defaultPrevented);
}

void ServiceWorkerGlobalScopeProxy::dispatchGeofencingEvent(int eventID, WebGeofencingEventType eventType, const WebString& regionID, const WebCircularGeofencingRegion& region)
{
    ASSERT(m_workerGlobalScope);
    const AtomicString& type = eventType == WebGeofencingEventTypeEnter ? EventTypeNames::geofenceenter : EventTypeNames::geofenceleave;
    m_workerGlobalScope->dispatchEvent(GeofencingEvent::create(type, regionID, CircularGeofencingRegion::create(regionID, region)));
}

void ServiceWorkerGlobalScopeProxy::dispatchInstallEvent(int eventID)
{
    ASSERT(m_workerGlobalScope);
    WaitUntilObserver* observer = WaitUntilObserver::create(m_workerGlobalScope, WaitUntilObserver::Install, eventID);
    RefPtrWillBeRawPtr<Event> event;
    if (RuntimeEnabledFeatures::foreignFetchEnabled()) {
        event = InstallEvent::create(EventTypeNames::install, ExtendableEventInit(), observer);
    } else {
        event = ExtendableEvent::create(EventTypeNames::install, ExtendableEventInit(), observer);
    }
    m_workerGlobalScope->dispatchExtendableEvent(event.release(), observer);
}

void ServiceWorkerGlobalScopeProxy::dispatchMessageEvent(const WebString& message, const WebMessagePortChannelArray& webChannels)
{
    ASSERT(m_workerGlobalScope);

    MessagePortArray* ports = MessagePort::toMessagePortArray(m_workerGlobalScope, webChannels);
    WebSerializedScriptValue value = WebSerializedScriptValue::fromString(message);
    m_workerGlobalScope->dispatchEvent(MessageEvent::create(ports, value));
}

void ServiceWorkerGlobalScopeProxy::dispatchNotificationClickEvent(int eventID, int64_t notificationID, const WebNotificationData& data, int actionIndex)
{
    ASSERT(m_workerGlobalScope);
    WaitUntilObserver* observer = WaitUntilObserver::create(m_workerGlobalScope, WaitUntilObserver::NotificationClick, eventID);
    NotificationEventInit eventInit;
    eventInit.setNotification(Notification::create(m_workerGlobalScope, notificationID, data));
    if (0 <= actionIndex && actionIndex < static_cast<int>(data.actions.size()))
        eventInit.setAction(data.actions[actionIndex].action);
    RefPtrWillBeRawPtr<Event> event(NotificationEvent::create(EventTypeNames::notificationclick, eventInit, observer));
    m_workerGlobalScope->dispatchExtendableEvent(event.release(), observer);
}

void ServiceWorkerGlobalScopeProxy::dispatchPushEvent(int eventID, const WebString& data)
{
    ASSERT(m_workerGlobalScope);
    WaitUntilObserver* observer = WaitUntilObserver::create(m_workerGlobalScope, WaitUntilObserver::Push, eventID);
    RefPtrWillBeRawPtr<Event> event(PushEvent::create(EventTypeNames::push, PushMessageData::create(data), observer));
    m_workerGlobalScope->dispatchExtendableEvent(event.release(), observer);
}

void ServiceWorkerGlobalScopeProxy::dispatchServicePortConnectEvent(WebServicePortConnectEventCallbacks* rawCallbacks, const WebURL& targetURL, const WebString& origin, WebServicePortID portID)
{
    ASSERT(m_workerGlobalScope);
    OwnPtr<WebServicePortConnectEventCallbacks> callbacks = adoptPtr(rawCallbacks);
    ServicePortCollection* collection = WorkerNavigatorServices::services(m_workerGlobalScope, *m_workerGlobalScope->navigator());
    collection->dispatchConnectEvent(callbacks.release(), targetURL, origin, portID);
}

void ServiceWorkerGlobalScopeProxy::dispatchSyncEvent(int eventID, const WebSyncRegistration& registration)
{
    ASSERT(m_workerGlobalScope);
    if (!RuntimeEnabledFeatures::backgroundSyncEnabled()) {
        ServiceWorkerGlobalScopeClient::from(m_workerGlobalScope)->didHandleSyncEvent(eventID, WebServiceWorkerEventResultCompleted);
        return;
    }
    WaitUntilObserver* observer = WaitUntilObserver::create(m_workerGlobalScope, WaitUntilObserver::Sync, eventID);
    RefPtrWillBeRawPtr<Event> event(SyncEvent::create(EventTypeNames::sync, SyncRegistration::create(registration, m_workerGlobalScope->registration()), observer));
    m_workerGlobalScope->dispatchExtendableEvent(event.release(), observer);
}

void ServiceWorkerGlobalScopeProxy::dispatchCrossOriginMessageEvent(const WebCrossOriginServiceWorkerClient& webClient, const WebString& message, const WebMessagePortChannelArray& webChannels)
{
    ASSERT(m_workerGlobalScope);
    MessagePortArray* ports = MessagePort::toMessagePortArray(m_workerGlobalScope, webChannels);
    WebSerializedScriptValue value = WebSerializedScriptValue::fromString(message);
    // FIXME: Have proper source for this MessageEvent.
    RefPtrWillBeRawPtr<MessageEvent> event = MessageEvent::create(ports, value, webClient.origin.string());
    event->setType(EventTypeNames::crossoriginmessage);
    m_workerGlobalScope->dispatchEvent(event);
}

void ServiceWorkerGlobalScopeProxy::reportException(const String& errorMessage, int lineNumber, int columnNumber, const String& sourceURL, int)
{
    m_client.reportException(errorMessage, lineNumber, columnNumber, sourceURL);
}

void ServiceWorkerGlobalScopeProxy::reportConsoleMessage(PassRefPtrWillBeRawPtr<ConsoleMessage> consoleMessage)
{
    m_client.reportConsoleMessage(consoleMessage->source(), consoleMessage->level(), consoleMessage->message(), consoleMessage->lineNumber(), consoleMessage->url());
}

void ServiceWorkerGlobalScopeProxy::postMessageToPageInspector(const String& message)
{
    m_document.postInspectorTask(BLINK_FROM_HERE, createCrossThreadTask(&WebEmbeddedWorkerImpl::postMessageToPageInspector, &m_embeddedWorker, message));
}

void ServiceWorkerGlobalScopeProxy::didEvaluateWorkerScript(bool success)
{
    m_client.didEvaluateWorkerScript(success);
}

void ServiceWorkerGlobalScopeProxy::didInitializeWorkerContext()
{
    ASSERT(m_workerGlobalScope);
    ScriptState::Scope scope(m_workerGlobalScope->script()->scriptState());
    m_client.didInitializeWorkerContext(m_workerGlobalScope->script()->context(), WebURL(m_documentURL));
}

void ServiceWorkerGlobalScopeProxy::workerGlobalScopeStarted(WorkerGlobalScope* workerGlobalScope)
{
    ASSERT(!m_workerGlobalScope);
    m_workerGlobalScope = static_cast<ServiceWorkerGlobalScope*>(workerGlobalScope);
    m_client.workerContextStarted(this);
}

void ServiceWorkerGlobalScopeProxy::workerGlobalScopeClosed()
{
    m_document.postTask(BLINK_FROM_HERE, createCrossThreadTask(&WebEmbeddedWorkerImpl::terminateWorkerContext, &m_embeddedWorker));
}

void ServiceWorkerGlobalScopeProxy::willDestroyWorkerGlobalScope()
{
    v8::HandleScope handleScope(m_workerGlobalScope->thread()->isolate());
    m_client.willDestroyWorkerContext(m_workerGlobalScope->script()->context());
    m_workerGlobalScope = nullptr;
}

void ServiceWorkerGlobalScopeProxy::workerThreadTerminated()
{
    m_client.workerContextDestroyed();
}

ServiceWorkerGlobalScopeProxy::ServiceWorkerGlobalScopeProxy(WebEmbeddedWorkerImpl& embeddedWorker, Document& document, WebServiceWorkerContextClient& client)
    : m_embeddedWorker(embeddedWorker)
    , m_document(document)
    , m_documentURL(document.url().copy())
    , m_client(client)
    , m_workerGlobalScope(nullptr)
{
}

} // namespace blink
