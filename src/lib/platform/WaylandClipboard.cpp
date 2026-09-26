/*
 * Inputgate -- mouse, keyboard, clipboard and file sharing utility
 * Copyright (C) 2026 The Inputgate Developers
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/WaylandClipboard.h"
#include "platform/WaylandImageConverter.h"

#include "inputleap/Clipboard.h"
#include "inputleap/FileBundle.h"
#include "base/Log.h"

#include <wayland-client.h>
#include "ext-data-control-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace inputleap {

namespace {

// Offered on every selection we set, so we can recognize our own selection
// when the compositor announces it back to us.
const char* const kOwnMarkerMime = "application/x-inputgate-source";

const char* const kTextMimes[] = {
    "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "STRING", "TEXT",
};

const char* const kUriListMime = "text/uri-list";
const char* const kGnomeFilesMime = "x-special/gnome-copied-files";

// Bytes per MIME type
using MimeData = std::map<std::string, std::string>;

const char* image_mime(IClipboard::EFormat format)
{
    switch (format) {
        case IClipboard::kPNG: return "image/png";
        case IClipboard::kJpeg: return "image/jpeg";
        case IClipboard::kTiff: return "image/tiff";
        case IClipboard::kWebp: return "image/webp";
        default: return nullptr;
    }
}

std::uint32_t read_le32(const std::string& data, std::size_t offset)
{
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | static_cast<std::uint8_t>(data[offset + i]);
    }
    return v;
}

std::uint16_t read_le16(const std::string& data, std::size_t offset)
{
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[offset]) |
                                      (static_cast<std::uint8_t>(data[offset + 1]) << 8));
}

void append_le32(std::string& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out += static_cast<char>((v >> (8 * i)) & 0xff);
    }
}

// IClipboard::kBitmap is a BMP without its 14 byte file header, add it back
std::string dib_to_bmp(const std::string& dib)
{
    if (dib.size() < 40) {
        return {};
    }
    std::uint32_t header_size = read_le32(dib, 0);
    std::uint16_t bit_count = read_le16(dib, 14);
    std::uint32_t compression = read_le32(dib, 16);
    std::uint32_t colors_used = read_le32(dib, 32);

    std::uint32_t offset = 14 + header_size;
    if (compression == 3 /* BI_BITFIELDS */ && header_size == 40) {
        offset += 12;
    }
    if (colors_used != 0) {
        offset += colors_used * 4;
    } else if (bit_count <= 8) {
        offset += (1u << bit_count) * 4;
    }

    std::string bmp = "BM";
    append_le32(bmp, static_cast<std::uint32_t>(14 + dib.size()));
    append_le32(bmp, 0);
    append_le32(bmp, offset);
    bmp += dib;
    return bmp;
}

std::string bmp_to_dib(const std::string& bmp)
{
    if (bmp.size() <= 14 || bmp[0] != 'B' || bmp[1] != 'M') {
        return {};
    }
    return bmp.substr(14);
}

// Reads everything from fd until EOF. Gives up if the other side stalls.
bool read_all(int fd, std::string& out)
{
    const int timeout_ms = 5000;
    const std::size_t max_size = 0xF0000000ULL;
    char buf[64 * 1024];
    out.clear();
    for (;;) {
        pollfd pfd{fd, POLLIN, 0};
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc == 0) {
            LOG_WARN("wayland clipboard: timed out reading clipboard data");
            return false;
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) {
            return true;
        }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return false;
        }
        out.append(buf, static_cast<std::size_t>(n));
        if (out.size() > max_size) {
            LOG_WARN("wayland clipboard: clipboard data too large");
            return false;
        }
    }
}

