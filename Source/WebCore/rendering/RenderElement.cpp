/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2005 Allan Sandfeld Jensen (kde@carewolf.com)
 *           (C) 2005, 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2013, 2015 Apple Inc. All rights reserved.
 * Copyright (C) 2010, 2012 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "RenderElement.h"

#include "AXObjectCache.h"
#include "ContentData.h"
#include "CursorList.h"
#include "ElementChildIterator.h"
#include "EventHandler.h"
#include "FocusController.h"
#include "Frame.h"
#include "FrameSelection.h"
#include "HTMLAnchorElement.h"
#include "HTMLBodyElement.h"
#include "HTMLHtmlElement.h"
#include "HTMLImageElement.h"
#include "HTMLNames.h"
#include "Logging.h"
#include "Page.h"
#include "PathUtilities.h"
#include "RenderBlock.h"
#include "RenderChildIterator.h"
#include "RenderCounter.h"
#include "RenderDeprecatedFlexibleBox.h"
#include "RenderDescendantIterator.h"
#include "RenderFlexibleBox.h"
#include "RenderFragmentedFlow.h"
#include "RenderImage.h"
#include "RenderImageResourceStyleImage.h"
#include "RenderInline.h"
#include "RenderIterator.h"
#include "RenderLayer.h"
#include "RenderLayerCompositor.h"
#include "RenderLineBreak.h"
#include "RenderListItem.h"
#if !ASSERT_DISABLED
#include "RenderListMarker.h"
#endif
#include "RenderFragmentContainer.h"
#include "RenderTableCaption.h"
#include "RenderTableCell.h"
#include "RenderTableCol.h"
#include "RenderTableRow.h"
#include "RenderText.h"
#include "RenderTheme.h"
#include "RenderView.h"
#include "SVGRenderSupport.h"
#include "Settings.h"
#include "ShadowRoot.h"
#include "StylePendingResources.h"
#include "StyleResolver.h"
#include "TextAutoSizing.h"
#include <wtf/MathExtras.h>
#include <wtf/StackStats.h>

#include "RenderGrid.h"

