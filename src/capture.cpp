#include "capture.hpp"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

namespace valinvite {
namespace {
using namespace winrt;
namespace W = winrt::Windows;

class ThreadDpiContext final {
public:
    ThreadDpiContext() noexcept
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~ThreadDpiContext() { if (previous_) SetThreadDpiAwarenessContext(previous_); }
    ThreadDpiContext(const ThreadDpiContext&) = delete;
    ThreadDpiContext& operator=(const ThreadDpiContext&) = delete;
private:
    DPI_AWARENESS_CONTEXT previous_{};
};

std::wstring hresultMessage(HRESULT hr) {
    return L"Windows Graphics Capture 初始化失败 (0x" + std::to_wstring(static_cast<unsigned long>(hr)) + L")";
}

W::Graphics::DirectX::Direct3D11::IDirect3DDevice createDirect3DDevice(ID3D11Device* device) {
    com_ptr<IDXGIDevice> dxgiDevice;
    check_hresult(device->QueryInterface(guid_of<IDXGIDevice>(), dxgiDevice.put_void()));
    com_ptr<::IInspectable> inspectable;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    return inspectable.as<W::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

W::Graphics::Capture::GraphicsCaptureItem createItem(HWND window) {
    using Item = W::Graphics::Capture::GraphicsCaptureItem;
    auto interop = get_activation_factory<Item, IGraphicsCaptureItemInterop>();
    Item item{nullptr};
    check_hresult(interop->CreateForWindow(window, guid_of<Item>(), put_abi(item)));
    return item;
}

com_ptr<ID3D11Texture2D> frameTexture(const W::Graphics::Capture::Direct3D11CaptureFrame& frame) {
    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    com_ptr<ID3D11Texture2D> texture;
    check_hresult(access->GetInterface(guid_of<ID3D11Texture2D>(), texture.put_void()));
    return texture;
}

bool isInside(const POINT& origin, const SIZE& client, UINT width, UINT height) noexcept {
    return origin.x >= 0 && origin.y >= 0
        && static_cast<UINT64>(origin.x) + static_cast<UINT>(client.cx) <= width
        && static_cast<UINT64>(origin.y) + static_cast<UINT>(client.cy) <= height;
}

bool roiFitsClientArea(HWND window, const Rect& roi) noexcept {
    ThreadDpiContext dpi;
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    return width > 0 && height > 0 && roi.x >= 0 && roi.y >= 0
        && static_cast<int64_t>(roi.x) + roi.width <= width
        && static_cast<int64_t>(roi.y) + roi.height <= height;
}

// A window capture may be client-only or include its non-client frame, depending
// on the window/provider. Infer the texture coordinate system from its size.
bool clientOriginInCapture(HWND window, UINT sourceWidth, UINT sourceHeight, POINT& origin) noexcept {
    ThreadDpiContext dpi;
    RECT clientRect{};
    POINT clientTopLeft{};
    if (!GetClientRect(window, &clientRect) || !ClientToScreen(window, &clientTopLeft)) {
        return false;
    }
    const SIZE client{clientRect.right - clientRect.left, clientRect.bottom - clientRect.top};
    if (client.cx <= 0 || client.cy <= 0) return false;

    // Exact client dimensions are unambiguous and avoid adding a non-client
    // offset to providers that capture client content only.
    if (static_cast<UINT>(client.cx) == sourceWidth && static_cast<UINT>(client.cy) == sourceHeight) {
        origin = {};
        return true;
    }

    const auto tryBounds = [&](const RECT& bounds) noexcept {
        const POINT candidate{clientTopLeft.x - bounds.left, clientTopLeft.y - bounds.top};
        if (!isInside(candidate, client, sourceWidth, sourceHeight)) return false;
        origin = candidate;
        return true;
    };

    // WGC captures the visible DWM frame. GetWindowRect may additionally include
    // invisible resize borders, which makes a valid client area appear wider
    // than the captured texture after a DPI/monitor transition.
    RECT visibleFrame{};
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
            &visibleFrame, sizeof(visibleFrame)))
        && tryBounds(visibleFrame)) {
        return true;
    }