// Writes data to fd on a separate thread so a slow reader can't block the
// Wayland connection, then closes fd.
void write_async(int fd, std::shared_ptr<const MimeData> data, std::string mime)
{
    std::thread([fd, data, mime]() {
        // A reader that goes away must give us EPIPE, not kill the process
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);

        int flags = fcntl(fd, F_GETFL);
        if (flags != -1) {
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        }

        auto it = data->find(mime);
        if (it != data->end()) {
            const std::string& bytes = it->second;
            std::size_t written = 0;
            while (written < bytes.size()) {
                ssize_t n = write(fd, bytes.data() + written, bytes.size() - written);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    break;
                }
                written += static_cast<std::size_t>(n);
            }
        }
        close(fd);
    }).detach();
}

} // namespace

// Protocol independent part of the data-control implementation
class DataControlBase {
public:
    virtual ~DataControlBase() = default;
    virtual const char* name() const = 0;
    virtual void create_device(wl_seat* seat) = 0;
    virtual bool has_device() const = 0;
    virtual bool supports_primary() const = 0;
    // Returns the read end of a pipe the current selection is written to, or -1
    virtual int receive(ClipboardID id, const std::string& mime) = 0;
    virtual void set_selection(ClipboardID id, std::shared_ptr<const MimeData> data) = 0;
};

struct WaylandClipboard::Impl {
    explicit Impl(GrabbedCallback cb) : on_grabbed(std::move(cb)) {}

    GrabbedCallback on_grabbed;

    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    std::unique_ptr<DataControlBase> control;

    std::thread thread;
    std::atomic<bool> stop{false};
    int wake_pipe[2] = { -1, -1 };

    std::mutex tasks_mutex;
    std::deque<std::function<void()>> tasks;
    bool thread_done = false; // guarded by tasks_mutex

    // Only touched on the Wayland thread
    bool seen_initial[kClipboardEnd] = { false, false };

    std::mutex state_mutex;
    struct Selection {
        std::vector<std::string> mimes;
        bool is_own = false;
        IClipboard::Time time = 0;
    };
    Selection selections[kClipboardEnd];
    // What we last set, returned when asked for our own selection
    std::unique_ptr<Clipboard> own_contents[kClipboardEnd];

    void connect();
    void run();
    void wake();
    template<class F> auto run_sync(F&& fn) -> decltype(fn());

    // Called by DataControl on the Wayland thread
    void on_selection(ClipboardID id, const std::vector<std::string>& mimes);

    static void registry_global(void* data, wl_registry* registry, std::uint32_t name,
                                const char* interface, std::uint32_t version);
    static void registry_global_remove(void*, wl_registry*, std::uint32_t) {}
};

