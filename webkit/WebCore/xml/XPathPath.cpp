

#include "config.h"
#include "XPathPath.h"

#if ENABLE(XPATH)

#include "Document.h"
#include "XPathPredicate.h"
#include "XPathStep.h"
#include "XPathValue.h"

namespace WebCore {
namespace XPath {
        
Filter::Filter(Expression* expr, const Vector<Predicate*>& predicates)
    : m_expr(expr), m_predicates(predicates)
{
    setIsContextNodeSensitive(m_expr->isContextNodeSensitive());
    setIsContextPositionSensitive(m_expr->isContextPositionSensitive());
    setIsContextSizeSensitive(m_expr->isContextSizeSensitive());
}

Filter::~Filter()
{
    delete m_expr;
    deleteAllValues(m_predicates);
}

Value Filter::evaluate() const
{
    Value v = m_expr->evaluate();
    
    NodeSet& nodes = v.modifiableNodeSet();
    nodes.sort();

    EvaluationContext& evaluationContext = Expression::evaluationContext();
    for (unsigned i = 0; i < m_predicates.size(); i++) {
        NodeSet newNodes;
        evaluationContext.size = nodes.size();
        evaluationContext.position = 0;
        
        for (unsigned j = 0; j < nodes.size(); j++) {
            Node* node = nodes[j];
            
            evaluationContext.node = node;
            ++evaluationContext.position;
            
            if (m_predicates[i]->evaluate())
                newNodes.append(node);
        }
        nodes.swap(newNodes);
    }

    return v;
}

LocationPath::LocationPath()
    : m_absolute(false)
{
    setIsContextNodeSensitive(true);
}

LocationPath::~LocationPath()
{
    deleteAllValues(m_steps);
}

Value LocationPath::evaluate() const
{
    EvaluationContext& evaluationContext = Expression::evaluationContext();
    EvaluationContext backupContext = evaluationContext;
    // For absolute location paths, the context node is ignored - the
    // document's root node is used instead.
    Node* context = evaluationContext.node.get();
    if (m_absolute && context->nodeType() != Node::DOCUMENT_NODE) 
        context = context->ownerDocument();

    NodeSet nodes;
    nodes.append(context);
    evaluate(nodes);
    
    evaluationContext = backupContext;
    return Value(nodes, Value::adopt);
}

void LocationPath::evaluate(NodeSet& nodes) const
{
    bool resultIsSorted = nodes.isSorted();

    for (unsigned i = 0; i < m_steps.size(); i++) {
        Step* step = m_steps[i];
        NodeSet newNodes;
        HashSet<Node*> newNodesSet;

        bool needToCheckForDuplicateNodes = !nodes.subtreesAreDisjoint() || (step->axis() != Step::ChildAxis && step->axis() != Step::SelfAxis
            && step->axis() != Step::DescendantAxis && step->axis() != Step::DescendantOrSelfAxis && step->axis() != Step::AttributeAxis);

        if (needToCheckForDuplicateNodes)
            resultIsSorted = false;

        // This is a simplified check that can be improved to handle more cases.
        if (nodes.subtreesAreDisjoint() && (step->axis() == Step::ChildAxis || step->axis() == Step::SelfAxis))
            newNodes.markSubtreesDisjoint(true);

        for (unsigned j = 0; j < nodes.size(); j++) {
            NodeSet matches;
            step->evaluate(nodes[j], matches);

            if (!matches.isSorted())
                resultIsSorted = false;

            for (size_t nodeIndex = 0; nodeIndex < matches.size(); ++nodeIndex) {
                Node* node = matches[nodeIndex];
                if (!needToCheckForDuplicateNodes || newNodesSet.add(node).second)
                    newNodes.append(node);
            }
        }
        
        nodes.swap(newNodes);
    }

    nodes.markSorted(resultIsSorted);
}

void LocationPath::appendStep(Step* step)
{
    unsigned stepCount = m_steps.size();
    if (stepCount) {
        bool dropSecondStep;
        optimizeStepPair(m_steps[stepCount - 1], step, dropSecondStep);
        if (dropSecondStep) {
            delete step;
            return;
        }
    }
    step->optimize();
    m_steps.append(step);
}

void LocationPath::insertFirstStep(Step* step)
{
    if (m_steps.size()) {
        bool dropSecondStep;
        optimizeStepPair(step, m_steps[0], dropSecondStep);
        if (dropSecondStep) {
            delete m_steps[0];
            m_steps[0] = step;
            return;
        }
    }
    step->optimize();
    m_steps.insert(0, step);
}

Path::Path(Filter* filter, LocationPath* path)
    : m_filter(filter)
    , m_path(path)
{
    setIsContextNodeSensitive(filter->isContextNodeSensitive());
    setIsContextPositionSensitive(filter->isContextPositionSensitive());
    setIsContextSizeSensitive(filter->isContextSizeSensitive());
}

Path::~Path()
{
    delete m_filter;
    delete m_path;
}

Value Path::evaluate() const
{
    Value v = m_filter->evaluate();

    NodeSet& nodes = v.modifiableNodeSet();
    m_path->evaluate(nodes);
    
    return v;
}

}
}

#endif // ENABLE(XPATH)