#pragma once

#include <string>
#include <memory>

class Clipboard {
public:
    Clipboard();
    ~Clipboard() = default;

    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    bool copy(const std::string& text);
    bool paste();
    bool copy_and_paste(const std::string& text, bool append_newline);

    std::string save();
    bool restore(const std::string& previous);

private:
    bool paste_via_wtype();
    bool paste_via_ydotool();
};
