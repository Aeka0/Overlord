#include <std_include.hpp>
#include "window.hpp"

std::mutex window::mutex_;
std::vector<window*> window::windows_;

window::window()
{
	static std::atomic<unsigned> next_class{};
	classname_ = L"Overlord.Window." + std::to_wstring(++next_class);
	wc_.cbSize = sizeof(wc_);
	wc_.style = CS_HREDRAW | CS_VREDRAW;
	wc_.lpfnWndProc = static_processor;
	wc_.hInstance = GetModuleHandleW(nullptr);
	wc_.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
	wc_.hIcon = LoadIconW(wc_.hInstance, MAKEINTRESOURCEW(ID_ICON));
	wc_.hIconSm = wc_.hIcon;
	background_ = CreateSolidBrush(RGB(26, 26, 26));
	wc_.hbrBackground = background_;
	wc_.lpszClassName = classname_.c_str();
	if (!RegisterClassExW(&wc_)) { DeleteObject(background_); throw std::runtime_error("Could not register the launcher window."); }
}

window::~window() { close(); UnregisterClassW(classname_.c_str(), wc_.hInstance); if (background_) DeleteObject(background_); }

void window::create(const std::string& title, int width, int height, const long flags)
{
	const auto dpi = GetDpiForSystem();
	last_dpi_ = dpi;
	RECT rect{0, 0, MulDiv(width, dpi, 96), MulDiv(height, dpi, 96)};
	MONITORINFO monitor{sizeof(monitor)};
	GetMonitorInfoW(MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY), &monitor);
	width = std::min(rect.right - rect.left, monitor.rcWork.right - monitor.rcWork.left);
	height = std::min(rect.bottom - rect.top, monitor.rcWork.bottom - monitor.rcWork.top);
	const std::wstring caption(title.begin(), title.end());
	{ std::lock_guard lock(mutex_); windows_.push_back(this); }
	handle_ = CreateWindowExW(0, classname_.c_str(), caption.c_str(), flags,
		monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
		monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
		width, height, nullptr, nullptr, wc_.hInstance, this);
	if (!handle_) { remove_window(this); throw std::runtime_error("Could not create the launcher window."); }
	last_dpi_ = GetDpiForWindow(handle_);
	const BOOL dark = TRUE;
	DwmSetWindowAttribute(handle_, 20, &dark, sizeof(dark));
	const DWORD no_border = 0xfffffffe; // DWMWA_COLOR_NONE; optional on Windows 11.
	DwmSetWindowAttribute(handle_, 34, &no_border, sizeof(no_border));
	SetWindowPos(handle_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void window::close() { if (handle_ && IsWindow(handle_)) DestroyWindow(handle_); handle_ = nullptr; }

void window::run()
{
	MSG message{};
	for (;;)
	{
		const auto result = GetMessageW(&message, nullptr, 0, 0);
		if (!result) break;
		if (result == -1) throw std::runtime_error("Launcher message processing failed.");
		TranslateMessage(&message); DispatchMessageW(&message);
	}
}

void window::close_all()
{
	std::vector<window*> windows;
	{ std::lock_guard lock(mutex_); windows = windows_; }
	for (auto* item : windows) if (GetWindowThreadProcessId(*item, nullptr) == GetCurrentThreadId()) item->close();
}

void window::remove_window(const window* item)
{
	std::lock_guard lock(mutex_);
	windows_.erase(std::remove(windows_.begin(), windows_.end(), item), windows_.end());
}

int window::get_window_count()
{
	std::lock_guard lock(mutex_);
	return static_cast<int>(std::count_if(windows_.begin(), windows_.end(), [](auto* item)
		{ return GetWindowThreadProcessId(*item, nullptr) == GetCurrentThreadId(); }));
}

void window::show() const { ShowWindow(handle_, SW_SHOW); UpdateWindow(handle_); }
void window::hide() const { ShowWindow(handle_, SW_HIDE); }
void window::set_callback(const std::function<LRESULT(window*, UINT, WPARAM, LPARAM)>& callback) { callback_ = callback; }
bool window::is_maximized() const { return IsZoomed(handle_) != FALSE; }
int window::resize_border() const { return is_maximized() ? 0 : std::max(1, MulDiv(6, last_dpi_, 96)); }
RECT window::content_bounds() const
{
	RECT bounds{}; GetClientRect(handle_, &bounds);
	const auto inset = resize_border();
	bounds.left += inset; bounds.top += inset;
	bounds.right = std::max<LONG>(bounds.left, bounds.right - inset);
	bounds.bottom = std::max<LONG>(bounds.top, bounds.bottom - inset);
	return bounds;
}
void window::request_control(const std::string& action) const
{
	const auto command = action == "minimize" ? SC_MINIMIZE : action == "toggleMaximize" ?
		(is_maximized() ? SC_RESTORE : SC_MAXIMIZE) : action == "close" ? SC_CLOSE : 0;
	if (!command) throw std::runtime_error("Unsupported window control.");
	PostMessageW(handle_, WM_SYSCOMMAND, command, 0);
}

LRESULT window::processor(const UINT message, const WPARAM w_param, const LPARAM l_param)
{
	if (message == WM_NCCALCSIZE) return 0;
	if (message == WM_NCHITTEST)
	{
		RECT rect{}; GetWindowRect(handle_, &rect);
		const int x = static_cast<short>(LOWORD(l_param)) - rect.left;
		const int y = static_cast<short>(HIWORD(l_param)) - rect.top;
		const int edge = resize_border();
		const bool left = x < edge, right = x >= rect.right - rect.left - edge;
		const bool top = y < edge, bottom = y >= rect.bottom - rect.top - edge;
		if (top && left) return HTTOPLEFT;
		if (top && right) return HTTOPRIGHT;
		if (bottom && left) return HTBOTTOMLEFT;
		if (bottom && right) return HTBOTTOMRIGHT;
		if (left) return HTLEFT;
		if (right) return HTRIGHT;
		if (top) return HTTOP;
		if (bottom) return HTBOTTOM;
		return HTCLIENT;
	}
	if (message == WM_DPICHANGED)
	{
		last_dpi_ = HIWORD(w_param);
		const auto* rect = reinterpret_cast<const RECT*>(l_param);
		if (rect) SetWindowPos(handle_, nullptr, rect->left, rect->top, rect->right - rect->left,
			rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
		return 0;
	}
	if (message == WM_GETMINMAXINFO)
	{
		auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
		MONITORINFO monitor{sizeof(monitor)};
		GetMonitorInfoW(MonitorFromWindow(handle_, MONITOR_DEFAULTTONEAREST), &monitor);
		info->ptMinTrackSize = {std::min<LONG>(MulDiv(640, last_dpi_, 96), monitor.rcWork.right - monitor.rcWork.left),
			std::min<LONG>(MulDiv(480, last_dpi_, 96), monitor.rcWork.bottom - monitor.rcWork.top)};
		info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
		info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top};
		return 0;
	}
	if (message == WM_DESTROY) { remove_window(this); if (!get_window_count()) PostQuitMessage(0); return 0; }
	if (message == WM_KILL_WINDOW) { close(); return 0; }
	if (callback_) return callback_(this, message, w_param, l_param);
	return DefWindowProcW(handle_, message, w_param, l_param);
}

LRESULT CALLBACK window::static_processor(HWND handle, const UINT message, const WPARAM w_param, const LPARAM l_param)
{
	if (message == WM_NCCREATE)
	{
		auto* self = static_cast<window*>(reinterpret_cast<CREATESTRUCTW*>(l_param)->lpCreateParams);
		self->handle_ = handle;
		SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	auto* self = reinterpret_cast<window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
	return self ? self->processor(message, w_param, l_param) : DefWindowProcW(handle, message, w_param, l_param);
}

window::operator HWND() const { return handle_; }