namespace WebCore {

struct SameSizeAsRenderElement : public RenderObject {
    uint32_t bitfields;
    void* firstChild;
    void* lastChild;
    RenderStyle style;
};

static_assert(sizeof(RenderElement) == sizeof(SameSizeAsRenderElement), "RenderElement should stay small");

bool RenderElement::s_affectsParentBlock = false;
bool RenderElement::s_noLongerAffectsParentBlock = false;
    
inline RenderElement::RenderElement(ContainerNode& elementOrDocument, RenderStyle&& style, BaseTypeFlags baseTypeFlags)
    : RenderObject(elementOrDocument)
    , m_baseTypeFlags(baseTypeFlags)
    , m_ancestorLineBoxDirty(false)
    , m_hasInitializedStyle(false)
    , m_renderInlineAlwaysCreatesLineBoxes(false)
    , m_renderBoxNeedsLazyRepaint(false)
    , m_hasPausedImageAnimations(false)
    , m_hasCounterNodeMap(false)
    , m_hasContinuation(false)
    , m_isContinuation(false)
    , m_hasValidCachedFirstLineStyle(false)
    , m_renderBlockHasMarginBeforeQuirk(false)
    , m_renderBlockHasMarginAfterQuirk(false)
    , m_renderBlockShouldForceRelayoutChildren(false)
    , m_renderBlockFlowHasMarkupTruncation(false)
    , m_renderBlockFlowLineLayoutPath(RenderBlockFlow::UndeterminedPath)
    , m_isRegisteredForVisibleInViewportCallback(false)
    , m_visibleInViewportState(static_cast<unsigned>(VisibleInViewportState::Unknown))
    , m_firstChild(nullptr)
    , m_lastChild(nullptr)
    , m_style(WTFMove(style))
{
}

RenderElement::RenderElement(Element& element, RenderStyle&& style, BaseTypeFlags baseTypeFlags)
    : RenderElement(static_cast<ContainerNode&>(element), WTFMove(style), baseTypeFlags)
{
}

RenderElement::RenderElement(Document& document, RenderStyle&& style, BaseTypeFlags baseTypeFlags)
    : RenderElement(static_cast<ContainerNode&>(document), WTFMove(style), baseTypeFlags)
{
}

RenderElement::~RenderElement()
{
    // Do not add any code here. Add it to willBeDestroyed() instead.
}

RenderPtr<RenderElement> RenderElement::createFor(Element& element, RenderStyle&& style, RendererCreationType creationType)
{
    // Minimal support for content properties replacing an entire element.
    // Works only if we have exactly one piece of content and it's a URL.
    // Otherwise acts as if we didn't support this feature.
    const ContentData* contentData = style.contentData();
    if (creationType == CreateAllRenderers && contentData && !contentData->next() && is<ImageContentData>(*contentData) && !element.isPseudoElement()) {
        Style::loadPendingResources(style, element.document(), &element);
        auto& styleImage = downcast<ImageContentData>(*contentData).image();
        auto image = createRenderer<RenderImage>(element, WTFMove(style), const_cast<StyleImage*>(&styleImage));
        image->setIsGeneratedContent();
        return WTFMove(image);
    }

    switch (style.display()) {
    case NONE:
    case CONTENTS:
        return nullptr;
    case INLINE:
        if (creationType == CreateAllRenderers)
            return createRenderer<RenderInline>(element, WTFMove(style));
        FALLTHROUGH; // Fieldsets should make a block flow if display:inline is set.
    case BLOCK:
    case INLINE_BLOCK:
    case COMPACT:
        return createRenderer<RenderBlockFlow>(element, WTFMove(style));
    case LIST_ITEM:
        return createRenderer<RenderListItem>(element, WTFMove(style));
    case FLEX:
    case INLINE_FLEX:
    case WEBKIT_FLEX:
    case WEBKIT_INLINE_FLEX:
        return createRenderer<RenderFlexibleBox>(element, WTFMove(style));
    case GRID:
    case INLINE_GRID:
        return createRenderer<RenderGrid>(element, WTFMove(style));
    case BOX:
    case INLINE_BOX:
        return createRenderer<RenderDeprecatedFlexibleBox>(element, WTFMove(style));
    default: {
        if (creationType == OnlyCreateBlockAndFlexboxRenderers)
            return createRenderer<RenderBlockFlow>(element, WTFMove(style));
        switch (style.display()) {
        case TABLE:
        case INLINE_TABLE:
            return createRenderer<RenderTable>(element, WTFMove(style));
        case TABLE_CELL:
            return createRenderer<RenderTableCell>(element, WTFMove(style));
        case TABLE_CAPTION:
            return createRenderer<RenderTableCaption>(element, WTFMove(style));
        case TABLE_ROW_GROUP:
        case TABLE_HEADER_GROUP:
        case TABLE_FOOTER_GROUP:
            return createRenderer<RenderTableSection>(element, WTFMove(style));
        case TABLE_ROW:
            return createRenderer<RenderTableRow>(element, WTFMove(style));
        case TABLE_COLUMN_GROUP:
        case TABLE_COLUMN:
            return createRenderer<RenderTableCol>(element, WTFMove(style));
        default:
            break;
        }
        break;
    }
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

std::unique_ptr<RenderStyle> RenderElement::computeFirstLineStyle() const
{
    ASSERT(view().usesFirstLineRules());

    RenderElement& rendererForFirstLineStyle = isBeforeOrAfterContent() ? *parent() : const_cast<RenderElement&>(*this);

    if (rendererForFirstLineStyle.isRenderBlockFlow() || rendererForFirstLineStyle.isRenderButton()) {
        RenderBlock* firstLineBlock = rendererForFirstLineStyle.firstLineBlock();
        if (!firstLineBlock)
            return nullptr;
        auto* firstLineStyle = firstLineBlock->getCachedPseudoStyle(FIRST_LINE, &style());
        if (!firstLineStyle)
            return nullptr;
        return RenderStyle::clonePtr(*firstLineStyle);
    }

    if (!rendererForFirstLineStyle.isRenderInline())
        return nullptr;

    auto& parentStyle = rendererForFirstLineStyle.parent()->firstLineStyle();
    if (&parentStyle == &rendererForFirstLineStyle.parent()->style())
        return nullptr;

    if (rendererForFirstLineStyle.isAnonymous()) {
        auto* textRendererWithDisplayContentsParent = RenderText::findByDisplayContentsInlineWrapperCandidate(rendererForFirstLineStyle);
        if (!textRendererWithDisplayContentsParent)
            return nullptr;
        auto* composedTreeParentElement = textRendererWithDisplayContentsParent->textNode()->parentElementInComposedTree();
        if (!composedTreeParentElement)
            return nullptr;

        auto style = composedTreeParentElement->styleResolver().styleForElement(*composedTreeParentElement, &parentStyle).renderStyle;
        ASSERT(style->display() == CONTENTS);

        // We act as if there was an unstyled <span> around the text node. Only styling happens via inheritance.
        auto firstLineStyle = RenderStyle::createPtr();
        firstLineStyle->inheritFrom(*style);
        return firstLineStyle;
    }

    return rendererForFirstLineStyle.element()->styleResolver().styleForElement(*element(), &parentStyle).renderStyle;
}

const RenderStyle& RenderElement::firstLineStyle() const
{
    if (!view().usesFirstLineRules())
        return style();

    if (!m_hasValidCachedFirstLineStyle) {
        auto firstLineStyle = computeFirstLineStyle();
        if (firstLineStyle || hasRareData())
            const_cast<RenderElement&>(*this).ensureRareData().cachedFirstLineStyle = WTFMove(firstLineStyle);
        m_hasValidCachedFirstLineStyle = true;
    }

    return (hasRareData() && rareData().cachedFirstLineStyle) ? *rareData().cachedFirstLineStyle : style();
}

StyleDifference RenderElement::adjustStyleDifference(StyleDifference diff, unsigned contextSensitiveProperties) const
{
    // If transform changed, and we are not composited, need to do a layout.
    if (contextSensitiveProperties & ContextSensitivePropertyTransform) {
        // FIXME: when transforms are taken into account for overflow, we will need to do a layout.
        if (!hasLayer() || !downcast<RenderLayerModelObject>(*this).layer()->isComposited()) {
            if (!hasLayer())
                diff = std::max(diff, StyleDifferenceLayout);
            else {
                // We need to set at least SimplifiedLayout, but if PositionedMovementOnly is already set
                // then we actually need SimplifiedLayoutAndPositionedMovement.
                diff = std::max(diff, (diff == StyleDifferenceLayoutPositionedMovementOnly) ? StyleDifferenceSimplifiedLayoutAndPositionedMovement : StyleDifferenceSimplifiedLayout);
            }
        
        } else
            diff = std::max(diff, StyleDifferenceRecompositeLayer);
    }

    if (contextSensitiveProperties & ContextSensitivePropertyOpacity) {
        if (!hasLayer() || !downcast<RenderLayerModelObject>(*this).layer()->isComposited())
            diff = std::max(diff, StyleDifferenceRepaintLayer);
        else
            diff = std::max(diff, StyleDifferenceRecompositeLayer);
    }

    if (contextSensitiveProperties & ContextSensitivePropertyClipPath) {
        if (hasLayer()
            && downcast<RenderLayerModelObject>(*this).layer()->isComposited()
            && hasClipPath()
            && RenderLayerCompositor::canCompositeClipPath(*downcast<RenderLayerModelObject>(*this).layer()))
            diff = std::max(diff, StyleDifferenceRecompositeLayer);
        else
            diff = std::max(diff, StyleDifferenceRepaint);
    }
    
    if (contextSensitiveProperties & ContextSensitivePropertyWillChange) {
        if (style().willChange() && style().willChange()->canTriggerCompositing())
            diff = std::max(diff, StyleDifferenceRecompositeLayer);
    }
    
    if ((contextSensitiveProperties & ContextSensitivePropertyFilter) && hasLayer()) {
        auto& layer = *downcast<RenderLayerModelObject>(*this).layer();
        if (!layer.isComposited() || layer.paintsWithFilters())
            diff = std::max(diff, StyleDifferenceRepaintLayer);
        else
            diff = std::max(diff, StyleDifferenceRecompositeLayer);
    }
    
    // The answer to requiresLayer() for plugins, iframes, and canvas can change without the actual
    // style changing, since it depends on whether we decide to composite these elements. When the
    // layer status of one of these elements changes, we need to force a layout.
    if (diff < StyleDifferenceLayout && isRenderLayerModelObject()) {
        if (hasLayer() != downcast<RenderLayerModelObject>(*this).requiresLayer())
            diff = StyleDifferenceLayout;
    }

    // If we have no layer(), just treat a RepaintLayer hint as a normal Repaint.
    if (diff == StyleDifferenceRepaintLayer && !hasLayer())
        diff = StyleDifferenceRepaint;

    return diff;
}

inline bool RenderElement::hasImmediateNonWhitespaceTextChildOrBorderOrOutline() const
{
    for (auto& child : childrenOfType<RenderObject>(*this)) {
        if (is<RenderText>(child) && !downcast<RenderText>(child).isAllCollapsibleWhitespace())
            return true;
        if (child.style().hasOutline() || child.style().hasBorder())
            return true;
    }
    return false;
}

inline bool RenderElement::shouldRepaintForStyleDifference(StyleDifference diff) const
{
    return diff == StyleDifferenceRepaint || (diff == StyleDifferenceRepaintIfTextOrBorderOrOutline && hasImmediateNonWhitespaceTextChildOrBorderOrOutline());
}

void RenderElement::updateFillImages(const FillLayer* oldLayers, const FillLayer& newLayers)
{
    // Optimize the common case.
    if (FillLayer::imagesIdentical(oldLayers, &newLayers))
        return;
    
    // Add before removing, to avoid removing all clients of an image that is in both sets.
    for (auto* layer = &newLayers; layer; layer = layer->next()) {
        if (layer->image())
            layer->image()->addClient(this);
    }
    for (auto* layer = oldLayers; layer; layer = layer->next()) {
        if (layer->image())
            layer->image()->removeClient(this);
    }
}

void RenderElement::updateImage(StyleImage* oldImage, StyleImage* newImage)
{
    if (oldImage == newImage)
        return;
    if (oldImage)
        oldImage->removeClient(this);
    if (newImage)
        newImage->addClient(this);
}

void RenderElement::updateShapeImage(const ShapeValue* oldShapeValue, const ShapeValue* newShapeValue)
{
    if (oldShapeValue || newShapeValue)
        updateImage(oldShapeValue ? oldShapeValue->image() : nullptr, newShapeValue ? newShapeValue->image() : nullptr);
}

void RenderElement::initializeStyle()
{
    Style::loadPendingResources(m_style, document(), element());

    styleWillChange(StyleDifferenceNewStyle, style());
    m_hasInitializedStyle = true;
    styleDidChange(StyleDifferenceNewStyle, nullptr);

    // We shouldn't have any text children that would need styleDidChange at this point.
    ASSERT(!childrenOfType<RenderText>(*this).first());

    // It would be nice to assert that !parent() here, but some RenderLayer subrenderers
    // have their parent set before getting a call to initializeStyle() :|
}

void RenderElement::setStyle(RenderStyle&& style, StyleDifference minimalStyleDifference)
{
    // FIXME: Should change RenderView so it can use initializeStyle too.
    // If we do that, we can assert m_hasInitializedStyle unconditionally,
    // and remove the check of m_hasInitializedStyle below too.
    ASSERT(m_hasInitializedStyle || isRenderView());

    StyleDifference diff = StyleDifferenceEqual;
    unsigned contextSensitiveProperties = ContextSensitivePropertyNone;
    if (m_hasInitializedStyle)
        diff = m_style.diff(style, contextSensitiveProperties);

    diff = std::max(diff, minimalStyleDifference);

    diff = adjustStyleDifference(diff, contextSensitiveProperties);

    Style::loadPendingResources(style, document(), element());

    styleWillChange(diff, style);
    auto oldStyle = m_style.replace(WTFMove(style));
    bool detachedFromParent = !parent();

    // Make sure we invalidate the containing block cache for flows when the contianing block context changes
    // so that styleDidChange can safely use RenderBlock::locateEnclosingFragmentedFlow()
    if (oldStyle.position() != m_style.position())
        adjustFragmentedFlowStateOnContainingBlockChangeIfNeeded();

    styleDidChange(diff, &oldStyle);

    // Text renderers use their parent style. Notify them about the change.
    for (auto& child : childrenOfType<RenderText>(*this))
        child.styleDidChange(diff, &oldStyle);

    // FIXME: |this| might be destroyed here. This can currently happen for a RenderTextFragment when
    // its first-letter block gets an update in RenderTextFragment::styleDidChange. For RenderTextFragment(s),
    // we will safely bail out with the detachedFromParent flag. We might want to broaden this condition
    // in the future as we move renderer changes out of layout and into style changes.
    if (detachedFromParent)
        return;

    // Now that the layer (if any) has been updated, we need to adjust the diff again,
    // check whether we should layout now, and decide if we need to repaint.
    StyleDifference updatedDiff = adjustStyleDifference(diff, contextSensitiveProperties);
    
    if (diff <= StyleDifferenceLayoutPositionedMovementOnly) {
        if (updatedDiff == StyleDifferenceLayout)
            setNeedsLayoutAndPrefWidthsRecalc();
        else if (updatedDiff == StyleDifferenceLayoutPositionedMovementOnly)
            setNeedsPositionedMovementLayout(&oldStyle);
        else if (updatedDiff == StyleDifferenceSimplifiedLayoutAndPositionedMovement) {
            setNeedsPositionedMovementLayout(&oldStyle);
            setNeedsSimplifiedNormalFlowLayout();
        } else if (updatedDiff == StyleDifferenceSimplifiedLayout)
            setNeedsSimplifiedNormalFlowLayout();
    }

    if (updatedDiff == StyleDifferenceRepaintLayer || shouldRepaintForStyleDifference(updatedDiff)) {
        // Do a repaint with the new style now, e.g., for example if we go from
        // not having an outline to having an outline.
        repaint();
    }
}

bool RenderElement::childRequiresTable(const RenderObject& child) const
{
    if (is<RenderTableCol>(child)) {
        const RenderTableCol& newTableColumn = downcast<RenderTableCol>(child);
        bool isColumnInColumnGroup = newTableColumn.isTableColumn() && is<RenderTableCol>(*this);
        return !is<RenderTable>(*this) && !isColumnInColumnGroup;
    }
    if (is<RenderTableCaption>(child))
        return !is<RenderTable>(*this);

    if (is<RenderTableSection>(child))
        return !is<RenderTable>(*this);

    if (is<RenderTableRow>(child))
        return !is<RenderTableSection>(*this);

    if (is<RenderTableCell>(child))
        return !is<RenderTableRow>(*this);

    return false;
}

void RenderElement::addChild(RenderPtr<RenderObject> newChild, RenderObject* beforeChild)
{
    auto& child = *newChild;
    if (childRequiresTable(child)) {
        RenderTable* table;
        RenderObject* afterChild = beforeChild ? beforeChild->previousSibling() : m_lastChild;
        if (afterChild && afterChild->isAnonymous() && is<RenderTable>(*afterChild) && !afterChild->isBeforeContent())
            table = downcast<RenderTable>(afterChild);
        else {
            auto newTable =  RenderTable::createAnonymousWithParentRenderer(*this);
            table = newTable.get();
            addChild(WTFMove(newTable), beforeChild);
        }

        table->addChild(WTFMove(newChild));
    } else
        insertChildInternal(WTFMove(newChild), beforeChild, NotifyChildren);

    if (is<RenderText>(child))
        downcast<RenderText>(child).styleDidChange(StyleDifferenceEqual, nullptr);

    // SVG creates renderers for <g display="none">, as SVG requires children of hidden
    // <g>s to have renderers - at least that's how our implementation works. Consider:
    // <g display="none"><foreignObject><body style="position: relative">FOO...
    // - requiresLayer() would return true for the <body>, creating a new RenderLayer
    // - when the document is painted, both layers are painted. The <body> layer doesn't
    //   know that it's inside a "hidden SVG subtree", and thus paints, even if it shouldn't.
    // To avoid the problem alltogether, detect early if we're inside a hidden SVG subtree
    // and stop creating layers at all for these cases - they're not used anyways.
    if (child.hasLayer() && !layerCreationAllowedForSubtree())
        downcast<RenderLayerModelObject>(child).layer()->removeOnlyThisLayer();

    SVGRenderSupport::childAdded(*this, child);
}

RenderPtr<RenderObject> RenderElement::takeChild(RenderObject& oldChild)
{
    return takeChildInternal(oldChild, NotifyChildren);
}

void RenderElement::removeAndDestroyChild(RenderObject& oldChild)
{
    auto toDestroy = takeChild(oldChild);
}

void RenderElement::destroyLeftoverChildren()
{
    while (m_firstChild) {
        if (auto* node = m_firstChild->node())
            node->setRenderer(nullptr);
        removeAndDestroyChild(*m_firstChild);
    }
}

void RenderElement::insertChildInternal(RenderPtr<RenderObject> newChildPtr, RenderObject* beforeChild, NotifyChildrenType notifyChildren)
{
    RELEASE_ASSERT_WITH_MESSAGE(!view().layoutState(), "Layout must not mutate render tree");

    ASSERT(canHaveChildren() || canHaveGeneratedChildren());
    ASSERT(!newChildPtr->parent());
    ASSERT(!isRenderBlockFlow() || (!newChildPtr->isTableSection() && !newChildPtr->isTableRow() && !newChildPtr->isTableCell()));

    while (beforeChild && beforeChild->parent() && beforeChild->parent() != this)
        beforeChild = beforeChild->parent();

    ASSERT(!beforeChild || beforeChild->parent() == this);

    // Take the ownership.
    auto* newChild = newChildPtr.release();

    newChild->setParent(this);

    if (m_firstChild == beforeChild)
        m_firstChild = newChild;

    if (beforeChild) {
        RenderObject* previousSibling = beforeChild->previousSibling();
        if (previousSibling)
            previousSibling->setNextSibling(newChild);
        newChild->setPreviousSibling(previousSibling);
        newChild->setNextSibling(beforeChild);
        beforeChild->setPreviousSibling(newChild);
    } else {
        if (lastChild())
            lastChild()->setNextSibling(newChild);
        newChild->setPreviousSibling(lastChild());
        m_lastChild = newChild;
    }

    newChild->initializeFragmentedFlowStateOnInsertion();
    if (!renderTreeBeingDestroyed()) {
        if (notifyChildren == NotifyChildren)
            newChild->insertedIntoTree();
        if (is<RenderElement>(*newChild))
            RenderCounter::rendererSubtreeAttached(downcast<RenderElement>(*newChild));
    }

    newChild->setNeedsLayoutAndPrefWidthsRecalc();
    setPreferredLogicalWidthsDirty(true);
    if (!normalChildNeedsLayout())
        setChildNeedsLayout(); // We may supply the static position for an absolute positioned child.

    if (AXObjectCache* cache = document().axObjectCache())
        cache->childrenChanged(this, newChild);
    if (is<RenderBlockFlow>(*this))
        downcast<RenderBlockFlow>(*this).invalidateLineLayoutPath();
    if (hasOutlineAutoAncestor() || outlineStyleForRepaint().outlineStyleIsAuto())
        newChild->setHasOutlineAutoAncestor();
}

RenderPtr<RenderObject> RenderElement::takeChildInternal(RenderObject& oldChild, NotifyChildrenType notifyChildren)
{
    RELEASE_ASSERT_WITH_MESSAGE(!view().layoutState(), "Layout must not mutate render tree");

    ASSERT(canHaveChildren() || canHaveGeneratedChildren());
    ASSERT(oldChild.parent() == this);

    if (oldChild.isFloatingOrOutOfFlowPositioned())
        downcast<RenderBox>(oldChild).removeFloatingOrPositionedChildFromBlockLists();

    // So that we'll get the appropriate dirty bit set (either that a normal flow child got yanked or
    // that a positioned child got yanked). We also repaint, so that the area exposed when the child
    // disappears gets repainted properly.
    if (!renderTreeBeingDestroyed() && notifyChildren == NotifyChildren && oldChild.everHadLayout()) {
        oldChild.setNeedsLayoutAndPrefWidthsRecalc();
        // We only repaint |oldChild| if we have a RenderLayer as its visual overflow may not be tracked by its parent.
        if (oldChild.isBody())
            view().repaintRootContents();
        else
            oldChild.repaint();
    }

    // If we have a line box wrapper, delete it.
    if (is<RenderBox>(oldChild))
        downcast<RenderBox>(oldChild).deleteLineBoxWrapper();
    else if (is<RenderLineBreak>(oldChild))
        downcast<RenderLineBreak>(oldChild).deleteInlineBoxWrapper();
    
    if (!renderTreeBeingDestroyed() && is<RenderFlexibleBox>(this) && !oldChild.isFloatingOrOutOfFlowPositioned() && oldChild.isBox())
        downcast<RenderFlexibleBox>(this)->clearCachedChildIntrinsicContentLogicalHeight(downcast<RenderBox>(oldChild));

    // If oldChild is the start or end of the selection, then clear the selection to
    // avoid problems of invalid pointers.
    if (!renderTreeBeingDestroyed() && oldChild.isSelectionBorder())
        frame().selection().setNeedsSelectionUpdate();

    if (!renderTreeBeingDestroyed() && notifyChildren == NotifyChildren)
        oldChild.willBeRemovedFromTree();

    oldChild.resetFragmentedFlowStateOnRemoval();

    // WARNING: There should be no code running between willBeRemovedFromTree and the actual removal below.
    // This is needed to avoid race conditions where willBeRemovedFromTree would dirty the tree's structure
    // and the code running here would force an untimely rebuilding, leaving |oldChild| dangling.
    
    RenderObject* nextSibling = oldChild.nextSibling();

    if (oldChild.previousSibling())
        oldChild.previousSibling()->setNextSibling(nextSibling);
    if (nextSibling)
        nextSibling->setPreviousSibling(oldChild.previousSibling());

    if (m_firstChild == &oldChild)
        m_firstChild = nextSibling;
    if (m_lastChild == &oldChild)
        m_lastChild = oldChild.previousSibling();

    oldChild.setPreviousSibling(nullptr);
    oldChild.setNextSibling(nullptr);
    oldChild.setParent(nullptr);

    // rendererRemovedFromTree walks the whole subtree. We can improve performance
    // by skipping this step when destroying the entire tree.
    if (!renderTreeBeingDestroyed() && is<RenderElement>(oldChild))
        RenderCounter::rendererRemovedFromTree(downcast<RenderElement>(oldChild));

    if (AXObjectCache* cache = document().existingAXObjectCache())
        cache->childrenChanged(this);

    return RenderPtr<RenderObject>(&oldChild);
}

RenderBlock* RenderElement::containingBlockForFixedPosition() const
{
    auto* renderer = parent();
    while (renderer && !renderer->canContainFixedPositionObjects())
        renderer = renderer->parent();

    ASSERT(!renderer || !renderer->isAnonymousBlock());
    return downcast<RenderBlock>(renderer);
}

RenderBlock* RenderElement::containingBlockForAbsolutePosition() const
{
    // A relatively positioned RenderInline forwards its absolute positioned descendants to
    // its nearest non-anonymous containing block (to avoid having a positioned objects list in all RenderInlines).
    auto* renderer = isRenderInline() ? const_cast<RenderElement*>(downcast<RenderElement>(this)) : parent();
    while (renderer && !renderer->canContainAbsolutelyPositionedObjects())
        renderer = renderer->parent();
    // Make sure we only return non-anonymous RenderBlock as containing block.
    while (renderer && (!is<RenderBlock>(*renderer) || renderer->isAnonymousBlock()))
        renderer = renderer->containingBlock();
    return downcast<RenderBlock>(renderer);
}

static void addLayers(RenderElement& renderer, RenderLayer* parentLayer, RenderElement*& newObject, RenderLayer*& beforeChild)
{
    if (renderer.hasLayer()) {
        if (!beforeChild && newObject) {
            // We need to figure out the layer that follows newObject. We only do
            // this the first time we find a child layer, and then we update the
            // pointer values for newObject and beforeChild used by everyone else.
            beforeChild = newObject->parent()->findNextLayer(parentLayer, newObject);
            newObject = nullptr;
        }
        parentLayer->addChild(downcast<RenderLayerModelObject>(renderer).layer(), beforeChild);
        return;
    }

    for (auto& child : childrenOfType<RenderElement>(renderer))
        addLayers(child, parentLayer, newObject, beforeChild);
}

void RenderElement::addLayers(RenderLayer* parentLayer)
{
    if (!parentLayer)
        return;

    RenderElement* renderer = this;
    RenderLayer* beforeChild = nullptr;
    WebCore::addLayers(*this, parentLayer, renderer, beforeChild);
}

void RenderElement::removeLayers(RenderLayer* parentLayer)
{
    if (!parentLayer)
        return;

    if (hasLayer()) {
        parentLayer->removeChild(downcast<RenderLayerModelObject>(*this).layer());
        return;
    }

    for (auto& child : childrenOfType<RenderElement>(*this))
        child.removeLayers(parentLayer);
}

void RenderElement::moveLayers(RenderLayer* oldParent, RenderLayer* newParent)
{
    if (!newParent)
        return;

    if (hasLayer()) {
        RenderLayer* layer = downcast<RenderLayerModelObject>(*this).layer();
        ASSERT(oldParent == layer->parent());
        if (oldParent)
            oldParent->removeChild(layer);
        newParent->addChild(layer);
        return;
    }

    for (auto& child : childrenOfType<RenderElement>(*this))
        child.moveLayers(oldParent, newParent);
}

RenderLayer* RenderElement::findNextLayer(RenderLayer* parentLayer, RenderObject* startPoint, bool checkParent)
{
    // Error check the parent layer passed in. If it's null, we can't find anything.
    if (!parentLayer)
        return nullptr;

    // Step 1: If our layer is a child of the desired parent, then return our layer.
    RenderLayer* ourLayer = hasLayer() ? downcast<RenderLayerModelObject>(*this).layer() : nullptr;
    if (ourLayer && ourLayer->parent() == parentLayer)
        return ourLayer;

    // Step 2: If we don't have a layer, or our layer is the desired parent, then descend
    // into our siblings trying to find the next layer whose parent is the desired parent.
    if (!ourLayer || ourLayer == parentLayer) {
        for (RenderObject* child = startPoint ? startPoint->nextSibling() : firstChild(); child; child = child->nextSibling()) {
            if (!is<RenderElement>(*child))
                continue;
            RenderLayer* nextLayer = downcast<RenderElement>(*child).findNextLayer(parentLayer, nullptr, false);
            if (nextLayer)
                return nextLayer;
        }
    }

    // Step 3: If our layer is the desired parent layer, then we're finished. We didn't
    // find anything.
    if (parentLayer == ourLayer)
        return nullptr;

    // Step 4: If |checkParent| is set, climb up to our parent and check its siblings that
    // follow us to see if we can locate a layer.
    if (checkParent && parent())
        return parent()->findNextLayer(parentLayer, this, true);

    return nullptr;
}

bool RenderElement::layerCreationAllowedForSubtree() const
{
    RenderElement* parentRenderer = parent();
    while (parentRenderer) {
        if (parentRenderer->isSVGHiddenContainer())
            return false;
        parentRenderer = parentRenderer->parent();
    }
    
    return true;
}

void RenderElement::propagateStyleToAnonymousChildren(StylePropagationType propagationType)
{
    // FIXME: We could save this call when the change only affected non-inherited properties.
    for (auto& elementChild : childrenOfType<RenderElement>(*this)) {
        if (!elementChild.isAnonymous() || elementChild.style().styleType() != NOPSEUDO)
            continue;

        if (propagationType == PropagateToBlockChildrenOnly && !is<RenderBlock>(elementChild))
            continue;

#if ENABLE(FULLSCREEN_API)
        if (elementChild.isRenderFullScreen() || elementChild.isRenderFullScreenPlaceholder())
            continue;
#endif

        // RenderFragmentedFlows are updated through the RenderView::styleDidChange function.
        if (is<RenderFragmentedFlow>(elementChild))
            continue;

        auto newStyle = RenderStyle::createAnonymousStyleWithDisplay(style(), elementChild.style().display());
        if (style().specifiesColumns()) {
            if (elementChild.style().specifiesColumns())
                newStyle.inheritColumnPropertiesFrom(style());
            if (elementChild.style().columnSpan())
                newStyle.setColumnSpan(ColumnSpanAll);
        }

        // Preserve the position style of anonymous block continuations as they can have relative or sticky position when
        // they contain block descendants of relative or sticky positioned inlines.
        if (elementChild.isInFlowPositioned() && downcast<RenderBlock>(elementChild).isAnonymousBlockContinuation())
            newStyle.setPosition(elementChild.style().position());

        updateAnonymousChildStyle(elementChild, newStyle);
        
        elementChild.setStyle(WTFMove(newStyle));
    }
}

static inline bool rendererHasBackground(const RenderElement* renderer)
{
    return renderer && renderer->hasBackground();
}

void RenderElement::invalidateCachedFirstLineStyle()
{
    if (!m_hasValidCachedFirstLineStyle)
        return;
    m_hasValidCachedFirstLineStyle = false;
    // Invalidate the subtree as descendant's first line style may depend on ancestor's.
    for (auto& descendant : descendantsOfType<RenderElement>(*this))
        descendant.m_hasValidCachedFirstLineStyle = false;
}

void RenderElement::styleWillChange(StyleDifference diff, const RenderStyle& newStyle)
{
    auto* oldStyle = hasInitializedStyle() ? &style() : nullptr;
    if (oldStyle) {
        // If our z-index changes value or our visibility changes,
        // we need to dirty our stacking context's z-order list.
        bool visibilityChanged = m_style.visibility() != newStyle.visibility()
            || m_style.zIndex() != newStyle.zIndex()
            || m_style.hasAutoZIndex() != newStyle.hasAutoZIndex();
#if ENABLE(DASHBOARD_SUPPORT)
        if (visibilityChanged)
            document().setAnnotatedRegionsDirty(true);
#endif
#if PLATFORM(IOS) && ENABLE(TOUCH_EVENTS)
        if (visibilityChanged)
            document().setTouchEventRegionsNeedUpdate();
#endif
        if (visibilityChanged) {
            if (AXObjectCache* cache = document().existingAXObjectCache())
                cache->childrenChanged(parent(), this);
        }

        // Keep layer hierarchy visibility bits up to date if visibility changes.
        if (m_style.visibility() != newStyle.visibility()) {
            if (RenderLayer* layer = enclosingLayer()) {
                if (newStyle.visibility() == VISIBLE)
                    layer->setHasVisibleContent();
                else if (layer->hasVisibleContent() && (this == &layer->renderer() || layer->renderer().style().visibility() != VISIBLE)) {
                    layer->dirtyVisibleContentStatus();
                    if (diff > StyleDifferenceRepaintLayer)
                        repaint();
                }
            }
        }

        if (m_parent && (newStyle.outlineSize() < m_style.outlineSize() || shouldRepaintForStyleDifference(diff)))
            repaint();
        if (isFloating() && m_style.floating() != newStyle.floating()) {
            // For changes in float styles, we need to conceivably remove ourselves
            // from the floating objects list.
            downcast<RenderBox>(*this).removeFloatingOrPositionedChildFromBlockLists();
        } else if (isOutOfFlowPositioned() && m_style.position() != newStyle.position()) {
            // For changes in positioning styles, we need to conceivably remove ourselves
            // from the positioned objects list.
            downcast<RenderBox>(*this).removeFloatingOrPositionedChildFromBlockLists();
        }

        s_affectsParentBlock = isFloatingOrOutOfFlowPositioned()
            && (!newStyle.isFloating() && !newStyle.hasOutOfFlowPosition())
            && parent() && (parent()->isRenderBlockFlow() || parent()->isRenderInline());

        s_noLongerAffectsParentBlock = ((!isFloating() && newStyle.isFloating()) || (!isOutOfFlowPositioned() && newStyle.hasOutOfFlowPosition()))
            && parent() && parent()->isRenderBlock();

        // reset style flags
        if (diff == StyleDifferenceLayout || diff == StyleDifferenceLayoutPositionedMovementOnly) {
            setFloating(false);
            clearPositionedState();
        }
        if (newStyle.hasPseudoStyle(FIRST_LINE) || oldStyle->hasPseudoStyle(FIRST_LINE))
            invalidateCachedFirstLineStyle();

        setHorizontalWritingMode(true);
        setHasVisibleBoxDecorations(false);
        setHasOverflowClip(false);
        setHasTransformRelatedProperty(false);
        setHasReflection(false);
    } else {
        s_affectsParentBlock = false;
        s_noLongerAffectsParentBlock = false;
    }

    bool newStyleUsesFixedBackgrounds = newStyle.hasFixedBackgroundImage();
    bool oldStyleUsesFixedBackgrounds = m_style.hasFixedBackgroundImage();
    if (newStyleUsesFixedBackgrounds || oldStyleUsesFixedBackgrounds) {
        bool repaintFixedBackgroundsOnScroll = !settings().fixedBackgroundsPaintRelativeToDocument();
        bool newStyleSlowScroll = repaintFixedBackgroundsOnScroll && newStyleUsesFixedBackgrounds;
        bool oldStyleSlowScroll = oldStyle && repaintFixedBackgroundsOnScroll && oldStyleUsesFixedBackgrounds;
        bool drawsRootBackground = isDocumentElementRenderer() || (isBody() && !rendererHasBackground(document().documentElement()->renderer()));
        if (drawsRootBackground && repaintFixedBackgroundsOnScroll) {
            if (view().compositor().supportsFixedRootBackgroundCompositing()) {
                if (newStyleSlowScroll && newStyle.hasEntirelyFixedBackground())
                    newStyleSlowScroll = false;

                if (oldStyleSlowScroll && m_style.hasEntirelyFixedBackground())
                    oldStyleSlowScroll = false;
            }
        }

        if (oldStyleSlowScroll != newStyleSlowScroll) {
            if (oldStyleSlowScroll)
                view().frameView().removeSlowRepaintObject(this);

            if (newStyleSlowScroll)
                view().frameView().addSlowRepaintObject(this);
        }
    }

    if (isDocumentElementRenderer() || isBody())
        view().frameView().updateExtendBackgroundIfNecessary();
}

void RenderElement::handleDynamicFloatPositionChange()
{
    // We have gone from not affecting the inline status of the parent flow to suddenly
    // having an impact.  See if there is a mismatch between the parent flow's
    // childrenInline() state and our state.
    setInline(style().isDisplayInlineType());
    if (isInline() != parent()->childrenInline()) {
        if (!isInline())
            downcast<RenderBoxModelObject>(*parent()).childBecameNonInline(*this);
        else {
            // An anonymous block must be made to wrap this inline.
            auto newBlock = downcast<RenderBlock>(*parent()).createAnonymousBlock();
            auto& block = *newBlock;
            parent()->insertChildInternal(WTFMove(newBlock), this, RenderElement::NotifyChildren);
            auto thisToMove = parent()->takeChildInternal(*this, RenderElement::NotifyChildren);
            block.insertChildInternal(WTFMove(thisToMove), nullptr, RenderElement::NotifyChildren);
        }
    }
}

void RenderElement::removeAnonymousWrappersForInlinesIfNecessary()
{
    RenderBlock& parentBlock = downcast<RenderBlock>(*parent());
    if (!parentBlock.canDropAnonymousBlockChild())
        return;

    // We have changed to floated or out-of-flow positioning so maybe all our parent's
    // children can be inline now. Bail if there are any block children left on the line,
    // otherwise we can proceed to stripping solitary anonymous wrappers from the inlines.
    // FIXME: We should also handle split inlines here - we exclude them at the moment by returning
    // if we find a continuation.
    RenderObject* current = parent()->firstChild();
    while (current && ((current->isAnonymousBlock() && !downcast<RenderBlock>(*current).isAnonymousBlockContinuation()) || current->style().isFloating() || current->style().hasOutOfFlowPosition()))
        current = current->nextSibling();

    if (current)
        return;

    RenderObject* next;
    for (current = parent()->firstChild(); current; current = next) {
        next = current->nextSibling();
        if (current->isAnonymousBlock())
            parentBlock.dropAnonymousBoxChild(parentBlock, downcast<RenderBlock>(*current));
    }
}

#if !PLATFORM(IOS)
static bool areNonIdenticalCursorListsEqual(const RenderStyle* a, const RenderStyle* b)
{
    ASSERT(a->cursors() != b->cursors());
    return a->cursors() && b->cursors() && *a->cursors() == *b->cursors();
}

static inline bool areCursorsEqual(const RenderStyle* a, const RenderStyle* b)
{
    return a->cursor() == b->cursor() && (a->cursors() == b->cursors() || areNonIdenticalCursorListsEqual(a, b));
}
#endif

void RenderElement::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    updateFillImages(oldStyle ? &oldStyle->backgroundLayers() : nullptr, m_style.backgroundLayers());
    updateFillImages(oldStyle ? &oldStyle->maskLayers() : nullptr, m_style.maskLayers());
    updateImage(oldStyle ? oldStyle->borderImage().image() : nullptr, m_style.borderImage().image());
    updateImage(oldStyle ? oldStyle->maskBoxImage().image() : nullptr, m_style.maskBoxImage().image());
    updateShapeImage(oldStyle ? oldStyle->shapeOutside() : nullptr, m_style.shapeOutside());

    if (s_affectsParentBlock)
        handleDynamicFloatPositionChange();

    if (s_noLongerAffectsParentBlock)
        removeAnonymousWrappersForInlinesIfNecessary();

    SVGRenderSupport::styleChanged(*this, oldStyle);

    if (!m_parent)
        return;
    
    if (diff == StyleDifferenceLayout || diff == StyleDifferenceSimplifiedLayout) {
        RenderCounter::rendererStyleChanged(*this, oldStyle, &m_style);

        // If the object already needs layout, then setNeedsLayout won't do
        // any work. But if the containing block has changed, then we may need
        // to mark the new containing blocks for layout. The change that can
        // directly affect the containing block of this object is a change to
        // the position style.
        if (needsLayout() && oldStyle->position() != m_style.position())
            markContainingBlocksForLayout();

        if (diff == StyleDifferenceLayout)
            setNeedsLayoutAndPrefWidthsRecalc();
        else
            setNeedsSimplifiedNormalFlowLayout();
    } else if (diff == StyleDifferenceSimplifiedLayoutAndPositionedMovement) {
        setNeedsPositionedMovementLayout(oldStyle);
        setNeedsSimplifiedNormalFlowLayout();
    } else if (diff == StyleDifferenceLayoutPositionedMovementOnly)
        setNeedsPositionedMovementLayout(oldStyle);

    // Don't check for repaint here; we need to wait until the layer has been
    // updated by subclasses before we know if we have to repaint (in setStyle()).

#if !PLATFORM(IOS)
    if (oldStyle && !areCursorsEqual(oldStyle, &style()))
        frame().eventHandler().scheduleCursorUpdate();
#endif
    bool hadOutlineAuto = oldStyle && oldStyle->outlineStyleIsAuto();
    bool hasOutlineAuto = outlineStyleForRepaint().outlineStyleIsAuto();
    if (hasOutlineAuto != hadOutlineAuto) {
        updateOutlineAutoAncestor(hasOutlineAuto);
        issueRepaintForOutlineAuto(hasOutlineAuto ? outlineStyleForRepaint().outlineSize() : oldStyle->outlineSize());
    }
}

void RenderElement::insertedIntoTree()
{
    // Keep our layer hierarchy updated. Optimize for the common case where we don't have any children
    // and don't have a layer attached to ourselves.
    RenderLayer* layer = nullptr;
    if (firstChild() || hasLayer()) {
        layer = parent()->enclosingLayer();
        addLayers(layer);
    }

    // If |this| is visible but this object was not, tell the layer it has some visible content
    // that needs to be drawn and layer visibility optimization can't be used
    if (parent()->style().visibility() != VISIBLE && style().visibility() == VISIBLE && !hasLayer()) {
        if (!layer)
            layer = parent()->enclosingLayer();
        if (layer)
            layer->setHasVisibleContent();
    }

    RenderObject::insertedIntoTree();
}

void RenderElement::willBeRemovedFromTree()
{
    // If we remove a visible child from an invisible parent, we don't know the layer visibility any more.
    RenderLayer* layer = nullptr;
    if (parent()->style().visibility() != VISIBLE && style().visibility() == VISIBLE && !hasLayer()) {
        if ((layer = parent()->enclosingLayer()))
            layer->dirtyVisibleContentStatus();
    }
    // Keep our layer hierarchy updated.
    if (firstChild() || hasLayer()) {
        if (!layer)
            layer = parent()->enclosingLayer();
        removeLayers(layer);
    }

    if (isOutOfFlowPositioned() && parent()->childrenInline())
        parent()->dirtyLinesFromChangedChild(*this);

    RenderObject::willBeRemovedFromTree();
}

inline void RenderElement::clearSubtreeLayoutRootIfNeeded() const
{
    if (renderTreeBeingDestroyed())
        return;

    if (view().frameView().layoutContext().subtreeLayoutRoot() != this)
        return;

    // Normally when a renderer is detached from the tree, the appropriate dirty bits get set
    // which ensures that this renderer is no longer the layout root.
    ASSERT_NOT_REACHED();
    
    // This indicates a failure to layout the child, which is why
    // the layout root is still set to |this|. Make sure to clear it
    // since we are getting destroyed.
    view().frameView().layoutContext().clearSubtreeLayoutRoot();
}

void RenderElement::willBeDestroyed()
{
    if (m_style.hasFixedBackgroundImage() && !settings().fixedBackgroundsPaintRelativeToDocument())
        view().frameView().removeSlowRepaintObject(this);

    destroyLeftoverChildren();

    unregisterForVisibleInViewportCallback();

    if (hasCounterNodeMap())
        RenderCounter::destroyCounterNodes(*this);

    RenderObject::willBeDestroyed();

    clearSubtreeLayoutRootIfNeeded();

    if (hasInitializedStyle()) {
        for (auto* bgLayer = &m_style.backgroundLayers(); bgLayer; bgLayer = bgLayer->next()) {
            if (auto* backgroundImage = bgLayer->image())
                backgroundImage->removeClient(this);
        }
        for (auto* maskLayer = &m_style.maskLayers(); maskLayer; maskLayer = maskLayer->next()) {
            if (auto* maskImage = maskLayer->image())
                maskImage->removeClient(this);
        }
        if (auto* borderImage = m_style.borderImage().image())
            borderImage->removeClient(this);
        if (auto* maskBoxImage = m_style.maskBoxImage().image())
            maskBoxImage->removeClient(this);
        if (auto shapeValue = m_style.shapeOutside()) {
            if (auto shapeImage = shapeValue->image())
                shapeImage->removeClient(this);
        }
    }
    if (m_hasPausedImageAnimations)
        view().removeRendererWithPausedImageAnimations(*this);
}

void RenderElement::setNeedsPositionedMovementLayout(const RenderStyle* oldStyle)
{
    ASSERT(!isSetNeedsLayoutForbidden());
    if (needsPositionedMovementLayout())
        return;
    setNeedsPositionedMovementLayoutBit(true);
    markContainingBlocksForLayout();
    if (hasLayer()) {
        if (oldStyle && style().diffRequiresLayerRepaint(*oldStyle, downcast<RenderLayerModelObject>(*this).layer()->isComposited()))
            setLayerNeedsFullRepaint();
        else
            setLayerNeedsFullRepaintForPositionedMovementLayout();
    }
}

void RenderElement::clearChildNeedsLayout()
{
    setNormalChildNeedsLayoutBit(false);
    setPosChildNeedsLayoutBit(false);
    setNeedsSimplifiedNormalFlowLayoutBit(false);
    setNormalChildNeedsLayoutBit(false);
    setNeedsPositionedMovementLayoutBit(false);
}

void RenderElement::setNeedsSimplifiedNormalFlowLayout()
{
    ASSERT(!isSetNeedsLayoutForbidden());
    if (needsSimplifiedNormalFlowLayout())
        return;
    setNeedsSimplifiedNormalFlowLayoutBit(true);
    markContainingBlocksForLayout();
    if (hasLayer())
        setLayerNeedsFullRepaint();
}

RenderElement* RenderElement::hoverAncestor() const
{
    return parent();
}

static inline void paintPhase(RenderElement& element, PaintPhase phase, PaintInfo& paintInfo, const LayoutPoint& childPoint)
{
    paintInfo.phase = phase;
    element.paint(paintInfo, childPoint);
}

void RenderElement::paintAsInlineBlock(PaintInfo& paintInfo, const LayoutPoint& childPoint)
{
    // Paint all phases atomically, as though the element established its own stacking context.
    // (See Appendix E.2, section 6.4 on inline block/table/replaced elements in the CSS2.1 specification.)
    // This is also used by other elements (e.g. flex items and grid items).
    PaintPhase paintPhaseToUse = isExcludedAndPlacedInBorder() ? paintInfo.phase : PaintPhaseForeground;
    if (paintInfo.phase == PaintPhaseSelection)
        paint(paintInfo, childPoint);
    else if (paintInfo.phase == paintPhaseToUse) {
        paintPhase(*this, PaintPhaseBlockBackground, paintInfo, childPoint);
        paintPhase(*this, PaintPhaseChildBlockBackgrounds, paintInfo, childPoint);
        paintPhase(*this, PaintPhaseFloat, paintInfo, childPoint);
        paintPhase(*this, PaintPhaseForeground, paintInfo, childPoint);
        paintPhase(*this, PaintPhaseOutline, paintInfo, childPoint);

        // Reset |paintInfo| to the original phase.
        paintInfo.phase = paintPhaseToUse;
    }
}

void RenderElement::layout()
{
    StackStats::LayoutCheckPoint layoutCheckPoint;
    ASSERT(needsLayout());
    for (auto* child = firstChild(); child; child = child->nextSibling()) {
        if (child->needsLayout())
            downcast<RenderElement>(*child).layout();
        ASSERT(!child->needsLayout());
    }
    clearNeedsLayout();
}

static bool mustRepaintFillLayers(const RenderElement& renderer, const FillLayer& layer)
{
    // Nobody will use multiple layers without wanting fancy positioning.
    if (layer.next())
        return true;

    // Make sure we have a valid image.
    auto* image = layer.image();
    if (!image || !image->canRender(&renderer, renderer.style().effectiveZoom()))
        return false;

    if (!layer.xPosition().isZero() || !layer.yPosition().isZero())
        return true;

    auto sizeType = layer.sizeType();

    if (sizeType == Contain || sizeType == Cover)
        return true;

    if (sizeType == SizeLength) {
        auto size = layer.sizeLength();
        if (size.width.isPercentOrCalculated() || size.height.isPercentOrCalculated())
            return true;
        // If the image has neither an intrinsic width nor an intrinsic height, its size is determined as for 'contain'.
        if ((size.width.isAuto() || size.height.isAuto()) && image->isGeneratedImage())
            return true;
    } else if (image->usesImageContainerSize())
        return true;

    return false;
}

static bool mustRepaintBackgroundOrBorder(const RenderElement& renderer)
{
    if (renderer.hasMask() && mustRepaintFillLayers(renderer, renderer.style().maskLayers()))
        return true;

    // If we don't have a background/border/mask, then nothing to do.
    if (!renderer.hasVisibleBoxDecorations())
        return false;

    if (mustRepaintFillLayers(renderer, renderer.style().backgroundLayers()))
        return true;

    // Our fill layers are ok. Let's check border.
    if (renderer.style().hasBorder() && renderer.borderImageIsLoadedAndCanBeRendered())
        return true;

    return false;
}

bool RenderElement::repaintAfterLayoutIfNeeded(const RenderLayerModelObject* repaintContainer, const LayoutRect& oldBounds, const LayoutRect& oldOutlineBox, const LayoutRect* newBoundsPtr, const LayoutRect* newOutlineBoxRectPtr)
{
    if (view().printing())
        return false; // Don't repaint if we're printing.

    // This ASSERT fails due to animations. See https://bugs.webkit.org/show_bug.cgi?id=37048
    // ASSERT(!newBoundsPtr || *newBoundsPtr == clippedOverflowRectForRepaint(repaintContainer));
    LayoutRect newBounds = newBoundsPtr ? *newBoundsPtr : clippedOverflowRectForRepaint(repaintContainer);
    LayoutRect newOutlineBox;

    bool fullRepaint = selfNeedsLayout();
    // Presumably a background or a border exists if border-fit:lines was specified.
    if (!fullRepaint && style().borderFit() == BorderFitLines)
        fullRepaint = true;
    if (!fullRepaint) {
        // This ASSERT fails due to animations. See https://bugs.webkit.org/show_bug.cgi?id=37048
        // ASSERT(!newOutlineBoxRectPtr || *newOutlineBoxRectPtr == outlineBoundsForRepaint(repaintContainer));
        newOutlineBox = newOutlineBoxRectPtr ? *newOutlineBoxRectPtr : outlineBoundsForRepaint(repaintContainer);
        fullRepaint = (newOutlineBox.location() != oldOutlineBox.location() || (mustRepaintBackgroundOrBorder(*this) && (newBounds != oldBounds || newOutlineBox != oldOutlineBox)));
    }

    if (!repaintContainer)
        repaintContainer = &view();

    if (fullRepaint) {
        repaintUsingContainer(repaintContainer, oldBounds);
        if (newBounds != oldBounds)
            repaintUsingContainer(repaintContainer, newBounds);
        return true;
    }

    if (newBounds == oldBounds && newOutlineBox == oldOutlineBox)
        return false;

    LayoutUnit deltaLeft = newBounds.x() - oldBounds.x();
    if (deltaLeft > 0)
        repaintUsingContainer(repaintContainer, LayoutRect(oldBounds.x(), oldBounds.y(), deltaLeft, oldBounds.height()));
    else if (deltaLeft < 0)
        repaintUsingContainer(repaintContainer, LayoutRect(newBounds.x(), newBounds.y(), -deltaLeft, newBounds.height()));

    LayoutUnit deltaRight = newBounds.maxX() - oldBounds.maxX();
    if (deltaRight > 0)
        repaintUsingContainer(repaintContainer, LayoutRect(oldBounds.maxX(), newBounds.y(), deltaRight, newBounds.height()));
    else if (deltaRight < 0)
        repaintUsingContainer(repaintContainer, LayoutRect(newBounds.maxX(), oldBounds.y(), -deltaRight, oldBounds.height()));

    LayoutUnit deltaTop = newBounds.y() - oldBounds.y();
    if (deltaTop > 0)
        repaintUsingContainer(repaintContainer, LayoutRect(oldBounds.x(), oldBounds.y(), oldBounds.width(), deltaTop));
    else if (deltaTop < 0)
        repaintUsingContainer(repaintContainer, LayoutRect(newBounds.x(), newBounds.y(), newBounds.width(), -deltaTop));

    LayoutUnit deltaBottom = newBounds.maxY() - oldBounds.maxY();
    if (deltaBottom > 0)
        repaintUsingContainer(repaintContainer, LayoutRect(newBounds.x(), oldBounds.maxY(), newBounds.width(), deltaBottom));
    else if (deltaBottom < 0)
        repaintUsingContainer(repaintContainer, LayoutRect(oldBounds.x(), newBounds.maxY(), oldBounds.width(), -deltaBottom));

    if (newOutlineBox == oldOutlineBox)
        return false;

    // We didn't move, but we did change size. Invalidate the delta, which will consist of possibly
    // two rectangles (but typically only one).
    const RenderStyle& outlineStyle = outlineStyleForRepaint();
    LayoutUnit outlineWidth = outlineStyle.outlineSize();
    LayoutBoxExtent insetShadowExtent = style().getBoxShadowInsetExtent();
    LayoutUnit width = absoluteValue(newOutlineBox.width() - oldOutlineBox.width());
    if (width) {
        LayoutUnit shadowLeft;
        LayoutUnit shadowRight;
        style().getBoxShadowHorizontalExtent(shadowLeft, shadowRight);
        LayoutUnit borderRight = is<RenderBox>(*this) ? downcast<RenderBox>(*this).borderRight() : LayoutUnit::fromPixel(0);
        LayoutUnit boxWidth = is<RenderBox>(*this) ? downcast<RenderBox>(*this).width() : LayoutUnit();
        LayoutUnit minInsetRightShadowExtent = std::min<LayoutUnit>(-insetShadowExtent.right(), std::min(newBounds.width(), oldBounds.width()));
        LayoutUnit borderWidth = std::max(borderRight, std::max(valueForLength(style().borderTopRightRadius().width, boxWidth), valueForLength(style().borderBottomRightRadius().width, boxWidth)));
        LayoutUnit decorationsWidth = std::max<LayoutUnit>(-outlineStyle.outlineOffset(), borderWidth + minInsetRightShadowExtent) + std::max(outlineWidth, shadowRight);
        LayoutRect rightRect(newOutlineBox.x() + std::min(newOutlineBox.width(), oldOutlineBox.width()) - decorationsWidth,
            newOutlineBox.y(),
            width + decorationsWidth,
            std::max(newOutlineBox.height(), oldOutlineBox.height()));
        LayoutUnit right = std::min(newBounds.maxX(), oldBounds.maxX());
        if (rightRect.x() < right) {
            rightRect.setWidth(std::min(rightRect.width(), right - rightRect.x()));
            repaintUsingContainer(repaintContainer, rightRect);
        }
    }
    LayoutUnit height = absoluteValue(newOutlineBox.height() - oldOutlineBox.height());
    if (height) {
        LayoutUnit shadowTop;
        LayoutUnit shadowBottom;
        style().getBoxShadowVerticalExtent(shadowTop, shadowBottom);
        LayoutUnit borderBottom = is<RenderBox>(*this) ? downcast<RenderBox>(*this).borderBottom() : LayoutUnit::fromPixel(0);
        LayoutUnit boxHeight = is<RenderBox>(*this) ? downcast<RenderBox>(*this).height() : LayoutUnit();
        LayoutUnit minInsetBottomShadowExtent = std::min<LayoutUnit>(-insetShadowExtent.bottom(), std::min(newBounds.height(), oldBounds.height()));
        LayoutUnit borderHeight = std::max(borderBottom, std::max(valueForLength(style().borderBottomLeftRadius().height, boxHeight),
            valueForLength(style().borderBottomRightRadius().height, boxHeight)));
        LayoutUnit decorationsHeight = std::max<LayoutUnit>(-outlineStyle.outlineOffset(), borderHeight + minInsetBottomShadowExtent) + std::max(outlineWidth, shadowBottom);
        LayoutRect bottomRect(newOutlineBox.x(),
            std::min(newOutlineBox.maxY(), oldOutlineBox.maxY()) - decorationsHeight,
            std::max(newOutlineBox.width(), oldOutlineBox.width()),
            height + decorationsHeight);
        LayoutUnit bottom = std::min(newBounds.maxY(), oldBounds.maxY());
        if (bottomRect.y() < bottom) {
            bottomRect.setHeight(std::min(bottomRect.height(), bottom - bottomRect.y()));
            repaintUsingContainer(repaintContainer, bottomRect);
        }
    }
    return false;
}

bool RenderElement::borderImageIsLoadedAndCanBeRendered() const
{
    ASSERT(style().hasBorder());

    StyleImage* borderImage = style().borderImage().image();
    return borderImage && borderImage->canRender(this, style().effectiveZoom()) && borderImage->isLoaded();
}

bool RenderElement::mayCauseRepaintInsideViewport(const IntRect* optionalViewportRect) const
{
    auto& frameView = view().frameView();
    if (frameView.isOffscreen())
        return false;

    if (!hasOverflowClip()) {
        // FIXME: Computing the overflow rect is expensive if any descendant has
        // its own self-painting layer. As a result, we prefer to abort early in
        // this case and assume it may cause us to repaint inside the viewport.
        if (!hasLayer() || downcast<RenderLayerModelObject>(*this).layer()->firstChild())
            return true;
    }

    // Compute viewport rect if it was not provided.
    const IntRect& visibleRect = optionalViewportRect ? *optionalViewportRect : frameView.windowToContents(frameView.windowClipRect());
    return visibleRect.intersects(enclosingIntRect(absoluteClippedOverflowRect()));
}

bool RenderElement::isVisibleInDocumentRect(const IntRect& documentRect) const
{
    if (document().activeDOMObjectsAreSuspended())
        return false;
    if (style().visibility() != VISIBLE)
        return false;
    if (view().frameView().isOffscreen())
        return false;

    // Use background rect if we are the root or if we are the body and the background is propagated to the root.
    // FIXME: This is overly conservative as the image may not be a background-image, in which case it will not
    // be propagated to the root. At this point, we unfortunately don't have access to the image anymore so we
    // can no longer check if it is a background image.
    bool backgroundIsPaintedByRoot = isDocumentElementRenderer();
    if (isBody()) {
        auto& rootRenderer = *parent(); // If <body> has a renderer then <html> does too.
        ASSERT(rootRenderer.isDocumentElementRenderer());
        ASSERT(is<HTMLHtmlElement>(rootRenderer.element()));
        // FIXME: Should share body background propagation code.
        backgroundIsPaintedByRoot = !rootRenderer.hasBackground();

    }

    LayoutRect backgroundPaintingRect = backgroundIsPaintedByRoot ? view().backgroundRect() : absoluteClippedOverflowRect();
    if (!documentRect.intersects(enclosingIntRect(backgroundPaintingRect)))
        return false;

    return true;
}

void RenderElement::registerForVisibleInViewportCallback()
{
    if (m_isRegisteredForVisibleInViewportCallback)
        return;
    m_isRegisteredForVisibleInViewportCallback = true;

    view().registerForVisibleInViewportCallback(*this);
}

void RenderElement::unregisterForVisibleInViewportCallback()
{
    if (!m_isRegisteredForVisibleInViewportCallback)
        return;
    m_isRegisteredForVisibleInViewportCallback = false;

    view().unregisterForVisibleInViewportCallback(*this);
}

void RenderElement::setVisibleInViewportState(VisibleInViewportState state)
{
    if (state == visibleInViewportState())
        return;
    m_visibleInViewportState = static_cast<unsigned>(state);
    visibleInViewportStateChanged();
}

void RenderElement::visibleInViewportStateChanged()
{
    ASSERT_NOT_REACHED();
}

bool RenderElement::isVisibleInViewport() const
{
    auto& frameView = view().frameView();
    auto visibleRect = frameView.windowToContents(frameView.windowClipRect());
    return isVisibleInDocumentRect(visibleRect);
}

VisibleInViewportState RenderElement::imageFrameAvailable(CachedImage& image, ImageAnimatingState animatingState, const IntRect* changeRect)
{
    bool isVisible = isVisibleInViewport();

    if (!isVisible && animatingState == ImageAnimatingState::Yes)
        view().addRendererWithPausedImageAnimations(*this, image);

    // Static images should repaint even if they are outside the viewport rectangle
    // because they should be inside the TileCoverageRect.
    if (isVisible || animatingState == ImageAnimatingState::No)
        imageChanged(&image, changeRect);

    if (element() && image.image()->isBitmapImage())
        element()->dispatchWebKitImageReadyEventForTesting();

    return isVisible ? VisibleInViewportState::Yes : VisibleInViewportState::No;
}

void RenderElement::didRemoveCachedImageClient(CachedImage& cachedImage)
{
    if (hasPausedImageAnimations())
        view().removeRendererWithPausedImageAnimations(*this, cachedImage);
}

bool RenderElement::repaintForPausedImageAnimationsIfNeeded(const IntRect& visibleRect, CachedImage& cachedImage)
{
    ASSERT(m_hasPausedImageAnimations);
    if (!isVisibleInDocumentRect(visibleRect))
        return false;

    repaint();

    if (auto* image = cachedImage.image())
        image->startAnimation();

    // For directly-composited animated GIFs it does not suffice to call repaint() to resume animation. We need to mark the image as changed.
    if (is<RenderBoxModelObject>(*this))
        downcast<RenderBoxModelObject>(*this).contentChanged(ImageChanged);

    return true;
}

const RenderStyle* RenderElement::getCachedPseudoStyle(PseudoId pseudo, const RenderStyle* parentStyle) const
{
    if (pseudo < FIRST_INTERNAL_PSEUDOID && !style().hasPseudoStyle(pseudo))
        return nullptr;

    RenderStyle* cachedStyle = style().getCachedPseudoStyle(pseudo);
    if (cachedStyle)
        return cachedStyle;

    std::unique_ptr<RenderStyle> result = getUncachedPseudoStyle(PseudoStyleRequest(pseudo), parentStyle);
    if (result)
        return const_cast<RenderStyle&>(m_style).addCachedPseudoStyle(WTFMove(result));
    return nullptr;
}

std::unique_ptr<RenderStyle> RenderElement::getUncachedPseudoStyle(const PseudoStyleRequest& pseudoStyleRequest, const RenderStyle* parentStyle, const RenderStyle* ownStyle) const
{
    if (pseudoStyleRequest.pseudoId < FIRST_INTERNAL_PSEUDOID && !ownStyle && !style().hasPseudoStyle(pseudoStyleRequest.pseudoId))
        return nullptr;

    if (!parentStyle) {
        ASSERT(!ownStyle);
        parentStyle = &style();
    }

    if (isAnonymous())
        return nullptr;

    auto& styleResolver = element()->styleResolver();

    std::unique_ptr<RenderStyle> style = styleResolver.pseudoStyleForElement(*element(), pseudoStyleRequest, *parentStyle);

    if (style)
        Style::loadPendingResources(*style, document(), element());

    return style;
}

Color RenderElement::selectionColor(int colorProperty) const
{
    // If the element is unselectable, or we are only painting the selection,
    // don't override the foreground color with the selection foreground color.
    if (style().userSelect() == SELECT_NONE
        || (view().frameView().paintBehavior() & (PaintBehaviorSelectionOnly | PaintBehaviorSelectionAndBackgroundsOnly)))
        return Color();

    if (std::unique_ptr<RenderStyle> pseudoStyle = selectionPseudoStyle()) {
        Color color = pseudoStyle->visitedDependentColor(colorProperty);
        if (!color.isValid())
            color = pseudoStyle->visitedDependentColor(CSSPropertyColor);
        return color;
    }

    if (frame().selection().isFocusedAndActive())
        return theme().activeSelectionForegroundColor();
    return theme().inactiveSelectionForegroundColor();
}

std::unique_ptr<RenderStyle> RenderElement::selectionPseudoStyle() const
{
    if (isAnonymous())
        return nullptr;

    if (ShadowRoot* root = element()->containingShadowRoot()) {
        if (root->mode() == ShadowRootMode::UserAgent) {
            if (Element* shadowHost = element()->shadowHost())
                return shadowHost->renderer()->getUncachedPseudoStyle(PseudoStyleRequest(SELECTION));
        }
    }

    return getUncachedPseudoStyle(PseudoStyleRequest(SELECTION));
}

Color RenderElement::selectionForegroundColor() const
{
    return selectionColor(CSSPropertyWebkitTextFillColor);
}

Color RenderElement::selectionEmphasisMarkColor() const
{
    return selectionColor(CSSPropertyWebkitTextEmphasisColor);
}

Color RenderElement::selectionBackgroundColor() const
{
    if (style().userSelect() == SELECT_NONE)
        return Color();

    if (frame().selection().shouldShowBlockCursor() && frame().selection().isCaret())
        return style().visitedDependentColor(CSSPropertyColor).blendWithWhite();

    std::unique_ptr<RenderStyle> pseudoStyle = selectionPseudoStyle();
    if (pseudoStyle && pseudoStyle->visitedDependentColor(CSSPropertyBackgroundColor).isValid())
        return pseudoStyle->visitedDependentColor(CSSPropertyBackgroundColor).blendWithWhite();

    if (frame().selection().isFocusedAndActive())
        return theme().activeSelectionBackgroundColor();
    return theme().inactiveSelectionBackgroundColor();
}

bool RenderElement::getLeadingCorner(FloatPoint& point, bool& insideFixed) const
{
    if (!isInline() || isReplaced()) {
        point = localToAbsolute(FloatPoint(), UseTransforms, &insideFixed);
        return true;
    }

    // find the next text/image child, to get a position
    const RenderObject* o = this;
    while (o) {
        const RenderObject* p = o;
        if (RenderObject* child = o->firstChildSlow())
            o = child;
        else if (o->nextSibling())
            o = o->nextSibling();
        else {
            RenderObject* next = 0;
            while (!next && o->parent()) {
                o = o->parent();
                next = o->nextSibling();
            }
            o = next;

            if (!o)
                break;
        }
        ASSERT(o);

        if (!o->isInline() || o->isReplaced()) {
            point = o->localToAbsolute(FloatPoint(), UseTransforms, &insideFixed);
            return true;
        }

        if (p->node() && p->node() == element() && is<RenderText>(*o) && !downcast<RenderText>(*o).firstTextBox()) {
            // do nothing - skip unrendered whitespace that is a child or next sibling of the anchor
        } else if (is<RenderText>(*o) || o->isReplaced()) {
            point = FloatPoint();
            if (is<RenderText>(*o) && downcast<RenderText>(*o).firstTextBox())
                point.move(downcast<RenderText>(*o).linesBoundingBox().x(), downcast<RenderText>(*o).topOfFirstText());
            else if (is<RenderBox>(*o))
                point.moveBy(downcast<RenderBox>(*o).location());
            point = o->container()->localToAbsolute(point, UseTransforms, &insideFixed);
            return true;
        }
    }
    
    // If the target doesn't have any children or siblings that could be used to calculate the scroll position, we must be
    // at the end of the document. Scroll to the bottom. FIXME: who said anything about scrolling?
    if (!o && document().view()) {
        point = FloatPoint(0, document().view()->contentsHeight());
        return true;
    }
    return false;
}

bool RenderElement::getTrailingCorner(FloatPoint& point, bool& insideFixed) const
{
    if (!isInline() || isReplaced()) {
        point = localToAbsolute(LayoutPoint(downcast<RenderBox>(*this).size()), UseTransforms, &insideFixed);
        return true;
    }

    // find the last text/image child, to get a position
    const RenderObject* o = this;
    while (o) {
        if (RenderObject* child = o->lastChildSlow())
            o = child;
        else if (o->previousSibling())
            o = o->previousSibling();
        else {
            RenderObject* prev = 0;
            while (!prev) {
                o = o->parent();
                if (!o)
                    return false;
                prev = o->previousSibling();
            }
            o = prev;
        }
        ASSERT(o);
        if (is<RenderText>(*o) || o->isReplaced()) {
            point = FloatPoint();
            if (is<RenderText>(*o)) {
                LayoutRect linesBox = downcast<RenderText>(*o).linesBoundingBox();
                if (!linesBox.maxX() && !linesBox.maxY())
                    continue;
                point.moveBy(linesBox.maxXMaxYCorner());
            } else
                point.moveBy(downcast<RenderBox>(*o).frameRect().maxXMaxYCorner());
            point = o->container()->localToAbsolute(point, UseTransforms, &insideFixed);
            return true;
        }
    }
    return true;
}

LayoutRect RenderElement::absoluteAnchorRect(bool* insideFixed) const
{
    FloatPoint leading, trailing;
    bool leadingInFixed = false;
    bool trailingInFixed = false;
    getLeadingCorner(leading, leadingInFixed);
    getTrailingCorner(trailing, trailingInFixed);

    FloatPoint upperLeft = leading;
    FloatPoint lowerRight = trailing;

    // Vertical writing modes might mean the leading point is not in the top left
    if (!isInline() || isReplaced()) {
        upperLeft = FloatPoint(std::min(leading.x(), trailing.x()), std::min(leading.y(), trailing.y()));
        lowerRight = FloatPoint(std::max(leading.x(), trailing.x()), std::max(leading.y(), trailing.y()));
    } // Otherwise, it's not obvious what to do.

    if (insideFixed) {
        // For now, just look at the leading corner. Handling one inside fixed and one not would be tricky.
        *insideFixed = leadingInFixed;
    }

    return enclosingLayoutRect(FloatRect(upperLeft, lowerRight.expandedTo(upperLeft) - upperLeft));
}

const RenderElement* RenderElement::enclosingRendererWithTextDecoration(TextDecoration textDecoration, bool firstLine) const
{
    const RenderElement* current = this;
    do {
        if (current->isRenderBlock())
            return current;
        if (!current->isRenderInline() || current->isRubyText())
            return nullptr;
        
        const RenderStyle& styleToUse = firstLine ? current->firstLineStyle() : current->style();
        if (styleToUse.textDecoration() & textDecoration)
            return current;
        current = current->parent();
    } while (current && (!current->element() || (!is<HTMLAnchorElement>(*current->element()) && !current->element()->hasTagName(HTMLNames::fontTag))));

    return current;
}

void RenderElement::drawLineForBoxSide(GraphicsContext& graphicsContext, const FloatRect& rect, BoxSide side, Color color, EBorderStyle borderStyle, float adjacentWidth1, float adjacentWidth2, bool antialias) const
{
    auto drawBorderRect = [&graphicsContext] (const FloatRect& rect)
    {
        if (rect.isEmpty())
            return;
        graphicsContext.drawRect(rect);
    };

    auto drawLineFor = [this, &graphicsContext, color, antialias] (const FloatRect& rect, BoxSide side, EBorderStyle borderStyle, const FloatSize& adjacent)
    {
        if (rect.isEmpty())
            return;
        drawLineForBoxSide(graphicsContext, rect, side, color, borderStyle, adjacent.width(), adjacent.height(), antialias);
    };

    float x1 = rect.x();
    float x2 = rect.maxX();
    float y1 = rect.y();
    float y2 = rect.maxY();
    float thickness;
    float length;
    if (side == BSTop || side == BSBottom) {
        thickness = y2 - y1;
        length = x2 - x1;
    } else {
        thickness = x2 - x1;
        length = y2 - y1;
    }
    // FIXME: We really would like this check to be an ASSERT as we don't want to draw empty borders. However
    // nothing guarantees that the following recursive calls to drawLineForBoxSide will have non-null dimensions.
    if (!thickness || !length)
        return;

    float deviceScaleFactor = document().deviceScaleFactor();
    if (borderStyle == DOUBLE && (thickness * deviceScaleFactor) < 3)
        borderStyle = SOLID;

    switch (borderStyle) {
    case BNONE:
    case BHIDDEN:
        return;
    case DOTTED:
    case DASHED: {
        bool wasAntialiased = graphicsContext.shouldAntialias();
        StrokeStyle oldStrokeStyle = graphicsContext.strokeStyle();
        graphicsContext.setShouldAntialias(antialias);
        graphicsContext.setStrokeColor(color);
        graphicsContext.setStrokeThickness(thickness);
        graphicsContext.setStrokeStyle(borderStyle == DASHED ? DashedStroke : DottedStroke);
        graphicsContext.drawLine(roundPointToDevicePixels(LayoutPoint(x1, y1), deviceScaleFactor), roundPointToDevicePixels(LayoutPoint(x2, y2), deviceScaleFactor));
        graphicsContext.setShouldAntialias(wasAntialiased);
        graphicsContext.setStrokeStyle(oldStrokeStyle);
        break;
    }
    case DOUBLE: {
        float thirdOfThickness = ceilToDevicePixel(thickness / 3, deviceScaleFactor);
        ASSERT(thirdOfThickness);

        if (!adjacentWidth1 && !adjacentWidth2) {
            StrokeStyle oldStrokeStyle = graphicsContext.strokeStyle();
            graphicsContext.setStrokeStyle(NoStroke);
            graphicsContext.setFillColor(color);

            bool wasAntialiased = graphicsContext.shouldAntialias();
            graphicsContext.setShouldAntialias(antialias);

            switch (side) {
            case BSTop:
            case BSBottom:
                drawBorderRect(snapRectToDevicePixels(x1, y1, length, thirdOfThickness, deviceScaleFactor));
                drawBorderRect(snapRectToDevicePixels(x1, y2 - thirdOfThickness, length, thirdOfThickness, deviceScaleFactor));
                break;
            case BSLeft:
            case BSRight:
                drawBorderRect(snapRectToDevicePixels(x1, y1, thirdOfThickness, length, deviceScaleFactor));
                drawBorderRect(snapRectToDevicePixels(x2 - thirdOfThickness, y1, thirdOfThickness, length, deviceScaleFactor));
                break;
            }

            graphicsContext.setShouldAntialias(wasAntialiased);
            graphicsContext.setStrokeStyle(oldStrokeStyle);
        } else {
            float adjacent1BigThird = ceilToDevicePixel(adjacentWidth1 / 3, deviceScaleFactor);
            float adjacent2BigThird = ceilToDevicePixel(adjacentWidth2 / 3, deviceScaleFactor);

            float offset1 = floorToDevicePixel(fabs(adjacentWidth1) * 2 / 3, deviceScaleFactor);
            float offset2 = floorToDevicePixel(fabs(adjacentWidth2) * 2 / 3, deviceScaleFactor);

            float mitreOffset1 = adjacentWidth1 < 0 ? offset1 : 0;
            float mitreOffset2 = adjacentWidth1 > 0 ? offset1 : 0;
            float mitreOffset3 = adjacentWidth2 < 0 ? offset2 : 0;
            float mitreOffset4 = adjacentWidth2 > 0 ? offset2 : 0;

            FloatRect paintBorderRect;
            switch (side) {
            case BSTop:
                paintBorderRect = snapRectToDevicePixels(LayoutRect(x1 + mitreOffset1, y1, (x2 - mitreOffset3) - (x1 + mitreOffset1), thirdOfThickness), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));

                paintBorderRect = snapRectToDevicePixels(LayoutRect(x1 + mitreOffset2, y2 - thirdOfThickness, (x2 - mitreOffset4) - (x1 + mitreOffset2), thirdOfThickness), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));
                break;
            case BSLeft:
                paintBorderRect = snapRectToDevicePixels(LayoutRect(x1, y1 + mitreOffset1, thirdOfThickness, (y2 - mitreOffset3) - (y1 + mitreOffset1)), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));

                paintBorderRect = snapRectToDevicePixels(LayoutRect(x2 - thirdOfThickness, y1 + mitreOffset2, thirdOfThickness, (y2 - mitreOffset4) - (y1 + mitreOffset2)), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));
                break;
            case BSBottom:
                paintBorderRect = snapRectToDevicePixels(LayoutRect(x1 + mitreOffset2, y1, (x2 - mitreOffset4) - (x1 + mitreOffset2), thirdOfThickness), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));

                paintBorderRect = snapRectToDevicePixels(LayoutRect(x1 + mitreOffset1, y2 - thirdOfThickness, (x2 - mitreOffset3) - (x1 + mitreOffset1), thirdOfThickness), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));
                break;
            case BSRight:
                paintBorderRect = snapRectToDevicePixels(LayoutRect(x1, y1 + mitreOffset2, thirdOfThickness, (y2 - mitreOffset4) - (y1 + mitreOffset2)), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));

                paintBorderRect = snapRectToDevicePixels(LayoutRect(x2 - thirdOfThickness, y1 + mitreOffset1, thirdOfThickness, (y2 - mitreOffset3) - (y1 + mitreOffset1)), deviceScaleFactor);
                drawLineFor(paintBorderRect, side, SOLID, FloatSize(adjacent1BigThird, adjacent2BigThird));
                break;
            default:
                break;
            }
        }
        break;
    }
    case RIDGE:
    case GROOVE: {
        EBorderStyle s1;
        EBorderStyle s2;
        if (borderStyle == GROOVE) {
            s1 = INSET;
            s2 = OUTSET;
        } else {
            s1 = OUTSET;
            s2 = INSET;
        }

        float adjacent1BigHalf = ceilToDevicePixel(adjacentWidth1 / 2, deviceScaleFactor);
        float adjacent2BigHalf = ceilToDevicePixel(adjacentWidth2 / 2, deviceScaleFactor);

        float adjacent1SmallHalf = floorToDevicePixel(adjacentWidth1 / 2, deviceScaleFactor);
        float adjacent2SmallHalf = floorToDevicePixel(adjacentWidth2 / 2, deviceScaleFactor);

        float offset1 = 0;
        float offset2 = 0;
        float offset3 = 0;
        float offset4 = 0;

        if (((side == BSTop || side == BSLeft) && adjacentWidth1 < 0) || ((side == BSBottom || side == BSRight) && adjacentWidth1 > 0))
            offset1 = floorToDevicePixel(adjacentWidth1 / 2, deviceScaleFactor);

        if (((side == BSTop || side == BSLeft) && adjacentWidth2 < 0) || ((side == BSBottom || side == BSRight) && adjacentWidth2 > 0))
            offset2 = ceilToDevicePixel(adjacentWidth2 / 2, deviceScaleFactor);

        if (((side == BSTop || side == BSLeft) && adjacentWidth1 > 0) || ((side == BSBottom || side == BSRight) && adjacentWidth1 < 0))
            offset3 = floorToDevicePixel(fabs(adjacentWidth1) / 2, deviceScaleFactor);

        if (((side == BSTop || side == BSLeft) && adjacentWidth2 > 0) || ((side == BSBottom || side == BSRight) && adjacentWidth2 < 0))
            offset4 = ceilToDevicePixel(adjacentWidth2 / 2, deviceScaleFactor);

        float adjustedX = ceilToDevicePixel((x1 + x2) / 2, deviceScaleFactor);
        float adjustedY = ceilToDevicePixel((y1 + y2) / 2, deviceScaleFactor);
        // Quads can't use the default snapping rect functions.
        x1 = roundToDevicePixel(x1, deviceScaleFactor);
        x2 = roundToDevicePixel(x2, deviceScaleFactor);
        y1 = roundToDevicePixel(y1, deviceScaleFactor);
        y2 = roundToDevicePixel(y2, deviceScaleFactor);

        switch (side) {
        case BSTop:
            drawLineFor(FloatRect(FloatPoint(x1 + offset1, y1), FloatPoint(x2 - offset2, adjustedY)), side, s1, FloatSize(adjacent1BigHalf, adjacent2BigHalf));
            drawLineFor(FloatRect(FloatPoint(x1 + offset3, adjustedY), FloatPoint(x2 - offset4, y2)), side, s2, FloatSize(adjacent1SmallHalf, adjacent2SmallHalf));
            break;
        case BSLeft:
            drawLineFor(FloatRect(FloatPoint(x1, y1 + offset1), FloatPoint(adjustedX, y2 - offset2)), side, s1, FloatSize(adjacent1BigHalf, adjacent2BigHalf));
            drawLineFor(FloatRect(FloatPoint(adjustedX, y1 + offset3), FloatPoint(x2, y2 - offset4)), side, s2, FloatSize(adjacent1SmallHalf, adjacent2SmallHalf));
            break;
        case BSBottom:
            drawLineFor(FloatRect(FloatPoint(x1 + offset1, y1), FloatPoint(x2 - offset2, adjustedY)), side, s2, FloatSize(adjacent1BigHalf, adjacent2BigHalf));
            drawLineFor(FloatRect(FloatPoint(x1 + offset3, adjustedY), FloatPoint(x2 - offset4, y2)), side, s1, FloatSize(adjacent1SmallHalf, adjacent2SmallHalf));
            break;
        case BSRight:
            drawLineFor(FloatRect(FloatPoint(x1, y1 + offset1), FloatPoint(adjustedX, y2 - offset2)), side, s2, FloatSize(adjacent1BigHalf, adjacent2BigHalf));
            drawLineFor(FloatRect(FloatPoint(adjustedX, y1 + offset3), FloatPoint(x2, y2 - offset4)), side, s1, FloatSize(adjacent1SmallHalf, adjacent2SmallHalf));
            break;
        }
        break;
    }
    case INSET:
    case OUTSET:
        calculateBorderStyleColor(borderStyle, side, color);
        FALLTHROUGH;
    case SOLID: {
        StrokeStyle oldStrokeStyle = graphicsContext.strokeStyle();
        ASSERT(x2 >= x1);
        ASSERT(y2 >= y1);
        if (!adjacentWidth1 && !adjacentWidth2) {
            graphicsContext.setStrokeStyle(NoStroke);
            graphicsContext.setFillColor(color);
            bool wasAntialiased = graphicsContext.shouldAntialias();
            graphicsContext.setShouldAntialias(antialias);
            drawBorderRect(snapRectToDevicePixels(x1, y1, x2 - x1, y2 - y1, deviceScaleFactor));
            graphicsContext.setShouldAntialias(wasAntialiased);
            graphicsContext.setStrokeStyle(oldStrokeStyle);
            return;
        }

        // FIXME: These roundings should be replaced by ASSERT(device pixel positioned) when all the callers have transitioned to device pixels.
        x1 = roundToDevicePixel(x1, deviceScaleFactor);
        y1 = roundToDevicePixel(y1, deviceScaleFactor);
        x2 = roundToDevicePixel(x2, deviceScaleFactor);
        y2 = roundToDevicePixel(y2, deviceScaleFactor);

        Vector<FloatPoint> quad;
        quad.reserveInitialCapacity(4);
        switch (side) {
        case BSTop:
            quad.uncheckedAppend({ x1 + std::max<float>(-adjacentWidth1, 0), y1 });
            quad.uncheckedAppend({ x1 + std::max<float>( adjacentWidth1, 0), y2 });
            quad.uncheckedAppend({ x2 - std::max<float>( adjacentWidth2, 0), y2 });
            quad.uncheckedAppend({ x2 - std::max<float>(-adjacentWidth2, 0), y1 });
            break;
        case BSBottom:
            quad.uncheckedAppend({ x1 + std::max<float>( adjacentWidth1, 0), y1 });
            quad.uncheckedAppend({ x1 + std::max<float>(-adjacentWidth1, 0), y2 });
            quad.uncheckedAppend({ x2 - std::max<float>(-adjacentWidth2, 0), y2 });
            quad.uncheckedAppend({ x2 - std::max<float>( adjacentWidth2, 0), y1 });
            break;
        case BSLeft:
            quad.uncheckedAppend({ x1, y1 + std::max<float>(-adjacentWidth1, 0) });
            quad.uncheckedAppend({ x1, y2 - std::max<float>(-adjacentWidth2, 0) });
            quad.uncheckedAppend({ x2, y2 - std::max<float>( adjacentWidth2, 0) });
            quad.uncheckedAppend({ x2, y1 + std::max<float>( adjacentWidth1, 0) });
            break;
        case BSRight:
            quad.uncheckedAppend({ x1, y1 + std::max<float>( adjacentWidth1, 0) });
            quad.uncheckedAppend({ x1, y2 - std::max<float>( adjacentWidth2, 0) });
            quad.uncheckedAppend({ x2, y2 - std::max<float>(-adjacentWidth2, 0) });
            quad.uncheckedAppend({ x2, y1 + std::max<float>(-adjacentWidth1, 0) });
            break;
        }

        graphicsContext.setStrokeStyle(NoStroke);
        graphicsContext.setFillColor(color);
        bool wasAntialiased = graphicsContext.shouldAntialias();
        graphicsContext.setShouldAntialias(antialias);
        graphicsContext.fillPath(Path::polygonPathFromPoints(quad));
        graphicsContext.setShouldAntialias(wasAntialiased);

        graphicsContext.setStrokeStyle(oldStrokeStyle);
        break;
    }
    }
}

