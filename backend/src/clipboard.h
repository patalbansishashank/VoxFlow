#pragma once

#include <string>
#include <memory>

// A saved clipboard selection: the exact bytes of the most useful MIME type on offer
// (text stays text, an image stays image/png, ...), so it can be restored losslessly
// after we paste the transcript. mime == "" means there was nothing to save.
struct ClipSaved {
    std::string mime;
    std::string data;
    bool empty() const { return mime.empty() || data.empty(); }
};

class Clipboard {
public:
    Clipboard();
    ~Clipboard() = default;

    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    bool copy(const std::string& text);
    bool paste();
    bool copy_and_paste(const std::string& text, bool append_newline);

    ClipSaved save();
    bool restore(const ClipSaved& previous);

private:
    bool paste_via_wtype();
    bool paste_via_ydotool();
};