    // Keep compatibility with providers/windows that do use the full Win32
    // window rectangle rather than the compositor's visible frame.
    RECT windowRect{};
    return GetWindowRect(window, &windowRect) && tryBounds(windowRect);
}
} // namespace

struct Capture::Session final {
    std::recursive_mutex mutex;
    HWND window{};
    Rect roi{};
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11Texture2D> staging;
    W::Graphics::DirectX::Direct3D11::IDirect3DDevice direct3dDevice{nullptr};
    W::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
    W::Graphics::Capture::GraphicsCaptureSession captureSession{nullptr};
    W::Graphics::SizeInt32 poolSize{};
    event_token frameToken{};
    std::atomic_bool active{false};
    std::atomic_uint64_t sequence{0};
    Capture* owner{};

    void recreateFramePool(W::Graphics::SizeInt32 size) {
        if (poolSize.Width == size.Width && poolSize.Height == size.Height) return;
        framePool.Recreate(direct3dDevice, W::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        poolSize = size;
        // ROI dimensions are stable client-area configuration, so staging stays
        // allocated. It is recreated only when ROI dimensions change at START.
    }

    void stop() noexcept {
        active.store(false, std::memory_order_release);
        std::scoped_lock lock(mutex);
        try { if (framePool) framePool.FrameArrived(frameToken); } catch (...) {}
        try { if (captureSession) captureSession.Close(); } catch (...) {}
        try { if (framePool) framePool.Close(); } catch (...) {}
        captureSession = nullptr;
        framePool = nullptr;
        staging = nullptr;
        context = nullptr;
        device = nullptr;
        direct3dDevice = nullptr;
    }

    void onFrame(W::Graphics::Capture::Direct3D11CaptureFramePool const& sender) noexcept {
        if (!active.load(std::memory_order_acquire)) return;
        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame || !active.load(std::memory_order_acquire)) return;
            const auto contentSize = frame.ContentSize();
            if (contentSize.Width <= 0 || contentSize.Height <= 0) return;

            std::scoped_lock lock(mutex);
            if (!active.load(std::memory_order_acquire) || !staging || !context) return;
            recreateFramePool(contentSize);

            const UINT sourceWidth = static_cast<UINT>(contentSize.Width);
            const UINT sourceHeight = static_cast<UINT>(contentSize.Height);
            POINT clientOrigin{};
            if (!clientOriginInCapture(window, sourceWidth, sourceHeight, clientOrigin)) return;
            const LONG left = static_cast<LONG>(clientOrigin.x) + roi.x;
            const LONG top = static_cast<LONG>(clientOrigin.y) + roi.y;
            if (left < 0 || top < 0
                || static_cast<UINT64>(left) + static_cast<UINT>(roi.width) > sourceWidth
                || static_cast<UINT64>(top) + static_cast<UINT>(roi.height) > sourceHeight) {
                return;
            }

            auto target = staging;       // STOP may release the members in a callback.
            auto immediate = context;
            auto source = frameTexture(frame);
            const D3D11_BOX box{
                static_cast<UINT>(left), static_cast<UINT>(top), 0,
                static_cast<UINT>(left + roi.width), static_cast<UINT>(top + roi.height), 1};
            immediate->CopySubresourceRegion(target.get(), 0, 0, 0, 0, source.get(), 0, &box);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hresult(immediate->Map(target.get(), 0, D3D11_MAP_READ, 0, &mapped));
            const auto now = std::chrono::steady_clock::now();
            const auto id = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            const BgraRoiFrame output{static_cast<const std::uint8_t*>(mapped.pData), static_cast<UINT>(roi.width),
                static_cast<UINT>(roi.height), mapped.RowPitch, id, now};
            const CapturePreviewInfo preview{roi, sourceWidth, sourceHeight, id, now};

            FrameCallback frameCallback;
            PreviewCallback previewCallback;
            { std::scoped_lock ownerLock(owner->mutex_); frameCallback = owner->frameCallback_; previewCallback = owner->previewCallback_; }

            // The callback is synchronous by contract: output.pixels is valid
            // strictly between Map and Unmap, including if it calls Capture::stop.
            try { if (previewCallback) previewCallback(preview); } catch (...) {}
            try { if (frameCallback) frameCallback(output); } catch (...) {}
            immediate->Unmap(target.get(), 0);
        } catch (...) {
            // A minimized/destroyed source can invalidate a frame. A later
            // FrameArrived event resumes capture without any polling loop.
        }
    }
};