void RenderElement::paintFocusRing(PaintInfo& paintInfo, const RenderStyle& style, const Vector<LayoutRect>& focusRingRects)
{
    ASSERT(style.outlineStyleIsAuto());
    float outlineOffset = style.outlineOffset();
    Vector<FloatRect> pixelSnappedFocusRingRects;
    float deviceScaleFactor = document().deviceScaleFactor();
    for (auto rect : focusRingRects) {
        rect.inflate(outlineOffset);
        pixelSnappedFocusRingRects.append(snapRectToDevicePixels(rect, deviceScaleFactor));
    }
#if PLATFORM(MAC)
    bool needsRepaint;
    if (style.hasBorderRadius()) {
        Path path = PathUtilities::pathWithShrinkWrappedRectsForOutline(pixelSnappedFocusRingRects, style.border(), outlineOffset, style.direction(), style.writingMode(),
            document().deviceScaleFactor());
        if (path.isEmpty()) {
            for (auto rect : pixelSnappedFocusRingRects)
                path.addRect(rect);
        }
        paintInfo.context().drawFocusRing(path, page().focusController().timeSinceFocusWasSet(), needsRepaint);
    } else
        paintInfo.context().drawFocusRing(pixelSnappedFocusRingRects, page().focusController().timeSinceFocusWasSet(), needsRepaint);
    if (needsRepaint)
        page().focusController().setFocusedElementNeedsRepaint();
#else
    paintInfo.context().drawFocusRing(pixelSnappedFocusRingRects, style.outlineWidth(), style.outlineOffset(), style.visitedDependentColor(CSSPropertyOutlineColor));
#endif
}

