// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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

#include "../boost-fix.h"

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/filesystem.hpp>

#include <thread>

#include "vst2.h"

/**
 * Encapsulate capturing the STDOUT or STDERR stream by opening a pipe and
 * reopening the passed file descriptor as one of the ends of the newly opened
 * pipe. This allows all output sent to be read from that pipe. This is needed
 * to capture all (debug) output from Wine and the hosted plugins so we can
 * prefix it with a timestamp and a group identifier and potentially write it to
 * a log file. Since the host application is run independently of the yabridge
 * instance that spawned it, this can't simply be done by the caller like we're
 * doing for Wine output in individually hosted plugins.
 */
class StdIoCapture {
   public:
    /**
     * Redirect all output sent to a file descriptor (e.g. `STDOUT_FILENO` or
     * `STDERR_FILENO`) to a pipe. `StdIoCapture::pipe` can be used to read from
     * this pipe.
     *
     * @param io_context The IO context to create the captured pipe stream on.
     * @param file_descriptor The file descriptor to remap.
     */
    StdIoCapture(boost::asio::io_context& io_context, int file_descriptor);

    StdIoCapture(const StdIoCapture&) = delete;
    StdIoCapture& operator=(const StdIoCapture&) = delete;

    /**
     * On cleanup, close the outgoing file descriptor from the pipe and restore
     * the original file descriptor for the captured stream.
     */
    ~StdIoCapture();

    /**
     * The pipe endpoint where all output from the original file descriptor gets
     * redirected to. This can be read from like any other `Boost.Asio` stream.
     */
    boost::asio::posix::stream_descriptor pipe;

   private:
    /**
     * The file descriptor of the stream we're capturing.
     */
    const int target_fd;

    /**
     * A copy of the original file descriptor. Will be used to undo
     * the capture when this object gets destroyed.
     */
    const int original_fd_copy;

    /**
     * The two file descriptors generated by the `pipe()` function call.
     * `pipe_fd[1]` is used to reopen/capture the passed file descriptor, and
     * `pipe_fd[0]` can be used to read the captured output from.
     */
    int pipe_fd[2];
};

/**
 * A 'plugin group' that listens on a _group socket_ for plugins to host in this
 * process. Once the plugin gets loaded into a new thread the actual bridging
 * process is identical to individually hosted plugins.
 *
 * An important detail worth mentioning here is that while this plugin group can
 * throw in the constructor when another process is already listening on the
 * socket, this should not be treated as an error. When using plugins groups,
 * yabridge will try to connect to the group socket on initialization and it
 * will launch a new group host process if it can't. If this is done for
 * multiple yabridge instances at the same time, then multiple group host
 * processes will be launched. Instead of using complicated inter-process
 * synchronization, we'll simply allow the processes to fail when another
 * process is already listening on the socket.
 */
class GroupBridge {
   public:
    /**
     * Create a plugin group by listening on the provided socket for incoming
     * plugin host requests.
     *
     * @param gruop_socket_path The path to the group socket endpoint. This path
     *   should be in the form of
     *   `/tmp/yabridge-group-<group_name>-<wine_prefix_id>-<architecture>.sock`
     *   where `<wine_prefix_id>` is a numerical hash as explained in the
     *   `create_logger_prefix()` function in `./group.cpp`.
     *
     * @throw boost::system::system_error If we can't listen on the socket.
     *
     * @note Creating an `GroupBridge` instance has the side effect that the
     *   STDOUT and STDERR streams of the current process will be redirected to
     *   a pipe so they can be properly written to a log file.
     */
    GroupBridge(boost::filesystem::path group_socket_path);

    ~GroupBridge();

    GroupBridge(const GroupBridge&) = delete;
    GroupBridge& operator=(const GroupBridge&) = delete;

    /**
     * Run a plugin's dispatcher and message loop, processing all events on the
     * main IO context. The plugin will have already been created in
     * `accept_requests` since it has to be initiated inside of the IO context's
     * thread. Called by proxy using `handle_plugin_dispatch_proxy()` in
     * `./group.cpp` because the Win32 `CreateThread` API only allows passing a
     * single pointer to the function and does not allow lambdas.
     *
     * Once the plugin has exited, this thread will then remove itself from the
     * `active_plugins` map. If this causes the vector to become empty, we will
     * terminate this process. This check will be delayed by a few seconds to
     * prevent having to constantly restart the group process during plugin
     * scanning.
     *
     * @param request Information about the plugin to launch, i.e. the path to
     *   the plugin and the path of the socket endpoint that will be used for
     *   communication.
     *
     * @note In the case that the process starts but no plugin gets initiated,
     *   then the process will never exit on its own. This should not happen
     *   though.
     */
    void handle_plugin_dispatch(const GroupRequest request);

