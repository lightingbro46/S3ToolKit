#include "mini.h"

using namespace std;

namespace toolkit {

template<>
mINI_basic<string, variant> &mINI_basic<string, variant>::Instance(){
    static mINI_basic<string, variant> instance;
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