void RenderElement::paintOutline(PaintInfo& paintInfo, const LayoutRect& paintRect)
{
    GraphicsContext& graphicsContext = paintInfo.context();
    if (graphicsContext.paintingDisabled())
        return;

    if (!hasOutline())
        return;

    auto& styleToUse = style();
    float outlineWidth = floorToDevicePixel(styleToUse.outlineWidth(), document().deviceScaleFactor());
    float outlineOffset = floorToDevicePixel(styleToUse.outlineOffset(), document().deviceScaleFactor());

    // Only paint the focus ring by hand if the theme isn't able to draw it.
    if (styleToUse.outlineStyleIsAuto() && !theme().supportsFocusRing(styleToUse)) {
        Vector<LayoutRect> focusRingRects;
        addFocusRingRects(focusRingRects, paintRect.location(), paintInfo.paintContainer);
        paintFocusRing(paintInfo, styleToUse, focusRingRects);
    }

    if (hasOutlineAnnotation() && !styleToUse.outlineStyleIsAuto() && !theme().supportsFocusRing(styleToUse))
        addPDFURLRect(paintInfo, paintRect.location());

    if (styleToUse.outlineStyleIsAuto() || styleToUse.outlineStyle() == BNONE)
        return;

    FloatRect outer = paintRect;
    outer.inflate(outlineOffset + outlineWidth);
    FloatRect inner = outer;
    inner.inflate(-outlineWidth);

    // FIXME: This prevents outlines from painting inside the object. See bug 12042
    if (outer.isEmpty())
        return;

    EBorderStyle outlineStyle = styleToUse.outlineStyle();
    Color outlineColor = styleToUse.visitedDependentColor(CSSPropertyOutlineColor);

    bool useTransparencyLayer = !outlineColor.isOpaque();
    if (useTransparencyLayer) {
        if (outlineStyle == SOLID) {
            Path path;
            path.addRect(outer);
            path.addRect(inner);
            graphicsContext.setFillRule(RULE_EVENODD);
            graphicsContext.setFillColor(outlineColor);
            graphicsContext.fillPath(path);
            return;
        }
        graphicsContext.beginTransparencyLayer(outlineColor.alphaAsFloat());
        outlineColor = outlineColor.opaqueColor();
    }

    float leftOuter = outer.x();
    float leftInner = inner.x();
    float rightOuter = outer.maxX();
    float rightInner = std::min(inner.maxX(), rightOuter);
    float topOuter = outer.y();
    float topInner = inner.y();
    float bottomOuter = outer.maxY();
    float bottomInner = std::min(inner.maxY(), bottomOuter);

    drawLineForBoxSide(graphicsContext, FloatRect(FloatPoint(leftOuter, topOuter), FloatPoint(leftInner, bottomOuter)), BSLeft, outlineColor, outlineStyle, outlineWidth, outlineWidth);
    drawLineForBoxSide(graphicsContext, FloatRect(FloatPoint(leftOuter, topOuter), FloatPoint(rightOuter, topInner)), BSTop, outlineColor, outlineStyle, outlineWidth, outlineWidth);
    drawLineForBoxSide(graphicsContext, FloatRect(FloatPoint(rightInner, topOuter), FloatPoint(rightOuter, bottomOuter)), BSRight, outlineColor, outlineStyle, outlineWidth, outlineWidth);
    drawLineForBoxSide(graphicsContext, FloatRect(FloatPoint(leftOuter, bottomInner), FloatPoint(rightOuter, bottomOuter)), BSBottom, outlineColor, outlineStyle, outlineWidth, outlineWidth);

    if (useTransparencyLayer)
        graphicsContext.endTransparencyLayer();
}

