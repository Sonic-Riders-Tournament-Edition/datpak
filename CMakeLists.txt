cmake_minimum_required(VERSION 3.5)
project(DatPak CXX)

include_directories(include)

set(CMAKE_CXX_STANDARD 23)
set(WARNING_FLAGS
		#-Werror    # Turns all warnings into errors
		-Wall   # Enable most warning messages.
		-Wextra # Print extra (possibly unwanted) warnings.
		-Wpedantic # Issue warnings needed for strict compliance to the standard.
		-Wcast-align    # Warn about pointer casts which increase alignment. For example, warn if a char * is cast to an int * on machines where integers can only be accessed at two- or four-byte boundaries.
		-Wcast-qual # Warn about casts which discard qualifiers. For example, warn if a const char * is cast to an ordinary char *
		-Wctor-dtor-privacy # Warn when all constructors and destructors are private.
		-Wdisabled-optimization # Warn when an optimization pass is disabled.
		-Wformat=2  # Warn about printf/scanf/strftime/strfmon format string anomalies.
		-Winit-self # Warn about variables which are initialized to themselves.
		-Wlogical-op    # Warn when a logical operator is suspiciously always evaluating to true or false.
		-Wmissing-declarations # Warn about global functions without previous declarations.
		-Wmissing-include-dirs # Warn about user-specified include directories that do not exist.
		-Wnoexcept  # Warn when a noexcept expression evaluates to false even though the expression can't actually throw.
		-Wold-style-cast    # Warn if a C-style cast is used in a program.
		-Woverloaded-virtual    # Warn about overloaded virtual function names.
		-Wredundant-decls   # Warn about multiple declarations of the same object.
		-Wshadow    # Warn when one variable shadows another # Might disable this one
		-Wsign-conversion   # Warn for implicit type conversions between signed and unsigned integers.
		-Wsign-promo    # Warn when overload promotes from unsigned to signed.
		-Wstrict-null-sentinel  # Warn about un-casted NULL used as sentinel.
		-Wstrict-overflow=5 # Warn about optimizations that assume that signed overflow is undefined.
		-Wswitch-default    # Warn about enumerated switches missing a "default:" statement.
		-Wundef # Warn if an undefined macro is used in an #if directive.
		-Wzero-as-null-pointer-constant # Warn when a literal '0' is used as null pointer.
		-Wuseless-cast  # Warn about useless casts.

		-Wno-unknown-pragmas # Disables warning about unknown pragmas since both clion and clang-tidy use their own
)

find_package(fmt CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(gcem CONFIG REQUIRED)
find_package(DspTool 1 REQUIRED)

if (MSVC)
	# Force to always compile with W4
	if (CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
		string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	else ()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
else ()
	message(WARNING "CMake flags for compiler aren't set for compiler ${CMAKE_CXX_COMPILER_ID}")
endif ()

option (FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if (${FORCE_COLORED_OUTPUT})
	if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		add_compile_options (-fdiagnostics-color=always)
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		add_compile_options (-fcolor-diagnostics)
	endif ()
endif ()


add_executable(DatPak main.cpp gcaxArchive.cpp)
target_link_libraries(DatPak PUBLIC DspTool::DspTool fmt::fmt-header-only cxxopts::cxxopts gcem)
target_compile_options(DatPak PUBLIC ${WARNING_FLAGS})
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-g -DDEBUG -save-temps -fverbose-asm")


install(TARGETS DatPak RUNTIME)