#pragma once
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <print>
#include <vector>

struct conf_load_t {
  public:
    conf_load_t() = default;
    ~conf_load_t() = default;

    auto load(std::string_view path) -> void {
        auto ifs = std::ifstream(path.data());
        if (!ifs.is_open()) {
            std::cerr << "failed to open configure file: " << path << "\n";
            exit(-1);
        }

        auto line = std::string{};
        auto row = 0;
        while (!ifs.eof()) {
            std::getline(ifs, line);
            ++row;
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto pos = line.find_first_of("=");
            if (pos == std::string::npos) {
                std::cerr << "invalid configure file: " << path
                          << ", row: " << row << "\n";
                exit(-1);
            }

            auto key = trim(line.substr(0, pos));
            auto value = pos + 1 >= line.size() ? "" : trim(line.substr(pos + 1));
            m_conf_items[key] = value;
        }
    }

    template <typename T>
    auto get_or_exit(std::string_view key) -> T {
        auto ret = get<T>(key);
        if (!ret.has_value()) {
            std::cerr << "missing value for key: " << key << "\n";
            exit(-1);
        }
        return ret.value();
    }

    template <typename T>
    auto get(std::string_view key) -> std::optional<T> {
        if (!m_conf_items.contains(key.data())) {
            std::cerr << "configure item not found: " << key << "\n";
            exit(-1);
        }
        if constexpr (std::same_as<T, int>) {
            try {
                return std::stoi(m_conf_items[key.data()]);
            } catch (const std::invalid_argument &e) {
                return std::nullopt;
            } catch (const std::out_of_range &e) {
                return std::nullopt;
            }
        } else if constexpr (std::same_as<T, std::string>) {
            return m_conf_items[key.data()];
        } else if constexpr (std::same_as<T, std::vector<std::string>>) {
            return std::optional{split(m_conf_items[key.data()])};
        } else {
            static_assert(false, "unsupported type");
        }
    }

  private:
    auto split(const std::string &str) -> std::vector<std::string> {
        auto result = std::vector<std::string>{};
        auto start = str.find_first_not_of(' ', 0);
        auto end = str.find_first_of(' ', start);

        while (start != std::string::npos) {
            result.push_back(str.substr(start, end - start));
            start = str.find_first_not_of(' ', end);
            end = str.find_first_of(' ', start);
        }

        return result;
    }

    auto trim(const std::string &str) -> std::string {
        auto start = str.find_first_not_of(' ');
        auto end = str.find_last_not_of(' ');
        return str.substr(start, end - start + 1);
    }

  private:
    std::map<std::string, std::string> m_conf_items;
};