void RenderElement::issueRepaintForOutlineAuto(float outlineSize)
{
    LayoutRect repaintRect;
    Vector<LayoutRect> focusRingRects;
    addFocusRingRects(focusRingRects, LayoutPoint(), containerForRepaint());
    for (auto rect : focusRingRects) {
        rect.inflate(outlineSize);
        repaintRect.unite(rect);
    }
    repaintRectangle(repaintRect);
}

void RenderElement::updateOutlineAutoAncestor(bool hasOutlineAuto)
{
    for (auto& child : childrenOfType<RenderObject>(*this)) {
        if (hasOutlineAuto == child.hasOutlineAutoAncestor())
            continue;
        child.setHasOutlineAutoAncestor(hasOutlineAuto);
        bool childHasOutlineAuto = child.outlineStyleForRepaint().outlineStyleIsAuto();
        if (childHasOutlineAuto)
            continue;
        if (!is<RenderElement>(child))
            continue;
        downcast<RenderElement>(child).updateOutlineAutoAncestor(hasOutlineAuto);
    }
    if (hasContinuation())
        downcast<RenderBoxModelObject>(*this).continuation()->updateOutlineAutoAncestor(hasOutlineAuto);
}

bool RenderElement::hasOutlineAnnotation() const
{
    return element() && element()->isLink() && document().printing();
}

