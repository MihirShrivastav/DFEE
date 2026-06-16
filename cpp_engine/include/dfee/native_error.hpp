#pragma once

#include <stdexcept>
#include <string>

namespace dfee {

struct NativeError {
    std::string code;
    std::string user_message;
    std::string detail;

    [[nodiscard]] bool empty() const noexcept {
        return code.empty() && user_message.empty() && detail.empty();
    }
};

class NativeException : public std::runtime_error {
public:
    explicit NativeException(NativeError error)
        : std::runtime_error(error.detail.empty() ? error.user_message : error.detail),
          error_(std::move(error)) {}

    [[nodiscard]] const NativeError& error() const noexcept {
        return error_;
    }

private:
    NativeError error_;
};

[[nodiscard]] inline NativeException make_native_exception(
    std::string code,
    std::string user_message,
    std::string detail) {
    return NativeException({
        .code = std::move(code),
        .user_message = std::move(user_message),
        .detail = std::move(detail),
    });
}

}  // namespace dfee
