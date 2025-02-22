// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PaintController_h
#define PaintController_h

#include "platform/PlatformExport.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/geometry/IntRect.h"
#include "platform/geometry/LayoutPoint.h"
#include "platform/graphics/ContiguousContainer.h"
#include "platform/graphics/PaintInvalidationReason.h"
#include "platform/graphics/paint/DisplayItem.h"
#include "platform/graphics/paint/DisplayItemList.h"
#include "platform/graphics/paint/PaintArtifact.h"
#include "platform/graphics/paint/PaintChunk.h"
#include "platform/graphics/paint/PaintChunker.h"
#include "platform/graphics/paint/Transform3DDisplayItem.h"
#include "wtf/Alignment.h"
#include "wtf/HashMap.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/Utility.h"
#include "wtf/Vector.h"

namespace blink {

class GraphicsContext;

static const size_t kInitialDisplayItemListCapacityBytes = 512;

// Responsible for processing display items as they are produced, and producing
// a final paint artifact when complete. This class includes logic for caching,
// cache invalidation, and merging.
class PLATFORM_EXPORT PaintController {
    WTF_MAKE_NONCOPYABLE(PaintController);
    WTF_MAKE_FAST_ALLOCATED(PaintController);
public:
    static PassOwnPtr<PaintController> create()
    {
        return adoptPtr(new PaintController());
    }

    // These methods are called during paint invalidation (or paint if SlimmingPaintV2 is on).

    // If |visualRect| is not nullptr, for slimming paint v1, it contains all pixels within the GraphicsLayer
    // which might be painted into by the display item client, in coordinate space of the GraphicsLayer.
    // TODO(pdr): define it for spv2.
    // |visualRect| can be nullptr if we know it's unchanged and PaintController has cached the previous value.
    void invalidate(const DisplayItemClientWrapper&, PaintInvalidationReason, const IntRect* visualRect);
    void invalidateUntracked(DisplayItemClient);
    void invalidateAll();

    // Record when paint offsets change during paint.
    void invalidatePaintOffset(const DisplayItemClientWrapper&);
#if ENABLE(ASSERT)
    bool paintOffsetWasInvalidated(DisplayItemClient) const;
#endif

    // These methods are called during painting.

    // Provide a new set of paint chunk properties to apply to recorded display
    // items, for Slimming Paint v2.
    void updateCurrentPaintChunkProperties(const PaintChunkProperties&);

    template <typename DisplayItemClass, typename... Args>
    void createAndAppend(Args&&... args)
    {
        static_assert(WTF::IsSubclass<DisplayItemClass, DisplayItem>::value,
            "Can only createAndAppend subclasses of DisplayItem.");
        static_assert(sizeof(DisplayItemClass) <= kMaximumDisplayItemSize,
            "DisplayItem subclass is larger than kMaximumDisplayItemSize.");

        if (displayItemConstructionIsDisabled())
            return;
        DisplayItemClass& displayItem = m_newDisplayItemList.allocateAndConstruct<DisplayItemClass>(WTF::forward<Args>(args)...);
        processNewItem(displayItem);
    }

    // Creates and appends an ending display item to pair with a preceding
    // beginning item iff the display item actually draws content. For no-op
    // items, rather than creating an ending item, the begin item will
    // instead be removed, thereby maintaining brevity of the list. If display
    // item construction is disabled, no list mutations will be performed.
    template <typename DisplayItemClass, typename... Args>
    void endItem(Args&&... args)
    {
        if (displayItemConstructionIsDisabled())
            return;
        if (lastDisplayItemIsNoopBegin())
            removeLastDisplayItem();
        else
            createAndAppend<DisplayItemClass>(WTF::forward<Args>(args)...);
    }

    // Scopes must be used to avoid duplicated display item ids when we paint some object
    // multiple times and generate multiple display items with the same type.
    // We don't cache display items added in scopes.
    void beginScope();
    void endScope();

    // True if the last display item is a begin that doesn't draw content.
    bool lastDisplayItemIsNoopBegin() const;
    void removeLastDisplayItem();

    void beginSkippingCache() { ++m_skippingCacheCount; }
    void endSkippingCache() { ASSERT(m_skippingCacheCount > 0); --m_skippingCacheCount; }
    bool skippingCache() const { return m_skippingCacheCount; }

    // Must be called when a painting is finished.
    void commitNewDisplayItems();

    // Returns the approximate memory usage, excluding memory likely to be
    // shared with the embedder after copying to WebPaintController.
    // Should only be called right after commitNewDisplayItems.
    size_t approximateUnsharedMemoryUsage() const;

    // Get the artifact generated after the last commit.
    const PaintArtifact& paintArtifact() const;
    const DisplayItemList& displayItemList() const { return paintArtifact().displayItemList(); }
    const Vector<PaintChunk>& paintChunks() const { return paintArtifact().paintChunks(); }

    bool clientCacheIsValid(DisplayItemClient) const;

    bool displayItemConstructionIsDisabled() const { return m_constructionDisabled; }
    void setDisplayItemConstructionIsDisabled(const bool disable) { m_constructionDisabled = disable; }

    bool textPainted() const { return m_textPainted; }
    void setTextPainted() { m_textPainted = true; }

    // Returns displayItemList added using createAndAppend() since beginning or
    // the last commitNewDisplayItems(). Use with care.
    DisplayItemList& newDisplayItemList() { return m_newDisplayItemList; }

#ifndef NDEBUG
    void showDebugData() const;
#endif

