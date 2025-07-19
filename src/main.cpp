// -------------------------------------------------------------------------------------------------
//
// Copyright (C) all of the contributors. All rights reserved.
//
// This software, including documentation, is protected by copyright controlled by
// contributors. All rights are reserved. Copying, including reproducing, storing,
// adapting or translating, any or all of this material requires the prior written
// consent of all contributors.
//
// -------------------------------------------------------------------------------------------------

#include "vstream_app.h"
#include "mic_capture.h"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        // Handle special cases first
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                vstream_app::print_usage(argv[0]);
                return 0;
            } else if (arg == "--list-devices") {
                mic_capture::list_devices();
                return 0;
            }
        }

        // Parse configuration
        auto config = vstream_app::parse_command_line(argc, argv);

        // Create and run application
        vstream_app app(config);
        return app.run();

    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl << std::endl;
        vstream_app::print_usage(argv[0]);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
