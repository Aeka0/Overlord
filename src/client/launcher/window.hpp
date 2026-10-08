#pragma once

#define WM_KILL_WINDOW (WM_USER+0)

class window
{
public:
	window();
	virtual ~window();

	void create(const std::string& title, int width, int height,
	            long flags = WS_OVERLAPPEDWINDOW);

	void close();

	void show() const;
	void hide() const;
	bool is_maximized() const;
	int resize_border() const;
	RECT content_bounds() const;
	void request_control(const std::string& action) const;

	void set_callback(const std::function<LRESULT(window*, UINT, WPARAM, LPARAM)>& callback);

	operator HWND() const;

	static void run();
	static void close_all();

protected:
	virtual LRESULT processor(UINT message, WPARAM w_param, LPARAM l_param);

private:
	uint32_t last_dpi_ = 96;

	WNDCLASSEXW wc_{};
	HWND handle_ = nullptr;
	std::wstring classname_;
	HBRUSH background_ = nullptr;
	std::function<LRESULT(window*, UINT, WPARAM, LPARAM)> callback_;

	static LRESULT CALLBACK static_processor(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

	static std::mutex mutex_;
	static std::vector<window*> windows_;

	static void remove_window(const window* window);
	static int get_window_count();
};