    void startTrackingPaintInvalidationObjects()
    {
        ASSERT(RuntimeEnabledFeatures::slimmingPaintV2Enabled());
        m_trackedPaintInvalidationObjects = adoptPtr(new Vector<String>());
    }
    void stopTrackingPaintInvalidationObjects()
    {
        ASSERT(RuntimeEnabledFeatures::slimmingPaintV2Enabled());
        m_trackedPaintInvalidationObjects = nullptr;
    }
    Vector<String> trackedPaintInvalidationObjects()
    {
        ASSERT(RuntimeEnabledFeatures::slimmingPaintV2Enabled());
        return m_trackedPaintInvalidationObjects ? *m_trackedPaintInvalidationObjects : Vector<String>();
    }

    bool clientHasCheckedPaintInvalidation(DisplayItemClient client) const
    {
        ASSERT(RuntimeEnabledFeatures::slimmingPaintSynchronizedPaintingEnabled());
        return m_clientsCheckedPaintInvalidation.contains(client);
    }
    void setClientHasCheckedPaintInvalidation(DisplayItemClient client)
    {
        ASSERT(RuntimeEnabledFeatures::slimmingPaintSynchronizedPaintingEnabled());
        m_clientsCheckedPaintInvalidation.add(client);
    }

protected:
    PaintController()
        : m_newDisplayItemList(kInitialDisplayItemListCapacityBytes)
        , m_validlyCachedClientsDirty(false)
        , m_constructionDisabled(false)
        , m_textPainted(false)
        , m_skippingCacheCount(0)
        , m_numCachedItems(0)
        , m_nextScope(1) { }

private:
    // Set new item state (scopes, cache skipping, etc) for a new item.
    void processNewItem(DisplayItem&);

    void updateValidlyCachedClientsIfNeeded() const;

    void invalidateClient(const DisplayItemClientWrapper&);

#ifndef NDEBUG
    WTF::String displayItemListAsDebugString(const DisplayItemList&) const;
#endif

    // Indices into PaintList of all DrawingDisplayItems and BeginSubsequenceDisplayItems of each client.
    // Temporarily used during merge to find out-of-order display items.
    using DisplayItemIndicesByClientMap = HashMap<DisplayItemClient, Vector<size_t>>;

    static size_t findMatchingItemFromIndex(const DisplayItem::Id&, const DisplayItemIndicesByClientMap&, const DisplayItemList&);
    static void addItemToIndexIfNeeded(const DisplayItem&, size_t index, DisplayItemIndicesByClientMap&);

    struct OutOfOrderIndexContext;
    DisplayItemList::iterator findOutOfOrderCachedItem(const DisplayItem::Id&, OutOfOrderIndexContext&);
    DisplayItemList::iterator findOutOfOrderCachedItemForward(const DisplayItem::Id&, OutOfOrderIndexContext&);
    void copyCachedSubsequence(DisplayItemList::iterator& currentIt, DisplayItemList& updatedList);

#if ENABLE(ASSERT)
    // The following two methods are for checking under-invalidations
    // (when RuntimeEnabledFeatures::slimmingPaintUnderInvalidationCheckingEnabled).
    void checkUnderInvalidation(DisplayItemList::iterator& newIt, DisplayItemList::iterator& currentIt);
    void checkCachedDisplayItemIsUnchanged(const char* messagePrefix, const DisplayItem& newItem, const DisplayItem& oldItem);
    void checkNoRemainingCachedDisplayItems();
#endif

    // The last complete paint artifact.
    // In SPv2, this includes paint chunks as well as display items.
    PaintArtifact m_currentPaintArtifact;

    // Data being used to build the next paint artifact.
    DisplayItemList m_newDisplayItemList;
    PaintChunker m_newPaintChunks;

    // Contains all clients having valid cached paintings if updated.
    // It's lazily updated in updateValidlyCachedClientsIfNeeded().
    // TODO(wangxianzhu): In the future we can replace this with client-side repaint flags
    // to avoid the cost of building and querying the hash table.
    mutable HashSet<DisplayItemClient> m_validlyCachedClients;
    mutable bool m_validlyCachedClientsDirty;

    // Used during painting. Contains clients that have checked paint invalidation and
    // are known to be valid.
    // TODO(wangxianzhu): Use client side flag to avoid const of hash table.
    HashSet<DisplayItemClient> m_clientsCheckedPaintInvalidation;

#if ENABLE(ASSERT)
    // Set of clients which had paint offset changes since the last commit. This is used for
    // ensuring paint offsets are only updated once and are the same in all phases.
    HashSet<DisplayItemClient> m_clientsWithPaintOffsetInvalidations;
#endif

    // Allow display item construction to be disabled to isolate the costs of construction
    // in performance metrics.
    bool m_constructionDisabled;

    // Indicates this PaintController has ever had text. It is never reset to false.
    bool m_textPainted;

    int m_skippingCacheCount;

    int m_numCachedItems;

    unsigned m_nextScope;
    Vector<unsigned> m_scopeStack;

#if ENABLE(ASSERT)
    // Record the debug names of invalidated clients for assertion and debugging.
    Vector<String> m_invalidations;

    // This is used to check duplicated ids during add(). We could also check
    // during commitNewDisplayItems(), but checking during add() helps developer
    // easily find where the duplicated ids are from.
    DisplayItemIndicesByClientMap m_newDisplayItemIndicesByClient;
#endif

    OwnPtr<Vector<String>> m_trackedPaintInvalidationObjects;
};

} // namespace blink

#endif // PaintController_h
