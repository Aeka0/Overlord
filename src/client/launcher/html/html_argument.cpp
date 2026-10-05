#include <std_include.hpp>
#include "html_argument.hpp"
#include <climits>

html_argument::html_argument(VARIANT* val) : value_(val)
{
}

bool html_argument::is_empty() const
{
	return this->value_ == nullptr || this->value_->vt == VT_EMPTY;
}

bool html_argument::is_string() const
{
	if (this->is_empty()) return false;
	return this->value_->vt == VT_BSTR;
}

bool html_argument::is_number() const
{
	if (this->is_empty()) return false;
	return this->value_->vt == VT_I4;
}

bool html_argument::is_bool() const
{
	if (this->is_empty()) return false;
	return this->value_->vt == VT_BOOL;
}

std::string html_argument::get_string() const
{
	if (!this->is_string()) return {};
	const auto text = this->value_->bstrVal;
	const auto count = SysStringLen(text);
	if (!count) return {};
	if (count > INT_MAX) throw std::runtime_error("HTML argument is too large");
	const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
		static_cast<int>(count), nullptr, 0, nullptr, nullptr);
	if (!length) throw std::runtime_error("Invalid UTF-16 HTML argument");
	std::string result(length, '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(count),
		result.data(), length, nullptr, nullptr);
	return result;
}

int html_argument::get_number() const
{
	if (!this->is_number()) return 0;
	return this->value_->intVal;
}

bool html_argument::get_bool() const
{
	if (!this->is_bool()) return false;
	return this->value_->boolVal != FALSE;
}

void html_argument::set_string(const std::string& value)
{
	if (!this->value_) return;
	if (value.size() > INT_MAX) throw std::runtime_error("HTML callback result is too large");
	const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), nullptr, 0);
	if (!length && !value.empty()) throw std::runtime_error("Invalid UTF-8 HTML callback result");
	const auto text = SysAllocStringLen(nullptr, length);
	if (!text) throw std::bad_alloc();
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), text, length);
	VariantClear(this->value_);
	this->value_->vt = VT_BSTR;
	this->value_->bstrVal = text;
}
