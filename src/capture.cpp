#include "capture.hpp"

#include <d3d11.h>
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

std::wstring hresultMessage(HRESULT hr) { return L"Windows Graphics Capture 初始化失败 (0x" + std::to_wstring(static_cast<unsigned long>(hr)) + L")"; }

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

bool clientOriginInCapture(HWND window, UINT sourceWidth, UINT sourceHeight, POINT& origin) noexcept {
    RECT windowRect{}; POINT client{};
    if (!GetWindowRect(window, &windowRect) || !ClientToScreen(window, &client)) return false;
    origin = {client.x - windowRect.left, client.y - windowRect.top};
    // Some capture providers expose client-only content. In that case it is the
    // only sensible coordinate system and the client offset must be zero.
    if (origin.x < 0 || origin.y < 0 || static_cast<UINT>(origin.x) >= sourceWidth || static_cast<UINT>(origin.y) >= sourceHeight) origin = {};
    return true;
}
} // namespace

struct Capture::Session final : std::enable_shared_from_this<Capture::Session> {
    std::recursive_mutex mutex;
    HWND window{};
    Rect roi{};
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11Texture2D> staging;
    W::Graphics::DirectX::Direct3D11::IDirect3DDevice direct3dDevice{nullptr};
    W::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
    W::Graphics::Capture::GraphicsCaptureSession captureSession{nullptr};
    event_token frameToken{};
    std::atomic_bool active{false};
    std::atomic_uint64_t sequence{0};
    bool apartmentInitialized{};
    std::thread::id apartmentThread{};
    Capture* owner{};

    void stop() noexcept {
        active.store(false, std::memory_order_release);
        std::scoped_lock lock(mutex);
        try { if (framePool) framePool.FrameArrived(frameToken); } catch (...) {}
        try { if (captureSession) captureSession.Close(); } catch (...) {}
        try { if (framePool) framePool.Close(); } catch (...) {}
        captureSession = nullptr; framePool = nullptr; staging = nullptr; context = nullptr; device = nullptr; direct3dDevice = nullptr;
        if (apartmentInitialized && apartmentThread == std::this_thread::get_id()) { uninit_apartment(); apartmentInitialized = false; }
    }

    void onFrame(W::Graphics::Capture::Direct3D11CaptureFramePool const& sender) noexcept {
        if (!active.load(std::memory_order_acquire)) return;
        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame || !active.load(std::memory_order_acquire)) return;
            const auto size = frame.ContentSize();
            if (size.Width <= 0 || size.Height <= 0) return;
            const UINT sourceWidth = static_cast<UINT>(size.Width), sourceHeight = static_cast<UINT>(size.Height);
            POINT clientOrigin{};
            if (!clientOriginInCapture(window, sourceWidth, sourceHeight, clientOrigin)) return;
            const LONG left = static_cast<LONG>(clientOrigin.x + roi.x), top = static_cast<LONG>(clientOrigin.y + roi.y);
            if (left < 0 || top < 0 || static_cast<UINT64>(left) + static_cast<UINT>(roi.width) > sourceWidth || static_cast<UINT64>(top) + static_cast<UINT>(roi.height) > sourceHeight) return;

            FrameCallback callback; PreviewCallback preview;
            BgraRoiFrame output{}; CapturePreviewInfo previewInfo{};
            {
                std::scoped_lock lock(mutex);
                if (!active.load(std::memory_order_acquire) || !staging) return;
                auto target = staging;
                auto immediate = context;
                auto source = frameTexture(frame);
                D3D11_BOX box{static_cast<UINT>(left), static_cast<UINT>(top), 0, static_cast<UINT>(left + roi.width), static_cast<UINT>(top + roi.height), 1};
                immediate->CopySubresourceRegion(target.get(), 0, 0, 0, 0, source.get(), 0, &box);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                check_hresult(immediate->Map(target.get(), 0, D3D11_MAP_READ, 0, &mapped));
                const auto now = std::chrono::steady_clock::now(); const auto id = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
                output = {static_cast<const std::uint8_t*>(mapped.pData), static_cast<UINT>(roi.width), static_cast<UINT>(roi.height), mapped.RowPitch, id, now};
                previewInfo = {roi, sourceWidth, sourceHeight, id, now};
                { std::scoped_lock ownerLock(owner->mutex_); callback = owner->frameCallback_; preview = owner->previewCallback_; }
                // The mapped pointer cannot survive this scope; invoke synchronously.
                try { if (preview) preview(previewInfo); } catch (...) {}
                try { if (callback) callback(output); } catch (...) {}
                immediate->Unmap(target.get(), 0);
            }
        } catch (...) {
            // A destroyed/minimized source can make a frame invalid. The session
            // remains event-driven and can resume when the provider recovers.
        }
    }
};

Capture::Capture() = default;
Capture::~Capture() { stop(); }

bool Capture::start(HWND sourceWindow, Rect roi, std::wstring& error) {
    stop();
    if (!IsWindow(sourceWindow)) { error = L"直播窗口无效"; return false; }
    if (!roi.valid()) { error = L"ROI 尚未校准"; return false; }
    std::shared_ptr<Session> session;
    try {
        session = std::make_shared<Session>(); session->window = sourceWindow; session->roi = roi; session->owner = this;
        init_apartment(apartment_type::single_threaded); session->apartmentInitialized = true; session->apartmentThread = std::this_thread::get_id();
        if (!W::Graphics::Capture::GraphicsCaptureSession::IsSupported()) { error = L"当前系统或显卡不支持 Windows Graphics Capture"; session->stop(); return false; }
        constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}; D3D_FEATURE_LEVEL level{};
        check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, session->device.put(), &level, session->context.put()));
        session->direct3dDevice = createDirect3DDevice(session->device.get());
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = static_cast<UINT>(roi.width); desc.Height = static_cast<UINT>(roi.height); desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check_hresult(session->device->CreateTexture2D(&desc, nullptr, session->staging.put()));
        auto item = createItem(sourceWindow); const auto content = item.Size();
        session->framePool = W::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(session->direct3dDevice, W::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, content);
        std::weak_ptr<Session> weak = session;
        session->frameToken = session->framePool.FrameArrived([weak](auto const& pool, auto const&) { if (auto locked = weak.lock()) locked->onFrame(pool); });
        session->captureSession = session->framePool.CreateCaptureSession(item); session->active.store(true, std::memory_order_release); session->captureSession.StartCapture();
        std::scoped_lock lock(mutex_); session_ = std::move(session); return true;
    } catch (const hresult_error& ex) { if (session) session->stop(); error = hresultMessage(ex.code()); } catch (...) { if (session) session->stop(); error = L"初始化 Windows Graphics Capture 时发生未知错误"; }
    return false;
}
void Capture::stop() noexcept { std::shared_ptr<Session> old; { std::scoped_lock lock(mutex_); old = std::move(session_); } if (old) old->stop(); }
bool Capture::running() const noexcept { std::scoped_lock lock(mutex_); return session_ && session_->active.load(std::memory_order_acquire); }
void Capture::setFrameCallback(FrameCallback callback) { std::scoped_lock lock(mutex_); frameCallback_ = std::move(callback); }
void Capture::setPreviewCallback(PreviewCallback callback) { std::scoped_lock lock(mutex_); previewCallback_ = std::move(callback); }
} // namespace valinvite
