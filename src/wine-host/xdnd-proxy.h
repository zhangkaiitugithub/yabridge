// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <memory>

#include "boost-fix.h"

// Use the native version of xcb
#pragma push_macro("_WIN32")
#undef _WIN32
#include <xcb/xcb.h>
#pragma pop_macro("_WIN32")

#include <windows.h>
#include <boost/container/small_vector.hpp>

#include "utils.h"

/**
 * A simple, unmapped 1x1 proxy window we'll use for our Wine->X11 drag-and-drop
 * proxy so we can send and receive client messages.
 */
class ProxyWindow {
   public:
    /**
     * Create the proxy window.
     */
    ProxyWindow(std::shared_ptr<xcb_connection_t> x11_connection);

    /**
     * Destroy the window again when this object gets dropped.
     */
    ~ProxyWindow() noexcept;

    ProxyWindow(const ProxyWindow&) noexcept = delete;
    ProxyWindow& operator=(const ProxyWindow&) noexcept = delete;

    ProxyWindow(ProxyWindow&&) noexcept;
    ProxyWindow& operator=(ProxyWindow&&) noexcept;

   private:
    std::shared_ptr<xcb_connection_t> x11_connection;

   public:
    xcb_window_t window;

   private:
    bool is_moved = false;
};

/**
 * A simple wrapper that registers a WinEvents hook to listen for new windows
 * being created, and handles XDND client messages to achieve the behaviour
 * described in `WineXdndProxy::init_proxy()`.
 */
class WineXdndProxy {
   protected:
    /**
     * Initialize the proxy and register all hooks.
     */
    WineXdndProxy();

   public:
    /**
     * A sort of smart pointer for `WineXdndProxy`, similar to how the COM/VST3
     * pointers work. We want to unregister the hooks and drop the X11
     * connection when the last editor closes in a plugin group. This is not
     * strictly necessary, but there's an open X11 client limit and otherwise
     * opening and closing a bunch of editors would get you very close to that
     * limit.
     */
    class Handle {
       protected:
        /**
         * Before calling this, the reference count should be increased by one
         * in `WineXdndProxy::init_proxy()`.
         */
        Handle(WineXdndProxy* proxy);

       public:
        /**
         * Reduces the reference count by one, and frees `proxy` if this was the
         * last handle.
         */
        ~Handle() noexcept;

        Handle(const Handle&) noexcept;
        Handle& operator=(const Handle&) noexcept = default;

        Handle(Handle&&) noexcept;
        Handle& operator=(Handle&&) noexcept = default;

       private:
        WineXdndProxy* proxy;

        friend WineXdndProxy;
    };

    /**
     * Initialize the Wine->X11 drag-and-drop proxy. Calling this will hook into
     * Wine's OLE drag and drop system by listening for the creation of special
     * proxy windows created by the Wine server. When a drag and drop operation
     * is started, we will initiate the XDND protocol with the same file. This
     * will allow us to drag files from Wine windows to X11 applications,
     * something that's normally not possible. Calling this function more than
     * once doesn't have any effect, but this should still be called at least
     * once from every plugin host instance. Because the actual data is stored
     * in a COM object, we can only handle drag-and-drop coming form this
     * process.
     *
     * This is sort of a singleton but not quite, as the `WineXdndProxy` is only
     * alive for as long as there are open editors in this process. This is done
     * to avoid opening too many X11 connections.
     *
     * @note This function, like everything other GUI realted, should be called
     *   from the main thread that's running the Win32 message loop.
     */
    static WineXdndProxy::Handle get_handle();

    /**
     * Initiate the XDDN protocol by taking ownership of the `XdndSelection`
     * selection and setting up the event listeners.
     */
    void begin_xdnd(
        const boost::container::small_vector_base<std::string>& file_paths,
        HWND tracker_window);

    /**
     * Release ownership of the selection stop listening for X11 events.
     */
    void end_xdnd();

   private:
    /**
     * From another thread, constantly poll the mouse position until
     * `tracker_window` disappears, and then perform the drop if the mouse
     * cursor was last positioned over an XDND aware window. This is a
     * workaround for us not being able to grab the mouse cursor since Wine is
     * already doing that.
     */
    void run_xdnd_loop();

    /**
     * We need a dedicated X11 connection for our proxy because we can have
     * multiple open editors in a single process (e.g. when using VST3 plugins
     * or plugin groups), and client messages are sent to the X11 connection
     * that created the window. So we cannot just reuse the connection from the
     * editor.
     */
    std::shared_ptr<xcb_connection_t> x11_connection;

    /**
     * We need an unmapped proxy window to send and receive client messages for
     * the XDND protocol.
     */
    ProxyWindow proxy_window;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
    std::unique_ptr<std::remove_pointer_t<HWINEVENTHOOK>,
                    std::decay_t<decltype(&UnhookWinEvent)>>
        hook_handle;
#pragma GCC diagnostic pop

    /**
     * The files that are currently being dragged.
     */
    boost::container::small_vector<std::string, 4> dragged_file_paths;

    /**
     * Wine's tracker window for tracking the drag-and-drop operation. Normally
     * you would grab the mouse pointer when the drag-and-drop operation starts
     * so you can track what windows you are hovering over, but we cannot do
     * that because Wine is already doing just that. So instead we will
     * periodically poll the mouse position from another thread, and we'll
     * consider the disappearance of this window to mean that the drop has
     * either succeeded or cancelled (depending on whether or not Escape is
     * pressed).
     */
    HWND tracker_window;

    /**
     * We need to poll for mouse position changes from another thread, because
     * when the drag-and-drop operation starts Wine will be blocking the GUI
     * thread, so we cannot rely on the normal event loop.
     */
    Win32Thread xdnd_handler;

    // These are the atoms used for the XDND protocol, as described by
    // https://www.freedesktop.org/wiki/Specifications/XDND/#atomsandproperties
    xcb_atom_t xcb_xdnd_selection;
    xcb_atom_t xcb_xdnd_aware_property;
    xcb_atom_t xcb_xdnd_proxy_property;
};