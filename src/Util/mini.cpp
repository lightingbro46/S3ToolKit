#include "mini.h"
#include <cstdio>
#include <mutex>
#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "logger.h"
#include "uv_errno.h"

using namespace std;

namespace toolkit {

bool saveIniFile(const string &fileName, const string &content) {
    static mutex s_mtx;
    lock_guard<mutex> lck(s_mtx);
    const auto tmp = fileName + ".tmp";
    FILE *fp = fopen(tmp.data(), "wb");
    if (!fp) {
        WarnL << "Open " << tmp << " failed: " << get_uv_errmsg(false);
        return false;
    }
    bool ok = fwrite(content.data(), 1, content.size(), fp) == content.size() && fflush(fp) == 0;
#if defined(_WIN32)
    ok = ok && _commit(_fileno(fp)) == 0;
#else
    ok = ok && fsync(fileno(fp)) == 0;
#endif
    ok = fclose(fp) == 0 && ok;
#if defined(_WIN32)
    ok = ok && MoveFileExA(tmp.data(), fileName.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    ok = ok && rename(tmp.data(), fileName.data()) == 0;
#endif
    if (!ok) {
        WarnL << "Save " << fileName << " failed: " << get_uv_errmsg(false);
        remove(tmp.data());
    }
    return ok;
}

template<>
mINI_basic<variant> &mINI_basic<variant>::Instance(){
    static mINI_basic<variant> instance;
    return instance;
}

template <>
bool variant::as<bool>() const {
    if (empty() || isdigit(front())) {
        //Starts with a number
        return as_default<bool>();
    }
    if (strToLower(std::string(*this)) == "true") {
        return true;
    }
    if (strToLower(std::string(*this)) == "false") {
        return false;
    }
    //Unrecognized string
    return as_default<bool>();
}

template<>
uint8_t variant::as<uint8_t>() const {
    return 0xFF & as_default<int>();
}

}  // namespace toolkit


