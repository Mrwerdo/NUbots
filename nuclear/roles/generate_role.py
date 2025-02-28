#!/usr/bin/env python3

import itertools
import os
import re
import sys
import textwrap

from banner import ampscii, bigtext

role_name = sys.argv[1]
banner_file = sys.argv[2]
module_path = sys.argv[3]
role_modules = sys.argv[4:]

# Open our output role file
with open(role_name, "w", encoding="utf-8") as role_file:

    # We use our NUClear header
    role_file.write("#include <nuclear>\n\n")

    # Add our module headers
    for module in role_modules:
        # Each module is given to us as Namespace::Namespace::Name.
        # we need to replace the ::'s with /'s so we can include them.

        # module::a::b::C
        # module/a/b/C/src/C.hpp

        # replace :: with /
        header = re.sub(r"::", r"/", module)

        # Try to deduce the extension for the reactor header.
        # It must be called src/ReactorName.[h|hpp|hh]

        # replace last name with src/name
        header = re.sub(r"([^\/]+)$", r"\1/src/\1", header)

        # Try several extensions (hpp, hh, h) to see if one exists
        if os.path.isfile(os.path.join(module_path, "{}.hpp".format(header))):
            header = "{}.hpp".format(header)
        elif os.path.isfile(os.path.join(module_path, "{}.hh".format(header))):
            header = "{}.hh".format(header)
        elif os.path.isfile(os.path.join(module_path, "{}.h".format(header))):
            header = "{}.h".format(header)
        else:
            raise Exception("Cannot find main header file for {}".format(header))

        role_file.write('#include "{}"\n'.format(header))

    # Add our main function and include headers
    main = textwrap.dedent(
        """\
        int main(int argc, char** argv) {"""
    )
    role_file.write(main)

    role_file.write("\n\n    // Print the logo generated by ampscii\n")

    # Generate our banner from our banner image
    banner = ampscii(banner_file)
    banner_lines = banner.replace("\x1b", "\\x1b").split("\n")[:-1]
    for l in banner_lines:
        role_file.write('    std::cerr << "{}" << std::endl;\n'.format(l))

    role_file.write("\n    // Print the name of the role in big letters\n")

    # Insert banner for the name of the executing role
    role_banner_lines = bigtext(os.path.splitext(os.path.basename(role_name))[0]).split("\n")[:-1]
    for l in role_banner_lines:
        role_file.write('    std::cerr << R"({})" << std::endl;\n'.format(l))

    start = """\

    NUClear::PowerPlant::Configuration config;
    unsigned int nThreads = std::thread::hardware_concurrency() + 2;
    config.thread_count = nThreads >= 4 ? nThreads : 4;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    NUClear::PowerPlant plant(config, argc, const_cast<const char**>(argv));"""

    role_file.write(start)

    for module in role_modules:
        role_file.write('    std::cerr << "Installing " << "{0}" << std::endl;\n'.format(module))
        role_file.write("    plant.install<module::{0}>();\n".format(module))

    end = """
    plant.start();
    return 0;
}"""
    role_file.write(end)
    role_file.write("\n")
