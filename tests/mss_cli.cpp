/**
 * mss_cli — minimal C++ CLI for cross-language compatibility tests.
 *
 * Usage:
 *   mss_cli write <ON|OFF> <path>   Write state to path; exits 0 on success.
 *   mss_cli read  <path>            Read state from path; prints ON/OFF/UNKNOWN; exits 0.
 */

#include "maintenance_state_store/maintenance_state_store.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: mss_cli write <ON|OFF> <path>\n"
                  << "       mss_cli read  <path>\n";
        return 1;
    }

    const std::string_view command = argv[1];

    if (command == "write") {
        if (argc < 4) {
            std::cerr << "Usage: mss_cli write <ON|OFF> <path>\n";
            return 1;
        }
        const std::string_view state_str = argv[2];
        const std::string      path      = argv[3];

        maintenance::State state;
        if (state_str == "ON") {
            state = maintenance::State::ON;
        } else if (state_str == "OFF") {
            state = maintenance::State::OFF;
        } else {
            std::cerr << "Unknown state: " << state_str << "\n";
            return 1;
        }

        maintenance::Store store(path);
        return store.write(state) ? 0 : 1;

    } else if (command == "read") {
        const std::string path = argv[2];

        maintenance::Store store(path);
        switch (store.read()) {
        case maintenance::State::ON:
            std::cout << "ON\n";
            return 0;
        case maintenance::State::OFF:
            std::cout << "OFF\n";
            return 0;
        case maintenance::State::UNKNOWN:
            std::cout << "UNKNOWN\n";
            return 0;
        }
    }

    std::cerr << "Unknown command: " << command << "\n";
    return 1;
}
