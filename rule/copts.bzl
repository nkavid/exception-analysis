_WARNINGS = [
    "-Weverything",
    "-Wno-c++98-compat",
    "-Wno-c++98-compat-pedantic",
    "-Wno-switch-enum",
    "-Wno-switch-default",
    "-Wno-padded",
    "-Wno-missing-include-dirs",
    "-Werror",
]

EXA_COPTS = ["-std=c++20"] + _WARNINGS

EXA_TEST_COPTS = EXA_COPTS + ["-Wno-global-constructors"]
