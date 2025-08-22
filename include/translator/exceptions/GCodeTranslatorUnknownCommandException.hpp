//
// Created by Andrea on 22/08/2025.
//

class GCodeTranslatorUnknownCommandException : public std::exception {
private:
    std::string message;

public:
    explicit GCodeTranslatorUnknownCommandException(const std::string& msg) : message(msg) {}

    const char* what() const noexcept override {
        return message.c_str();
    }
};