namespace {

/*  ext-data-control-v1 and wlr-data-control-unstable-v1 are the same protocol
    under different names. The traits below give both the same C++ spelling so
    one template implements them. */
#define INPUTGATE_DATA_CONTROL_TRAITS(TraitsName, prefix, proto_name, primary_version)    \
    struct TraitsName {                                                                   \
        using Manager = prefix##_manager_v1;                                              \
        using Device = prefix##_device_v1;                                                \
        using Source = prefix##_source_v1;                                                \
        using Offer = prefix##_offer_v1;                                                  \
        using DeviceListener = prefix##_device_v1_listener;                               \
        using SourceListener = prefix##_source_v1_listener;                               \
        using OfferListener = prefix##_offer_v1_listener;                                 \
        static constexpr const char* name = proto_name;                                   \
        static constexpr std::uint32_t kPrimaryVersion = primary_version;                 \
        static const wl_interface* manager_interface()                                    \
            { return &prefix##_manager_v1_interface; }                                    \
        static Device* get_device(Manager* m, wl_seat* s)                                 \
            { return prefix##_manager_v1_get_data_device(m, s); }                         \
        static Source* create_source(Manager* m)                                          \
            { return prefix##_manager_v1_create_data_source(m); }                         \
        static void destroy_manager(Manager* m) { prefix##_manager_v1_destroy(m); }       \
        static void add_listener(Device* d, const DeviceListener* l, void* data)          \
            { prefix##_device_v1_add_listener(d, l, data); }                              \
        static void set_selection(Device* d, Source* s)                                   \
            { prefix##_device_v1_set_selection(d, s); }                                   \
        static void set_primary_selection(Device* d, Source* s)                           \
            { prefix##_device_v1_set_primary_selection(d, s); }                           \
        static void destroy_device(Device* d) { prefix##_device_v1_destroy(d); }          \
        static void add_listener(Source* s, const SourceListener* l, void* data)          \
            { prefix##_source_v1_add_listener(s, l, data); }                              \
        static void offer(Source* s, const char* mime) { prefix##_source_v1_offer(s, mime); } \
        static void destroy_source(Source* s) { prefix##_source_v1_destroy(s); }          \
        static void add_listener(Offer* o, const OfferListener* l, void* data)            \
            { prefix##_offer_v1_add_listener(o, l, data); }                               \
        static void receive(Offer* o, const char* mime, int fd)                           \
            { prefix##_offer_v1_receive(o, mime, fd); }                                   \
        static void destroy_offer(Offer* o) { prefix##_offer_v1_destroy(o); }             \
    };

INPUTGATE_DATA_CONTROL_TRAITS(ExtTraits, ext_data_control, "ext_data_control_manager_v1", 1)
INPUTGATE_DATA_CONTROL_TRAITS(WlrTraits, zwlr_data_control, "zwlr_data_control_manager_v1", 2)

#undef INPUTGATE_DATA_CONTROL_TRAITS

template<class T>
class DataControl : public DataControlBase {
public:
    DataControl(WaylandClipboard::Impl& owner, typename T::Manager* manager, std::uint32_t version) :
        owner_(owner), manager_(manager), version_(version) {}

    ~DataControl() override
    {
        for (auto& entry : sources_) {
            T::destroy_source(entry.first);
        }
        for (auto& entry : offers_) {
            T::destroy_offer(entry.first);
        }
        if (device_) {
            T::destroy_device(device_);
        }
        T::destroy_manager(manager_);
    }

    const char* name() const override { return T::name; }

    void create_device(wl_seat* seat) override
    {
        device_ = T::get_device(manager_, seat);
        T::add_listener(device_, &device_listener, this);
    }

    bool has_device() const override { return device_ != nullptr; }

    bool supports_primary() const override { return version_ >= T::kPrimaryVersion; }

    int receive(ClipboardID id, const std::string& mime) override
    {
        auto* offer = current_[id];
        if (offer == nullptr) {
            return -1;
        }
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) {
            return -1;
        }
        T::receive(offer, mime.c_str(), fds[1]);
        close(fds[1]);
        return fds[0];
    }

    void set_selection(ClipboardID id, std::shared_ptr<const MimeData> data) override
    {
        if (device_ == nullptr || (id == kClipboardSelection && !supports_primary())) {
            return;
        }
        auto* source = T::create_source(manager_);
        T::add_listener(source, &source_listener, this);
        for (const auto& entry : *data) {
            T::offer(source, entry.first.c_str());
        }
        sources_[source] = std::move(data);

        if (id == kClipboardClipboard) {
            T::set_selection(device_, source);
        } else {
            T::set_primary_selection(device_, source);
        }
    }

private:
    void set_current(ClipboardID id, typename T::Offer* offer)
    {
        auto* old = current_[id];
        current_[id] = offer;
        ClipboardID other = id == kClipboardClipboard ? kClipboardSelection : kClipboardClipboard;
        if (old != nullptr && old != offer && old != current_[other]) {
            T::destroy_offer(old);
            offers_.erase(old);
        }

        std::vector<std::string> mimes;
        if (offer != nullptr) {
            mimes = offers_[offer];
        }
        owner_.on_selection(id, mimes);
    }

    // device events
    static void on_data_offer(void* data, typename T::Device*, typename T::Offer* offer)
    {
        auto* self = static_cast<DataControl*>(data);
        self->offers_[offer] = {};
        T::add_listener(offer, &offer_listener, self);
    }

    static void on_selection(void* data, typename T::Device*, typename T::Offer* offer)
    {
        static_cast<DataControl*>(data)->set_current(kClipboardClipboard, offer);
    }

    static void on_primary_selection(void* data, typename T::Device*, typename T::Offer* offer)
    {
        static_cast<DataControl*>(data)->set_current(kClipboardSelection, offer);
    }

    static void on_finished(void* data, typename T::Device* device)
    {
        auto* self = static_cast<DataControl*>(data);
        LOG_WARN("wayland clipboard: data control device finished, clipboard sharing stops");
        T::destroy_device(device);
        self->device_ = nullptr;
    }

    // offer events
    static void on_offer(void* data, typename T::Offer* offer, const char* mime)
    {
        static_cast<DataControl*>(data)->offers_[offer].emplace_back(mime);
    }

    // source events
    static void on_send(void* data, typename T::Source* source, const char* mime, std::int32_t fd)
    {
        auto* self = static_cast<DataControl*>(data);
        auto it = self->sources_.find(source);
        if (it == self->sources_.end()) {
            close(fd);
            return;
        }
        write_async(fd, it->second, mime);
    }

    static void on_cancelled(void* data, typename T::Source* source)
    {
        auto* self = static_cast<DataControl*>(data);
        self->sources_.erase(source);
        T::destroy_source(source);
    }

    static const typename T::DeviceListener device_listener;
    static const typename T::OfferListener offer_listener;
    static const typename T::SourceListener source_listener;

    WaylandClipboard::Impl& owner_;
    typename T::Manager* manager_ = nullptr;
    typename T::Device* device_ = nullptr;
    std::uint32_t version_ = 0;
    std::map<typename T::Offer*, std::vector<std::string>> offers_;
    typename T::Offer* current_[kClipboardEnd] = { nullptr, nullptr };
    std::map<typename T::Source*, std::shared_ptr<const MimeData>> sources_;
};

template<class T>
const typename T::DeviceListener DataControl<T>::device_listener = {
    &DataControl<T>::on_data_offer,
    &DataControl<T>::on_selection,
    &DataControl<T>::on_finished,
    &DataControl<T>::on_primary_selection,
};

template<class T>
const typename T::OfferListener DataControl<T>::offer_listener = {
    &DataControl<T>::on_offer,
};

template<class T>
const typename T::SourceListener DataControl<T>::source_listener = {
    &DataControl<T>::on_send,
    &DataControl<T>::on_cancelled,
};

const wl_registry_listener registry_listener = {
    &WaylandClipboard::Impl::registry_global,
    &WaylandClipboard::Impl::registry_global_remove,
};

// Registry scan results, before we pick a protocol
struct Globals {
    WaylandClipboard::Impl* impl = nullptr;
    std::uint32_t ext_name = 0, ext_version = 0;
    std::uint32_t wlr_name = 0, wlr_version = 0;
    std::uint32_t seat_name = 0, seat_version = 0;
};

} // namespace

void WaylandClipboard::Impl::registry_global(void* data, wl_registry*, std::uint32_t name,
                                             const char* interface, std::uint32_t version)
{
    auto* globals = static_cast<Globals*>(data);
    if (std::strcmp(interface, ExtTraits::name) == 0) {
        globals->ext_name = name;
        globals->ext_version = version;
    } else if (std::strcmp(interface, WlrTraits::name) == 0) {
        globals->wlr_name = name;
        globals->wlr_version = version;
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 && globals->seat_name == 0) {
        // data-control is per seat; the first seat is the one that matters
        globals->seat_name = name;
        globals->seat_version = version;
    }
}

void WaylandClipboard::Impl::connect()
{
    display = wl_display_connect(nullptr);
    if (display == nullptr) {
        throw std::runtime_error("can't connect to the Wayland display");
    }

    Globals globals;
    globals.impl = this;
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &globals);
    wl_display_roundtrip(display);

    if (globals.seat_name == 0) {
        throw std::runtime_error("the compositor has no seat");
    }
    seat = static_cast<wl_seat*>(wl_registry_bind(registry, globals.seat_name, &wl_seat_interface,
                                                  std::min<std::uint32_t>(globals.seat_version, 2)));

    // INPUTGATE_DATA_CONTROL=wlr forces the older protocol, for debugging
    const char* forced = std::getenv("INPUTGATE_DATA_CONTROL");
    if (forced && std::strcmp(forced, "wlr") == 0) {
        globals.ext_name = 0;
    }

    if (globals.ext_name != 0) {
        auto version = std::min<std::uint32_t>(globals.ext_version, 1);
        auto* manager = static_cast<ExtTraits::Manager*>(
            wl_registry_bind(registry, globals.ext_name, ExtTraits::manager_interface(), version));
        control = std::make_unique<DataControl<ExtTraits>>(*this, manager, version);
    } else if (globals.wlr_name != 0) {
        auto version = std::min<std::uint32_t>(globals.wlr_version, 2);
        auto* manager = static_cast<WlrTraits::Manager*>(
            wl_registry_bind(registry, globals.wlr_name, WlrTraits::manager_interface(), version));
        control = std::make_unique<DataControl<WlrTraits>>(*this, manager, version);
    } else {
        throw std::runtime_error("the compositor doesn't support ext-data-control-v1 or "
                                 "wlr-data-control-unstable-v1 (GNOME doesn't)");
    }

    // Everything we need is bound; drop the registry so its listener, which
    // points at the local Globals, never fires again
    wl_registry_destroy(registry);
    registry = nullptr;

    control->create_device(seat);
    // Receive the initial selection before the thread starts
    wl_display_roundtrip(display);

    LOG_NOTE("wayland clipboard: using %s%s", control->name(),
             control->supports_primary() ? " with primary selection" : "");
}

void WaylandClipboard::Impl::on_selection(ClipboardID id, const std::vector<std::string>& mimes)
{
    bool is_own = std::find(mimes.begin(), mimes.end(), kOwnMarkerMime) != mimes.end();
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        auto& selection = selections[id];
        selection.mimes = mimes;
        selection.is_own = is_own;
        ++selection.time;
    }

    // The selection that exists when we start isn't a new copy by the user
    bool initial = !seen_initial[id];
    seen_initial[id] = true;

    if (!is_own && !mimes.empty() && !initial) {
        LOG_DEBUG("wayland clipboard: clipboard %d changed (%zu types)", id, mimes.size());
        on_grabbed(id);
    }
}

void WaylandClipboard::Impl::wake()
{
    char c = '!';
    (void)!write(wake_pipe[1], &c, 1);
}

template<class F>
auto WaylandClipboard::Impl::run_sync(F&& fn) -> decltype(fn())
{
    using R = decltype(fn());
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    auto result = task->get_future();
    {
        std::lock_guard<std::mutex> lock(tasks_mutex);
        if (thread_done) {
            // no Wayland thread to hand this to; the task checks `stop`
            (*task)();
            return result.get();
        }
        tasks.emplace_back([task]() { (*task)(); });
    }
    wake();
    return result.get();
}

void WaylandClipboard::Impl::run()
{
    int display_fd = wl_display_get_fd(display);

    while (!stop) {
        std::deque<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex);
            pending.swap(tasks);
        }
        for (auto& task : pending) {
            task();
        }

        while (wl_display_prepare_read(display) != 0) {
            wl_display_dispatch_pending(display);
        }
        wl_display_flush(display);

        pollfd pfds[2] = {
            { display_fd, POLLIN, 0 },
            { wake_pipe[0], POLLIN, 0 },
        };
        int rc = poll(pfds, 2, -1);
        if (rc > 0 && (pfds[0].revents & POLLIN)) {
            if (wl_display_read_events(display) != 0) {
                LOG_ERR("wayland clipboard: lost the connection to the compositor");
                break;
            }
        } else {
            wl_display_cancel_read(display);
        }
        if (pfds[0].revents & (POLLERR | POLLHUP)) {
            LOG_ERR("wayland clipboard: lost the connection to the compositor");
            break;
        }
        if (pfds[1].revents & POLLIN) {
            char buf[64];
            while (read(wake_pipe[0], buf, sizeof(buf)) > 0) {}
        }
        if (wl_display_dispatch_pending(display) < 0) {
            LOG_ERR("wayland clipboard: protocol error %d", wl_display_get_error(display));
            break;
        }
    }

    // Anything still waiting gets run so its caller doesn't hang; the
    // connection may be gone, so the results are ignored.
    stop = true;
    std::lock_guard<std::mutex> lock(tasks_mutex);
    thread_done = true;
    for (auto& task : tasks) {
        task();
    }
    tasks.clear();
}

WaylandClipboard::WaylandClipboard(GrabbedCallback on_grabbed) :
    impl_(std::make_unique<Impl>(std::move(on_grabbed)))
{
    if (pipe2(impl_->wake_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        throw std::runtime_error("can't create pipe");
    }
    try {
        impl_->connect();
    } catch (...) {
        impl_->control.reset();
        if (impl_->display) {
            wl_display_disconnect(impl_->display);
        }
        close(impl_->wake_pipe[0]);
        close(impl_->wake_pipe[1]);
        throw;
    }
    impl_->thread = std::thread([this]() { impl_->run(); });
}

WaylandClipboard::~WaylandClipboard()
{
    impl_->stop = true;
    impl_->wake();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    impl_->control.reset();
    if (impl_->seat) {
        wl_seat_destroy(impl_->seat);
    }
    if (impl_->registry) {
        wl_registry_destroy(impl_->registry);
    }
    wl_display_disconnect(impl_->display);
    close(impl_->wake_pipe[0]);
    close(impl_->wake_pipe[1]);
}

bool WaylandClipboard::get(ClipboardID id, IClipboard* clipboard) const
{
    if (id >= kClipboardEnd || impl_->stop) {
        return false;
    }

    Impl::Selection selection;
    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        selection = impl_->selections[id];
        if (selection.is_own && impl_->own_contents[id]) {
            return Clipboard::copy(clipboard, impl_->own_contents[id].get(), selection.time);
        }
    }

    auto has = [&](const std::string& mime) {
        return std::find(selection.mimes.begin(), selection.mimes.end(), mime) !=
               selection.mimes.end();
    };

    auto fetch = [&](const std::string& mime, std::string& out) {
        int fd = impl_->run_sync([&]() {
            return impl_->stop ? -1 : impl_->control->receive(id, mime);
        });
        if (fd < 0) {
            return false;
        }
        bool ok = read_all(fd, out);
        close(fd);
        return ok;
    };

    Clipboard snapshot;
    snapshot.open(selection.time);
    snapshot.clear();

    std::string data;
    for (const char* mime : kTextMimes) {
        if (has(mime) && fetch(mime, data)) {
            snapshot.add(IClipboard::kText, data);
            break;
        }
    }
    if (has("text/html") && fetch("text/html", data)) {
        snapshot.add(IClipboard::kHTML, data);
    }
    for (auto format : { IClipboard::kPNG, IClipboard::kJpeg, IClipboard::kTiff,
                         IClipboard::kWebp }) {
        if (has(image_mime(format)) && fetch(image_mime(format), data)) {
            snapshot.add(format, data);
        }
    }
    if (has("image/bmp") && fetch("image/bmp", data)) {
        auto dib = bmp_to_dib(data);
        if (!dib.empty()) {
            snapshot.add(IClipboard::kBitmap, dib);
        }
    }
    // Linux apps usually only offer PNG, but many Windows apps (Paint, ...)
    // only paste bitmaps, so provide one
    if (!snapshot.has(IClipboard::kBitmap) && snapshot.has(IClipboard::kPNG)) {
        auto dib = WaylandImageConverter::png_to_dib(snapshot.get(IClipboard::kPNG));
        if (!dib.empty()) {
            snapshot.add(IClipboard::kBitmap, dib);
        }
    }
    if (has(kUriListMime) && fetch(kUriListMime, data)) {
        auto paths = FileBundle::from_uri_list(data);
        std::string bundle;
        if (!paths.empty() && FileBundle::pack(paths, bundle)) {
            snapshot.add(IClipboard::kFileList, bundle);
        }
    }

    snapshot.close();
    return Clipboard::copy(clipboard, &snapshot, selection.time);
}

bool WaylandClipboard::set(ClipboardID id, const IClipboard* clipboard)
{
    if (id >= kClipboardEnd || impl_->stop) {
        return false;
    }
    if (clipboard == nullptr) {
        // Another screen took the clipboard; its contents arrive later with
        // a set() call, keep ours until then.
        return true;
    }

    auto snapshot = std::make_unique<Clipboard>();
    Clipboard::copy(snapshot.get(), clipboard);

    auto data = std::make_shared<MimeData>();
    if (snapshot->open(0)) {
        if (snapshot->has(IClipboard::kText)) {
            auto text = snapshot->get(IClipboard::kText);
            for (const char* mime : kTextMimes) {
                (*data)[mime] = text;
            }
        }
        if (snapshot->has(IClipboard::kHTML)) {
            (*data)["text/html"] = snapshot->get(IClipboard::kHTML);
        }
        for (auto format : { IClipboard::kPNG, IClipboard::kJpeg, IClipboard::kTiff,
                             IClipboard::kWebp }) {
            if (snapshot->has(format)) {
                (*data)[image_mime(format)] = snapshot->get(format);
            }
        }
        if (snapshot->has(IClipboard::kBitmap)) {
            auto dib = snapshot->get(IClipboard::kBitmap);
            auto bmp = dib_to_bmp(dib);
            if (!bmp.empty()) {
                (*data)["image/bmp"] = bmp;
            }
            // Windows only sends bitmaps; most Linux apps want PNG
            if (!snapshot->has(IClipboard::kPNG)) {
                auto png = WaylandImageConverter::dib_to_png(dib);
                if (!png.empty()) {
                    (*data)["image/png"] = png;
                }
            }
        }
        if (snapshot->has(IClipboard::kFileList)) {
            auto paths = FileBundle::unpack(snapshot->get(IClipboard::kFileList));
            if (!paths.empty()) {
                auto uris = FileBundle::to_uri_list(paths);
                (*data)[kUriListMime] = uris;

                // GNOME style: "copy" followed by one URI per line
                std::string gnome = "copy";
                std::size_t start = 0;
                while (start < uris.size()) {
                    auto end = uris.find("\r\n", start);
                    gnome += "\n" + uris.substr(start, end - start);
                    start = end + 2;
                }
                (*data)[kGnomeFilesMime] = gnome;

                if (!snapshot->has(IClipboard::kText)) {
                    std::string text;
                    for (const auto& path : paths) {
                        text += (text.empty() ? "" : "\n") + path.u8string();
                    }
                    (*data)["text/plain;charset=utf-8"] = text;
                    (*data)["text/plain"] = text;
                }
            }
        }
        snapshot->close();
    }

    if (data->empty()) {
        return true;
    }
    (*data)[kOwnMarkerMime] = "1";

    {
        std::lock_guard<std::mutex> lock(impl_->state_mutex);
        impl_->own_contents[id] = std::move(snapshot);
    }

    std::shared_ptr<const MimeData> shared = data;
    impl_->run_sync([&]() {
        if (!impl_->stop) {
            impl_->control->set_selection(id, shared);
        }
    });
    return true;
}

} // namespace inputleap