    /**
     * Listen for new requests to spawn plugins within this process and handle
     * them accordingly. Will terminate once all plugins have exited.
     */
    void handle_incoming_connections();

    /**
     * Returns true if the message loop should not be run at this time. This is
     * necessary because hosts will always call either `effEditOpen()` and then
     * `effEditGetRect()` or the other way around. If the message loop is
     * handled in between these two actions, then some plugins will either
     * freeze or sometimes outright crash. Because every plugin has to be run
     * from the same thread, this is a simple way to synchronize blocking the
     * mesage loop between the different plugin instances.
     */
    bool should_postpone_message_loop();

   private:
    /**
     * Listen on the group socket for incoming requests to host a new plugin
     * within this group process. This will read a `GoupRequest` object
     * containing information about the plugin, reply with this process's PID so
     * the yabridge instance can tell if the plugin crashed during
     * initialization, and it will then try to initialize the plugin. After
     * intialization the plugin handling will be handed over to a new thread
     * running `handle_plugin_dispatch()`. Because of the way the Win32 API
     * works, all plugins have to be initialized from the same thread, and all
     * event handling and message loop interaction also has to be done from that
     * thread, which is why we initialize the plugin here and use the
     * `handle_dispatch_multi()` function to run events within the same
     * `plugin_context`.
     *
     * @see handle_plugin_dispatch
     */
    void accept_requests();

    /**
     * Continuously read from a pipe and write the output to the log file. Used
     * with the IO streams captured by `stdout_redirect` and `stderr_redirect`.
     *
     * TODO: Merge this with `PluginBridge::async_log_pipe_lines`
     *
     * @param pipe The pipe to read from.
     * @param buffer The stream buffer to write to.
     * @param prefix Text to prepend to the line before writing to the log.
     */
    void async_log_pipe_lines(boost::asio::posix::stream_descriptor& pipe,
                              boost::asio::streambuf& buffer,
                              std::string prefix);

    /**
     * The logging facility used for this group host process. Since we can't
     * identify which plugin is generating (debug) output, every line will only
     * be prefixed with the name of the group.
     */
    Logger logger;

    /**
     * The IO context that connections will be accepted on, and that any plugin
     * operations that may involve the Win32 mesasge loop (e.g. initialization
     * and most `AEffect::dispatcher()` calls) should be run on.
     */
    boost::asio::io_context plugin_context;
    /**
     * A seperate IO context that handles the STDIO redirect through
     * `StdIoCapture`. This is seperated the `plugin_context` above so that
     * STDIO capture does not get blocked by blocking GUI operations. Since
     * every GUI related operation should be run from the same thread, we can't
     * just add another thread to the main IO context.
     */
    boost::asio::io_context stdio_context;

    boost::asio::streambuf stdout_buffer;
    boost::asio::streambuf stderr_buffer;
    /**
     * Contains a pipe used for capturing this process's STDOUT stream. Needed
     * to be able to process the output generated by Wine and plugins and to be
     * able write it write it to an external log file.
     */
    StdIoCapture stdout_redirect;
    /**
     * Contains a pipe used for capturing this process's STDERR stream. Needed
     * to be able to process the output generated by Wine and plugins and to be
     * able write it write it to an external log file.
     */
    StdIoCapture stderr_redirect;
    /**
     * A thread that runs the `stdio_context` loop.
     */
    std::thread stdio_handler;

    boost::asio::local::stream_protocol::endpoint group_socket_endpoint;
    /**
     * The UNIX domain socket acceptor that will be used to listen for incoming
     * connections to spawn new plugins within this process.
     */
    boost::asio::local::stream_protocol::acceptor group_socket_acceptor;

    /**
     * A map of threads that are currently hosting a plugin within this process
     * along with their plugin instance. After a plugin has exited or its
     * initialization has failed, the thread handling it will remove itself from
     * this map. This is to keep track of the amount of plugins currently
     * running with their associated thread handles.
     *
     * TODO: Check again if we can just use std::thread here instead, that would
     *       make everything much simpler. `std::thread` was a problem with
     *       gdiplus in the past as Serum would randomly crash because calling
     *       conventions were nto being respected.
     */
    std::unordered_map<GroupRequest,
                       std::pair<Win32Thread, std::unique_ptr<Vst2Bridge>>>
        active_plugins;
    /**
     * A mutex to prevent two threads from simultaneously accessing the plugins
     * map, and also to prevent `handle_plugin_dispatch()` from terminating the
     * process because it thinks there are no active plugins left just as a new
     * plugin is being spawned.
     */
    std::mutex active_plugins_mutex;

    /**
     * A timer to defer shutting down the process, allowing for fast plugin
     * scanning without having to start a new group host process for each
     * plugin.
     *
     * @see handle_plugin_dispatch
     */
    boost::asio::steady_timer shutdown_timer;
};