Capture::Capture() = default;
Capture::~Capture() { stop(); }

bool Capture::start(HWND sourceWindow, Rect roi, std::wstring& error) {
    stop();
    ThreadDpiContext dpi;
    if (!IsWindow(sourceWindow)) { error = L"直播窗口无效"; return false; }
    if (!roi.valid()) { error = L"ROI 尚未校准"; return false; }
    if (!roiFitsClientArea(sourceWindow, roi)) { error = L"ROI 超出直播窗口的 Client Area；请重新框选"; return false; }

    std::shared_ptr<Session> session;
    const auto cleanupApartment = [this]() noexcept {
        std::scoped_lock lock(mutex_);
        if (apartmentInitialized_ && apartmentThread_ == std::this_thread::get_id()) {
            uninit_apartment();
            apartmentInitialized_ = false;
            apartmentThread_ = {};
        }
    };
    try {
        session = std::make_shared<Session>();
        session->window = sourceWindow;
        session->roi = roi;
        session->owner = this;
        init_apartment(apartment_type::single_threaded);
        { std::scoped_lock lock(mutex_); apartmentInitialized_ = true; apartmentThread_ = std::this_thread::get_id(); }
        if (!W::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
            error = L"当前系统或显卡不支持 Windows Graphics Capture";
            session->stop();
            cleanupApartment();
            return false;
        }

        constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL level{};
        check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, session->device.put(), &level, session->context.put()));
        session->direct3dDevice = createDirect3DDevice(session->device.get());

        D3D11_TEXTURE2D_DESC stagingDesc{};
        stagingDesc.Width = static_cast<UINT>(roi.width);
        stagingDesc.Height = static_cast<UINT>(roi.height);
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check_hresult(session->device->CreateTexture2D(&stagingDesc, nullptr, session->staging.put()));

        auto item = createItem(sourceWindow);
        session->poolSize = item.Size();
        session->framePool = W::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            session->direct3dDevice, W::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, session->poolSize);
        std::weak_ptr<Session> weak = session;
        session->frameToken = session->framePool.FrameArrived([weak](auto const& pool, auto const&) {
            if (auto locked = weak.lock()) locked->onFrame(pool);
        });
        session->captureSession = session->framePool.CreateCaptureSession(item);
        { std::scoped_lock lock(mutex_); session_ = session; }
        session->active.store(true, std::memory_order_release);
        session->captureSession.StartCapture();
        return true;
    } catch (const hresult_error& ex) {
        stop();
        if (session) session->stop();
        cleanupApartment();
        error = hresultMessage(ex.code());
    } catch (...) {
        stop();
        if (session) session->stop();
        cleanupApartment();
        error = L"初始化 Windows Graphics Capture 时发生未知错误";
    }
    return false;
}

void Capture::stop() noexcept {
    std::shared_ptr<Session> old;
    { std::scoped_lock lock(mutex_); old = std::move(session_); }
    if (old) old->stop();
    std::scoped_lock lock(mutex_);
    if (apartmentInitialized_ && apartmentThread_ == std::this_thread::get_id()) {
        uninit_apartment();
        apartmentInitialized_ = false;
        apartmentThread_ = {};
    }
}

bool Capture::running() const noexcept {
    std::scoped_lock lock(mutex_);
    return session_ && session_->active.load(std::memory_order_acquire);
}

void Capture::setFrameCallback(FrameCallback callback) { std::scoped_lock lock(mutex_); frameCallback_ = std::move(callback); }
void Capture::setPreviewCallback(PreviewCallback callback) { std::scoped_lock lock(mutex_); previewCallback_ = std::move(callback); }
} // namespace valinvite
