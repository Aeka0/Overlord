#include "http.hpp"
#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <gsl/gsl>

#pragma comment(lib, "ws2_32.lib")

#ifdef max
#undef max
#endif

namespace utils::http
{
	namespace
	{
		struct progress_helper
		{
			const std::function<void(size_t, size_t, size_t)>* callback{};
			const request_options* options{};
			std::string* buffer{};
			std::exception_ptr exception{};
			std::chrono::steady_clock::time_point start{};
		};

		int progress_callback(void *clientp, const curl_off_t dltotal, const curl_off_t dlnow, const curl_off_t /*ultotal*/, const curl_off_t /*ulnow*/)
		{
			auto* helper = static_cast<progress_helper*>(clientp);

			try
			{
				if (helper->options->cancelled && helper->options->cancelled()) return 1;
				const auto now = std::chrono::steady_clock::now();
				const auto count = std::max<std::int64_t>(1, std::chrono::duration_cast<
					std::chrono::seconds>(now - helper->start).count());
				const auto speed = dlnow / count;

				if (*helper->callback)
				{
					(*helper->callback)(dlnow, dltotal, speed);
				}
			}
			catch(...)
			{
				helper->exception = std::current_exception();
				return -1;
			}

			return 0;
		}
	
		size_t write_callback(void* contents, const size_t size, const size_t nmemb, void* userp)
		{
			auto* helper = static_cast<progress_helper*>(userp);
			try
			{
				if (helper->options->cancelled && helper->options->cancelled()) return 0;
				if (nmemb && size > (std::numeric_limits<size_t>::max)() / nmemb) return 0;
				const auto total_size = size * nmemb;
				const auto limit = (std::min)(helper->buffer->max_size(), helper->options->max_response_bytes);
				if (helper->buffer->size() > limit || total_size > limit - helper->buffer->size()) return 0;
				helper->buffer->append(static_cast<char*>(contents), total_size);
				return total_size;
			}
			catch (...)
			{
				helper->exception = std::current_exception();
				return 0;
			}
		}
	}

	std::optional<std::string> get_data(const std::string& url, const headers& headers, 
		const std::function<void(size_t, size_t, size_t)>& callback, const request_options& options)
	{
		if (options.connect_timeout.count() <= 0 || options.timeout.count() <= 0 ||
			options.connect_timeout.count() > (std::numeric_limits<long>::max)() ||
			options.timeout.count() > (std::numeric_limits<long>::max)() ||
			(options.cancelled && options.cancelled())) return {};
		curl_slist* header_list = nullptr;
		auto* curl = curl_easy_init();
		if (!curl)
		{
			return {};
		}

		auto _ = gsl::finally([&]()
		{
			curl_slist_free_all(header_list);
			curl_easy_cleanup(curl);
		});
		
		for(const auto& header : headers)
		{
			auto data = header.first + ": " + header.second;
			header_list = curl_slist_append(header_list, data.data());
		}

		std::string buffer{};
		progress_helper helper{};
		helper.callback = &callback;
		helper.options = &options;
		helper.buffer = &buffer;
		helper.start = std::chrono::steady_clock::now();
		
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
		curl_easy_setopt(curl, CURLOPT_URL, url.data());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &helper);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &helper);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options.connect_timeout.count()));
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(options.timeout.count()));
		curl_easy_setopt(curl, CURLOPT_DNS_LOCAL_IP4, "1.1.1.1");

		if (curl_easy_perform(curl) == CURLE_OK && !(options.cancelled && options.cancelled()))
		{
			return {std::move(buffer)};
		}

		if (helper.exception)
		{
			std::rethrow_exception(helper.exception);
		}

		return {};
	}

	std::future<std::optional<std::string>> get_data_async(const std::string& url, const headers& headers)
	{
		return std::async(std::launch::async, [url, headers]()
		{
			return get_data(url, headers);
		});
	}
}