bool RenderElement::hasSelfPaintingLayer() const
{
    if (!hasLayer())
        return false;
    auto& layerModelObject = downcast<RenderLayerModelObject>(*this);
    return layerModelObject.hasSelfPaintingLayer();
}

bool RenderElement::checkForRepaintDuringLayout() const
{
    if (document().view()->layoutContext().needsFullRepaint() || !everHadLayout() || hasSelfPaintingLayer())
        return false;
    return !settings().repaintOutsideLayoutEnabled();
}

RespectImageOrientationEnum RenderElement::shouldRespectImageOrientation() const
{
#if USE(CG) || USE(CAIRO)
    // This can only be enabled for ports which honor the orientation flag in their drawing code.
    if (document().isImageDocument())
        return RespectImageOrientation;
#endif
    // Respect the image's orientation if it's being used as a full-page image or it's
    // an <img> and the setting to respect it everywhere is set.
    return settings().shouldRespectImageOrientation() && is<HTMLImageElement>(element()) ? RespectImageOrientation : DoNotRespectImageOrientation;
}

void RenderElement::adjustFragmentedFlowStateOnContainingBlockChangeIfNeeded()
{
    if (fragmentedFlowState() == NotInsideFragmentedFlow)
        return;

    // Invalidate the containing block caches.
    if (is<RenderBlock>(*this))
        downcast<RenderBlock>(*this).resetEnclosingFragmentedFlowAndChildInfoIncludingDescendants();
    
    // Adjust the flow tread state on the subtree.
    setFragmentedFlowState(RenderObject::computedFragmentedFlowState(*this));
    for (auto& descendant : descendantsOfType<RenderObject>(*this))
        descendant.setFragmentedFlowState(RenderObject::computedFragmentedFlowState(descendant));
}

