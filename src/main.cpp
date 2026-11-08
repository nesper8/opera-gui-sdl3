#include "app/application/Application.hpp"

#include "app/logging/Logger.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        app::Application application;
        return application.run();
    } catch (const std::exception& error) {
        app::Logger::write(app::LogLevel::Error, error.what());
        std::cerr << "Opera SDL3 Frontend: " << error.what() << '\n';
        return 1;
    } catch (...) {
        app::Logger::write(app::LogLevel::Error, "Fatal unknown application error.");
        std::cerr << "Opera SDL3 Frontend: fatal unknown error.\n";
        return 1;
    }
}
