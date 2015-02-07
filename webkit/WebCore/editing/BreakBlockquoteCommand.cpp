

#include "config.h"
#include "BreakBlockquoteCommand.h"

#include "HTMLElement.h"
#include "HTMLNames.h"
#include "RenderListItem.h"
#include "Text.h"
#include "VisiblePosition.h"
#include "htmlediting.h"

namespace WebCore {

using namespace HTMLNames;

BreakBlockquoteCommand::BreakBlockquoteCommand(Document *document)
    : CompositeEditCommand(document)
{
}

void BreakBlockquoteCommand::doApply()
{
    if (endingSelection().isNone())
        return;
    
    // Delete the current selection.
    if (endingSelection().isRange())
        deleteSelection(false, false);
    
    // This is a scenario that should never happen, but we want to
    // make sure we don't dereference a null pointer below.    

    ASSERT(!endingSelection().isNone());
    
    if (endingSelection().isNone())
        return;
        
    VisiblePosition visiblePos = endingSelection().visibleStart();
    
    // pos is a position equivalent to the caret.  We use downstream() so that pos will 
    // be in the first node that we need to move (there are a few exceptions to this, see below).
    Position pos = endingSelection().start().downstream();
    
    // Find the top-most blockquote from the start.
    Element* topBlockquote = 0;
    for (Node *node = pos.node()->parentNode(); node; node = node->parentNode()) {
        if (isMailBlockquote(node))
            topBlockquote = static_cast<Element*>(node);
    }
    if (!topBlockquote || !topBlockquote->parentNode())
        return;
    
    RefPtr<Element> breakNode = createBreakElement(document());

    bool isLastVisPosInNode = isLastVisiblePositionInNode(visiblePos, topBlockquote);

    // If the position is at the beginning of the top quoted content, we don't need to break the quote.
    // Instead, insert the break before the blockquote, unless the position is as the end of the the quoted content.
    if (isFirstVisiblePositionInNode(visiblePos, topBlockquote) && !isLastVisPosInNode) {
        insertNodeBefore(breakNode.get(), topBlockquote);
        setEndingSelection(VisibleSelection(Position(breakNode.get(), 0), DOWNSTREAM));
        rebalanceWhitespace();   
        return;
    }
    
    // Insert a break after the top blockquote.
    insertNodeAfter(breakNode.get(), topBlockquote);

    // If we're inserting the break at the end of the quoted content, we don't need to break the quote.
    if (isLastVisPosInNode) {
        setEndingSelection(VisibleSelection(Position(breakNode.get(), 0), DOWNSTREAM));
        rebalanceWhitespace();
        return;
    }
    
    // Don't move a line break just after the caret.  Doing so would create an extra, empty paragraph
    // in the new blockquote.
    if (lineBreakExistsAtVisiblePosition(visiblePos))
        pos = pos.next();
        
    // Adjust the position so we don't split at the beginning of a quote.  
    while (isFirstVisiblePositionInNode(VisiblePosition(pos), nearestMailBlockquote(pos.node())))
        pos = pos.previous();
    
    // startNode is the first node that we need to move to the new blockquote.
    Node* startNode = pos.node();
        
    // Split at pos if in the middle of a text node.
    if (startNode->isTextNode()) {
        Text* textNode = static_cast<Text*>(startNode);
        if ((unsigned)pos.deprecatedEditingOffset() >= textNode->length()) {
            startNode = startNode->traverseNextNode();
            ASSERT(startNode);
        } else if (pos.deprecatedEditingOffset() > 0)
            splitTextNode(textNode, pos.deprecatedEditingOffset());
    } else if (pos.deprecatedEditingOffset() > 0) {
        Node* childAtOffset = startNode->childNode(pos.deprecatedEditingOffset());
        startNode = childAtOffset ? childAtOffset : startNode->traverseNextNode();
        ASSERT(startNode);
    }
    
    // If there's nothing inside topBlockquote to move, we're finished.
    if (!startNode->isDescendantOf(topBlockquote)) {
        setEndingSelection(VisibleSelection(VisiblePosition(Position(startNode, 0))));
        return;
    }
    
    // Build up list of ancestors in between the start node and the top blockquote.
    Vector<Element*> ancestors;    
    for (Element* node = startNode->parentElement(); node && node != topBlockquote; node = node->parentElement())
        ancestors.append(node);
    
    // Insert a clone of the top blockquote after the break.
    RefPtr<Element> clonedBlockquote = topBlockquote->cloneElementWithoutChildren();
    insertNodeAfter(clonedBlockquote.get(), breakNode.get());
    
    // Clone startNode's ancestors into the cloned blockquote.
    // On exiting this loop, clonedAncestor is the lowest ancestor
    // that was cloned (i.e. the clone of either ancestors.last()
    // or clonedBlockquote if ancestors is empty).
    RefPtr<Element> clonedAncestor = clonedBlockquote;
    for (size_t i = ancestors.size(); i != 0; --i) {
        RefPtr<Element> clonedChild = ancestors[i - 1]->cloneElementWithoutChildren();
        // Preserve list item numbering in cloned lists.
        if (clonedChild->isElementNode() && clonedChild->hasTagName(olTag)) {
            Node* listChildNode = i > 1 ? ancestors[i - 2] : startNode;
            // The first child of the cloned list might not be a list item element, 
            // find the first one so that we know where to start numbering.
            while (listChildNode && !listChildNode->hasTagName(liTag))
                listChildNode = listChildNode->nextSibling();
            if (listChildNode && listChildNode->renderer())
                setNodeAttribute(static_cast<Element*>(clonedChild.get()), startAttr, String::number(toRenderListItem(listChildNode->renderer())->value()));
        }
            
        appendNode(clonedChild.get(), clonedAncestor.get());
        clonedAncestor = clonedChild;
    }
    
    // Move the startNode and its siblings.
    Node *moveNode = startNode;
    while (moveNode) {
        Node *next = moveNode->nextSibling();
        removeNode(moveNode);
        appendNode(moveNode, clonedAncestor.get());
        moveNode = next;
    }

    if (!ancestors.isEmpty()) {
        // Split the tree up the ancestor chain until the topBlockquote
        // Throughout this loop, clonedParent is the clone of ancestor's parent.
        // This is so we can clone ancestor's siblings and place the clones
        // into the clone corresponding to the ancestor's parent.
        Element* ancestor;
        Element* clonedParent;
        for (ancestor = ancestors.first(), clonedParent = clonedAncestor->parentElement();
             ancestor && ancestor != topBlockquote;
             ancestor = ancestor->parentElement(), clonedParent = clonedParent->parentElement()) {
            moveNode = ancestor->nextSibling();
            while (moveNode) {
                Node *next = moveNode->nextSibling();
                removeNode(moveNode);
                appendNode(moveNode, clonedParent);
                moveNode = next;
            }
        }
        
        // If the startNode's original parent is now empty, remove it
        Node* originalParent = ancestors.first();
        if (!originalParent->hasChildNodes())
            removeNode(originalParent);
    }
    
    // Make sure the cloned block quote renders.
    addBlockPlaceholderIfNeeded(clonedBlockquote.get());
    
    // Put the selection right before the break.
    setEndingSelection(VisibleSelection(Position(breakNode.get(), 0), DOWNSTREAM));
    rebalanceWhitespace();
}

} // namespace WebCore