void RenderElement::removeFromRenderFragmentedFlow()
{
    ASSERT(fragmentedFlowState() != NotInsideFragmentedFlow);
    // Sometimes we remove the element from the flow, but it's not destroyed at that time.
    // It's only until later when we actually destroy it and remove all the children from it.
    // Currently, that happens for firstLetter elements and list markers.
    // Pass in the flow thread so that we don't have to look it up for all the children.
    removeFromRenderFragmentedFlowIncludingDescendants(true);
}

void RenderElement::removeFromRenderFragmentedFlowIncludingDescendants(bool shouldUpdateState)
{
    // Once we reach another flow thread we don't need to update the flow thread state
    // but we have to continue cleanup the flow thread info.
    if (isRenderFragmentedFlow())
        shouldUpdateState = false;

    for (auto& child : childrenOfType<RenderObject>(*this)) {
        if (is<RenderElement>(child)) {
            downcast<RenderElement>(child).removeFromRenderFragmentedFlowIncludingDescendants(shouldUpdateState);
            continue;
        }
        if (shouldUpdateState)
            child.setFragmentedFlowState(NotInsideFragmentedFlow);
    }

    // We have to ask for our containing flow thread as it may be above the removed sub-tree.
    RenderFragmentedFlow* enclosingFragmentedFlow = this->enclosingFragmentedFlow();
    while (enclosingFragmentedFlow) {
        enclosingFragmentedFlow->removeFlowChildInfo(*this);

        if (enclosingFragmentedFlow->fragmentedFlowState() == NotInsideFragmentedFlow)
            break;
        auto* parent = enclosingFragmentedFlow->parent();
        if (!parent)
            break;
        enclosingFragmentedFlow = parent->enclosingFragmentedFlow();
    }
    if (is<RenderBlock>(*this))
        downcast<RenderBlock>(*this).setCachedEnclosingFragmentedFlowNeedsUpdate();

    if (shouldUpdateState)
        setFragmentedFlowState(NotInsideFragmentedFlow);
}

