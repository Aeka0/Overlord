#include "io.hpp"
#include "nt.hpp"
#include <algorithm>
#include <fstream>
#include <limits>

namespace utils::io
{
	bool remove_file(const std::string& file)
	{
		return DeleteFileA(file.data()) == TRUE;
	}

	bool move_file(const std::string& src, const std::string& target)
	{
		return MoveFileA(src.data(), target.data()) == TRUE;
	}

	bool file_exists(const std::string& file)
	{
		return std::ifstream(file).good();
	}

	bool write_file(const std::string& file, const std::string& data, const bool append)
	{
		const auto pos = file.find_last_of("/\\");
		if (pos != std::string::npos)
		{
			create_directory(file.substr(0, pos));
		}

		std::ofstream stream(
			file, std::ios::binary | std::ofstream::out | (append ? std::ofstream::app : 0));

		if (stream.is_open())
		{
			stream.write(data.data(), data.size());
			stream.close();
			return true;
		}

		return false;
	}

	bool write_file_atomic(const std::filesystem::path& file, const std::string& data) noexcept
	{
		try
		{
			auto temporary = file;
			temporary += L".tmp";
			if (!file.parent_path().empty())
			{
				std::filesystem::create_directories(file.parent_path());
			}

			const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				return false;
			}

			bool complete = true;
			std::size_t offset{};
			while (offset < data.size())
			{
				const auto remaining = data.size() - offset;
				const auto chunk_size = static_cast<DWORD>((std::min)(remaining,
					static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
				DWORD written{};
				if (!WriteFile(handle, data.data() + offset, chunk_size, &written, nullptr) ||
					written != chunk_size)
				{
					complete = false;
					break;
				}
				offset += written;
			}

			if (complete && !FlushFileBuffers(handle))
			{
				complete = false;
			}
			if (!CloseHandle(handle))
			{
				complete = false;
			}

			if (complete && MoveFileExW(temporary.c_str(), file.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				return true;
			}

			DeleteFileW(temporary.c_str());
			return false;
		}
		catch (...)
		{
			return false;
		}
	}

	std::string read_file(const std::string& file)
	{
		std::string data;
		read_file(file, &data);
		return data;
	}

	bool read_file(const std::string& file, std::string* data)
	{
		if (!data) return false;
		data->clear();

		if (file_exists(file))
		{
			std::ifstream stream(file, std::ios::binary);
			if (!stream.is_open()) return false;

			stream.seekg(0, std::ios::end);
			const std::streamsize size = stream.tellg();
			stream.seekg(0, std::ios::beg);

			if (size > -1)
			{
				data->resize(static_cast<uint32_t>(size));
				stream.read(const_cast<char*>(data->data()), size);
				stream.close();
				return true;
			}
		}

		return false;
	}

	size_t file_size(const std::string& file)
	{
		if (file_exists(file))
		{
			std::ifstream stream(file, std::ios::binary);

			if (stream.good())
			{
				stream.seekg(0, std::ios::end);
				return static_cast<size_t>(stream.tellg());
			}
		}

		return 0;
	}

	bool create_directory(const std::string& directory)
	{
		return std::filesystem::create_directories(directory);
	}

	bool directory_exists(const std::string& directory)
	{
		return std::filesystem::is_directory(directory);
	}

	bool directory_is_empty(const std::string& directory)
	{
		return std::filesystem::is_empty(directory);
	}

	bool remove_directory(const std::string& directory)
	{
		return std::filesystem::remove_all(directory);
	}

	std::vector<std::string> list_files(const std::string& directory)
	{
		std::vector<std::string> files;

		for (auto& file : std::filesystem::directory_iterator(directory))
		{
			files.push_back(file.path().generic_string());
		}

		return files;
	}

	std::vector<std::string> list_files_recursively(const std::string& directory)
	{
		std::vector<std::string> files;

		for (auto& file : std::filesystem::recursive_directory_iterator(directory))
		{
			files.push_back(file.path().generic_string());
		}

		return files;
	}

	void copy_folder(const std::filesystem::path& src, const std::filesystem::path& target)
	{
		std::filesystem::copy(src, target,
		                      std::filesystem::copy_options::overwrite_existing |
		                      std::filesystem::copy_options::recursive);
	}
}
