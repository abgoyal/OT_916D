

#include "config.h"
#include "EmbeddedWidget.h"

#include <WebCore/Document.h>
#include <WebCore/Element.h>
#include <WebCore/FrameView.h>
#include <WebCore/RenderObject.h>

#include "MemoryStream.h"
#include "WebError.h"
#include "WebURLResponse.h"

using namespace WebCore;

PassRefPtr<EmbeddedWidget> EmbeddedWidget::create(IWebEmbeddedView* view, Element* element, HWND parentWindow, const IntSize& size)
{
    RefPtr<EmbeddedWidget> widget = adoptRef(new EmbeddedWidget(view, element));

    widget->createWindow(parentWindow, size);
    return widget.release();
}

EmbeddedWidget::~EmbeddedWidget()
{
    if (m_window)
        DestroyWindow(m_window);
}

bool EmbeddedWidget::createWindow(HWND parentWindow, const IntSize& size)
{
    ASSERT(!m_window);

    HWND window;

    SIZE pluginSize(size);

    HRESULT hr = m_view->createViewWindow((OLE_HANDLE)parentWindow, &pluginSize, (OLE_HANDLE*)&window);
        
    if (FAILED(hr) || !window)
        return false;

    m_window = window;
    return true;
}

void EmbeddedWidget::invalidateRect(const IntRect& rect)
{
    if (!m_window)
        return;

    RECT r = rect;
   ::InvalidateRect(m_window, &r, false);
}

void EmbeddedWidget::setFrameRect(const IntRect& rect)
{
    if (m_element->document()->printing())
        return;

    if (rect != frameRect())
        Widget::setFrameRect(rect);

    frameRectsChanged();
}

void EmbeddedWidget::frameRectsChanged()
{
    if (!parent())
        return;

    ASSERT(parent()->isFrameView());
    FrameView* frameView = static_cast<FrameView*>(parent());

    IntRect oldWindowRect = m_windowRect;
    IntRect oldClipRect = m_clipRect;

    m_windowRect = IntRect(frameView->contentsToWindow(frameRect().location()), frameRect().size());
    m_clipRect = windowClipRect();
    m_clipRect.move(-m_windowRect.x(), -m_windowRect.y());

    if (!m_window)
        return;

    if (m_windowRect == oldWindowRect && m_clipRect == oldClipRect)
        return;

    HRGN rgn;

    // To prevent flashes while scrolling, we disable drawing during the window
    // update process by clipping the window to the zero rect.

    bool clipToZeroRect = true;

    if (clipToZeroRect) {
        rgn = ::CreateRectRgn(0, 0, 0, 0);
        ::SetWindowRgn(m_window, rgn, FALSE);
    } else {
        rgn = ::CreateRectRgn(m_clipRect.x(), m_clipRect.y(), m_clipRect.right(), m_clipRect.bottom());
        ::SetWindowRgn(m_window, rgn, TRUE);
     }

     if (m_windowRect != oldWindowRect)
        ::MoveWindow(m_window, m_windowRect.x(), m_windowRect.y(), m_windowRect.width(), m_windowRect.height(), TRUE);

     if (clipToZeroRect) {
        rgn = ::CreateRectRgn(m_clipRect.x(), m_clipRect.y(), m_clipRect.right(), m_clipRect.bottom());
        ::SetWindowRgn(m_window, rgn, TRUE);
    }
}

void EmbeddedWidget::setFocus()
{
    if (m_window)
        SetFocus(m_window);

    Widget::setFocus();
}

void EmbeddedWidget::show()
{
    m_isVisible = true;

    if (m_attachedToWindow && m_window)
        ShowWindow(m_window, SW_SHOWNA);

    Widget::show();
}

void EmbeddedWidget::hide()
{
    m_isVisible = false;

    if (m_attachedToWindow && m_window)
        ShowWindow(m_window, SW_HIDE);

    Widget::hide();
}

IntRect EmbeddedWidget::windowClipRect() const
{
    // Start by clipping to our bounds.
    IntRect clipRect(m_windowRect);
    
    // Take our element and get the clip rect from the enclosing layer and frame view.
    RenderLayer* layer = m_element->renderer()->enclosingLayer();
    FrameView* parentView = m_element->document()->view();
    clipRect.intersect(parentView->windowClipRectForLayer(layer, true));

    return clipRect;
}

void EmbeddedWidget::setParent(ScrollView* parent)
{
    Widget::setParent(parent);

    if (!m_window)
        return;

    if (parent)
        return;

    // If the embedded window or one of its children have the focus, we need to 
    // clear it to prevent the web view window from being focused because that can
    // trigger a layout while the plugin element is being detached.
    HWND focusedWindow = ::GetFocus();
    if (m_window == focusedWindow || ::IsChild(m_window, focusedWindow))
        ::SetFocus(0);
}

void EmbeddedWidget::attachToWindow()
{
    if (m_attachedToWindow)
        return;

    m_attachedToWindow = true;
    if (m_isVisible && m_window)
        ShowWindow(m_window, SW_SHOWNA);
}

void EmbeddedWidget::detachFromWindow()
{
    if (!m_attachedToWindow)
        return;

    if (m_isVisible && m_window)
        ShowWindow(m_window, SW_HIDE);
    m_attachedToWindow = false;
}

void EmbeddedWidget::didReceiveResponse(const ResourceResponse& response)
{
    ASSERT(m_view);

    COMPtr<IWebURLResponse> urlResponse(AdoptCOM, WebURLResponse::createInstance(response));
    m_view->didReceiveResponse(urlResponse.get());
}

void EmbeddedWidget::didReceiveData(const char* data, int length)
{
    COMPtr<MemoryStream> stream = MemoryStream::createInstance(SharedBuffer::create(data, length));
    m_view->didReceiveData(stream.get());
}

void EmbeddedWidget::didFinishLoading()
{
    m_view->didFinishLoading();
}

void EmbeddedWidget::didFail(const ResourceError& error)
{
    COMPtr<IWebError> webError(AdoptCOM, WebError::createInstance(error));
    m_view->didFail(webError.get());
}