void RenderElement::resetEnclosingFragmentedFlowAndChildInfoIncludingDescendants(RenderFragmentedFlow* fragmentedFlow)
{
    if (fragmentedFlow)
        fragmentedFlow->removeFlowChildInfo(*this);

    for (auto& child : childrenOfType<RenderElement>(*this))
        child.resetEnclosingFragmentedFlowAndChildInfoIncludingDescendants(fragmentedFlow);
}

#if ENABLE(TEXT_AUTOSIZING)
static RenderObject::BlockContentHeightType includeNonFixedHeight(const RenderObject& renderer)
{
    const RenderStyle& style = renderer.style();
    if (style.height().type() == Fixed) {
        if (is<RenderBlock>(renderer)) {
            // For fixed height styles, if the overflow size of the element spills out of the specified
            // height, assume we can apply text auto-sizing.
            if (style.overflowY() == OVISIBLE
                && style.height().value() < downcast<RenderBlock>(renderer).layoutOverflowRect().maxY())
                return RenderObject::OverflowHeight;
        }
        return RenderObject::FixedHeight;
    }
    return RenderObject::FlexibleHeight;
}

void RenderElement::adjustComputedFontSizesOnBlocks(float size, float visibleWidth)
{
    Document* document = view().frameView().frame().document();
    if (!document)
        return;

    Vector<int> depthStack;
    int currentDepth = 0;
    int newFixedDepth = 0;

    // We don't apply autosizing to nodes with fixed height normally.
    // But we apply it to nodes which are located deep enough
    // (nesting depth is greater than some const) inside of a parent block
    // which has fixed height but its content overflows intentionally.
    for (RenderObject* descendent = traverseNext(this, includeNonFixedHeight, currentDepth, newFixedDepth); descendent; descendent = descendent->traverseNext(this, includeNonFixedHeight, currentDepth, newFixedDepth)) {
        while (depthStack.size() > 0 && currentDepth <= depthStack[depthStack.size() - 1])
            depthStack.remove(depthStack.size() - 1);
        if (newFixedDepth)
            depthStack.append(newFixedDepth);

        int stackSize = depthStack.size();
        if (is<RenderBlockFlow>(*descendent) && !descendent->isListItem() && (!stackSize || currentDepth - depthStack[stackSize - 1] > TextAutoSizingFixedHeightDepth))
            downcast<RenderBlockFlow>(*descendent).adjustComputedFontSizes(size, visibleWidth);
        newFixedDepth = 0;
    }

    // Remove style from auto-sizing table that are no longer valid.
    document->textAutoSizing().updateRenderTree();
}

void RenderElement::resetTextAutosizing()
{
    Document* document = view().frameView().frame().document();
    if (!document)
        return;

    LOG(TextAutosizing, "RenderElement::resetTextAutosizing()");

    document->textAutoSizing().reset();

    Vector<int> depthStack;
    int currentDepth = 0;
    int newFixedDepth = 0;

    for (RenderObject* descendent = traverseNext(this, includeNonFixedHeight, currentDepth, newFixedDepth); descendent; descendent = descendent->traverseNext(this, includeNonFixedHeight, currentDepth, newFixedDepth)) {
        while (depthStack.size() > 0 && currentDepth <= depthStack[depthStack.size() - 1])
            depthStack.remove(depthStack.size() - 1);
        if (newFixedDepth)
            depthStack.append(newFixedDepth);

        int stackSize = depthStack.size();
        if (is<RenderBlockFlow>(*descendent) && !descendent->isListItem() && (!stackSize || currentDepth - depthStack[stackSize - 1] > TextAutoSizingFixedHeightDepth))
            downcast<RenderBlockFlow>(*descendent).resetComputedFontSize();
        newFixedDepth = 0;
    }
}
#endif // ENABLE(TEXT_AUTOSIZING)

}
