#include "core/ChangeQueue.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace wid::core {

namespace {

std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string escapeField(const std::wstring& w) {
    std::string s = toUtf8(w);
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\')      out += "\\\\";
        else if (c == '|')  out += "\\p";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out.push_back(c);
    }
    return out;
}

std::wstring unescapeField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case '\\': out.push_back('\\'); break;
                case 'p':  out.push_back('|');  break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                default:   out.push_back(n);    break;
            }
        } else out.push_back(s[i]);
    }
    return fromUtf8(out);
}

} // namespace

std::uint64_t ChangeQueue::enqueue(PendingChange c) {
    std::uint64_t id;
    {
        std::lock_guard lk(mtx_);
        c.id = nextId_++;
        id = c.id;
        items_.push_back(std::move(c));
    }
    notify_();
    return id;
}

bool ChangeQueue::remove(std::uint64_t id) {
    bool removed = false;
    {
        std::lock_guard lk(mtx_);
        auto it = std::find_if(items_.begin(), items_.end(),
                               [&](const PendingChange& c){ return c.id == id; });
        if (it != items_.end()) { items_.erase(it); removed = true; }
    }
    if (removed) notify_();
    return removed;
}

bool ChangeQueue::moveUp(std::uint64_t id) {
    bool moved = false;
    {
        std::lock_guard lk(mtx_);
        for (size_t i = 1; i < items_.size(); ++i) {
            if (items_[i].id == id) {
                std::swap(items_[i], items_[i - 1]);
                moved = true; break;
            }
        }
    }
    if (moved) notify_();
    return moved;
}

bool ChangeQueue::moveDown(std::uint64_t id) {
    bool moved = false;
    {
        std::lock_guard lk(mtx_);
        for (size_t i = 0; i + 1 < items_.size(); ++i) {
            if (items_[i].id == id) {
                std::swap(items_[i], items_[i + 1]);
                moved = true; break;
            }
        }
    }
    if (moved) notify_();
    return moved;
}

void ChangeQueue::clear() {
    {
        std::lock_guard lk(mtx_);
        items_.clear();
    }
    notify_();
}

std::vector<PendingChange> ChangeQueue::snapshot() const {
    std::lock_guard lk(mtx_);
    return items_;
}

std::size_t ChangeQueue::size() const {
    std::lock_guard lk(mtx_);
    return items_.size();
}

void ChangeQueue::notify_() {
    if (listener_) listener_();
}

bool ChangeQueue::saveToFile(const std::wstring& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "# WID Utility profile v1\n";
    auto items = snapshot();
    for (const auto& c : items) {
        f << (int)c.kind   << '|'
          << (int)c.action << '|'
          << escapeField(c.targetId)    << '|'
          << escapeField(c.description) << '|'
          << escapeField(c.payload)     << '|'
          << (c.continueOnError ? 1 : 0)
          << '\n';
    }
    return f.good();
}

bool ChangeQueue::loadFromFile(const std::wstring& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::vector<PendingChange> loaded;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields;
        std::string cur;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                cur.push_back(line[i++]);
                cur.push_back(line[i]);
            } else if (line[i] == '|') {
                fields.push_back(cur); cur.clear();
            } else cur.push_back(line[i]);
        }
        fields.push_back(cur);
        if (fields.size() < 6) continue;

        PendingChange c;
        c.kind            = (ChangeKind)  std::stoi(fields[0]);
        c.action          = (ChangeAction)std::stoi(fields[1]);
        c.targetId        = unescapeField(fields[2]);
        c.description     = unescapeField(fields[3]);
        c.payload         = unescapeField(fields[4]);
        c.continueOnError = std::stoi(fields[5]) != 0;
        loaded.push_back(std::move(c));
    }
    {
        std::lock_guard lk(mtx_);
        items_ = std::move(loaded);
        nextId_ = 1;
        for (auto& c : items_) c.id = nextId_++;
    }
    notify_();
    return true;
}

} // namespace wid::core
